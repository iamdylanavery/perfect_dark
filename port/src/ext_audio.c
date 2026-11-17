#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fs.h"
#include "system.h"
#include "platform.h"
#include "game/menu.h"
#include "types.h"
#include <SDL.h>

#include "ext_audio.h"
#include "ext_audio_tracks.h"
#include "ext_music_categories.h"

extern u16 g_SfxVolume;

// -------------------------------------------------------------
// GLOBAL SETTINGS & TOGGLE STATE
// -------------------------------------------------------------
int g_ExtAudioEnabled = 1;
int g_ExtLogSfxEnabled = 0;
int g_ExtLogMusicEnabled = 0;
int g_ExtLogVoxEnabled = 0;

// -------------------------------------------------------------
// LOAD-TIME NORMALIZATION SETTINGS
// -------------------------------------------------------------
int g_ExtNormalizeMusic = 0;               // 1 = Normalize Music samples on load, 0 = Off
int g_ExtNormalizeSfx = 0;                 // 1 = Normalize SFX samples on load, 0 = Off
int g_ExtNormalizeAttenuateOnly = 0;       // 0 = Full Normalize (boost & cut), 1 = Cut loud samples only
float g_ExtNormalizeMusicTarget = 1.0f;   // 65% peak (~ -3.7 dB, matches XBLA instrument levels)
float g_ExtNormalizeSfxTarget   = 1.0f;   // 75% peak (~ -2.5 dB, matches XBLA SFX levels)
float g_ExtMicroSampleTarget    = 0.25f;   // 42% peak (~ -7.5 dB, tames sustained bass/synth pulses)
int g_ExtRemasterMicroSamples   = 0;       // 1 = Resynthesize <2000B microsamples in code, 0 = N64 Fallback

s16 g_MixBuffer[8192 * 2]; // Shared master SDL Mix Buffer

int extAudioGetEnabled(void) { return g_ExtAudioEnabled; }
void extAudioSetEnabled(int state) {
    g_ExtAudioEnabled = state;
    sysLogPrintf(LOG_NOTE, "EXT-AUDIO: External Audio has been %s.", state ? "ENABLED" : "DISABLED");
}

// NEW: Global Slider Variables
extern float g_ExtVoxVolumeScale;
int g_ExtEasterEggChance = 1; // Default 1% (1 in 100)

static const char* g_VoxVolNames[] = {
    "Muted (0%)", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%", "Boosted (150%)", "Loud (200%)"
};
static const float g_VoxVolValues[] = {
    0.00f, 0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f, 1.50f, 2.00f
};

static const char* g_EasterEggNames[] = {
    "Off", "(1%)", "(10%)", "(25%)", "(50%)", "(100%)"
};
static const int g_EasterEggValues[] = {
    0, 1, 10, 25, 50, 100
};

// -------------------------------------------------------------
// INITIALIZATION & CACHE ORCHESTRATION
// -------------------------------------------------------------
void extAudioInit(void) {
    extern void extAudioSFXInit(void);
    extern void extAudioMusicInit(void);
    extern void extAudioVoxInit(void);

    extAudioSFXInit();
    extAudioMusicInit();
    extAudioVoxInit();
}

void extAudioResetCache(void) {
    extern void extAudioSFXReset(void);
    extern void extAudioMusicReset(void);
    extern void extAudioVoxReset(void);

    extAudioVoxReset();    
    extAudioSFXReset();
    extAudioMusicReset();
    sysLogPrintf(LOG_NOTE, "EXT-AUDIO: Audio Cache has been reset.");
}

// -------------------------------------------------------------
// TERMINAL MIXER DASHBOARD ORCHESTRATION
// -------------------------------------------------------------
static int g_DashTimer = 0;

void extAudioMonitorDashboard(void) {
    if (! g_ExtLogMusicEnabled) return; // Silent terminal mode
    g_DashTimer++;
    if (g_DashTimer < 60) return;
    g_DashTimer = 0;

    // Call the HLE music file's native console-logger 
    extern void extAudioPrintMusicVoices(const char* prefix);
    extAudioPrintMusicVoices(g_ExtAudioEnabled ? "EXT-AUDIO" : "N64-AUDIO");
}

// -------------------------------------------------------------
// MASTER SDL AUDIO MIXING INTERCEPT
// -------------------------------------------------------------
// NEW: Track the crossfade multiplier (1.0 = Fully External, 0.0 = Muted)
static float g_ExtCrossfadeVol = 1.0f;

#define N64_NATIVE_RATE    22020.0
#define MASTER_OUTPUT_RATE 48000.0
// #define TARGET_QUEUE_SAMPLES 2400 // ~50ms audio cushion

static SDL_AudioStream *s_N64AudioStream = NULL;

static inline float getStreamSampleL(const s16 *n64_buf, u32 in_frames, s32 idx, const s16 tailL[2]) {
    if (idx <= -2) return (float)tailL[0];
    if (idx == -1) return (float)tailL[1];
    if (idx < (s32)in_frames) return (float)n64_buf[idx * 2 + 0];
    return (float)n64_buf[(in_frames - 1) * 2 + 0];
}

static inline float getStreamSampleR(const s16 *n64_buf, u32 in_frames, s32 idx, const s16 tailR[2]) {
    if (idx <= -2) return (float)tailR[0];
    if (idx == -1) return (float)tailR[1];
    if (idx < (s32)in_frames) return (float)n64_buf[idx * 2 + 1];
    return (float)n64_buf[(in_frames - 1) * 2 + 1];
}

s16* extAudioProcessSDL(const s16 *n64_buf, u32 num_bytes, u32 *out_bytes) {
    u32 in_samples = num_bytes / 2;
    u32 in_frames = in_samples / 2; // Stereo = 4 bytes per frame

    if (in_frames == 0) {
        *out_bytes = 0;
        return g_MixBuffer;
    }

    // 1. Exact fractional output frame count matching N64 -> 48kHz ratio
    static double s_FrameAccum = 0.0;
    double ideal_out_frames = ((double)in_frames * (MASTER_OUTPUT_RATE / N64_NATIVE_RATE)) + s_FrameAccum;
    u32 out_frames = (u32)ideal_out_frames;
    s_FrameAccum = ideal_out_frames - (double)out_frames;

    if (out_frames * 2 > ARRAYCOUNT(g_MixBuffer)) {
        out_frames = ARRAYCOUNT(g_MixBuffer) / 2;
    }
    *out_bytes = out_frames * 4;

    // 2. 1-sample lookbehind phase (-1.0) with 2-sample tail history
    // Guarantees idx is in [-2, in_frames-2] and next_idx is in [-1, in_frames-1]
    static double s_Phase = -1.0;
    static s16 s_TailL[2] = {0, 0};
    static s16 s_TailR[2] = {0, 0};

    const double step = N64_NATIVE_RATE / MASTER_OUTPUT_RATE; // ~0.45875

    for (u32 i = 0; i < out_frames; i++) {
        s32 idx = (s32)floor(s_Phase);
        float frac = (float)(s_Phase - (double)idx);
        s32 next_idx = idx + 1;

        float l1 = getStreamSampleL(n64_buf, in_frames, idx, s_TailL);
        float l2 = getStreamSampleL(n64_buf, in_frames, next_idx, s_TailL);
        float r1 = getStreamSampleR(n64_buf, in_frames, idx, s_TailR);
        float r2 = getStreamSampleR(n64_buf, in_frames, next_idx, s_TailR);

        g_MixBuffer[i * 2 + 0] = (s16)roundf(l1 + (l2 - l1) * frac);
        g_MixBuffer[i * 2 + 1] = (s16)roundf(r1 + (r2 - r1) * frac);

        s_Phase += step;
    }

    // Save the last 2 stereo samples of this frame for seamless bridging into the next frame
    if (in_frames >= 2) {
        s_TailL[0] = n64_buf[(in_frames - 2) * 2 + 0];
        s_TailR[0] = n64_buf[(in_frames - 2) * 2 + 1];
        s_TailL[1] = n64_buf[(in_frames - 1) * 2 + 0];
        s_TailR[1] = n64_buf[(in_frames - 1) * 2 + 1];
    } else {
        s_TailL[0] = s_TailL[1];
        s_TailR[0] = s_TailR[1];
        s_TailL[1] = n64_buf[0];
        s_TailR[1] = n64_buf[1];
    }

    s_Phase -= (double)in_frames;

    if (g_ExtAudioEnabled) {
        extern void extAudioMonitorDashboard(void);
        extAudioMonitorDashboard();
    }

    // 3. Smooth crossfade logic
    if (g_ExtAudioEnabled) {
        if (g_ExtCrossfadeVol < 1.0f) {
            g_ExtCrossfadeVol += 0.02f;
            if (g_ExtCrossfadeVol > 1.0f) g_ExtCrossfadeVol = 1.0f;
        }
    } else {
        if (g_ExtCrossfadeVol > 0.0f) {
            g_ExtCrossfadeVol -= 0.02f;
            if (g_ExtCrossfadeVol < 0.0f) g_ExtCrossfadeVol = 0.0f;
        }
    }

    // 4. Render Modern HD Audio directly at 48,000 Hz
    if (g_ExtCrossfadeVol > 0.0f && out_frames > 0) {
        extern void extAudioMixSFX(s16 *mix_buffer, u32 num_frames, float crossfade_vol);
        extAudioMixSFX(g_MixBuffer, out_frames, g_ExtCrossfadeVol);

        extern void extAudioMixMusic(s16 *mix_buffer, u32 num_frames, float crossfade_vol);
        extAudioMixMusic(g_MixBuffer, out_frames, g_ExtCrossfadeVol);

        extern void extAudioMixVox(s16 *mix_buffer, u32 num_frames, float crossfade_vol);
        extAudioMixVox(g_MixBuffer, out_frames, g_ExtCrossfadeVol);
    }

    return g_MixBuffer;
}

// -------------------------------------------------------------
// TEXT-STACKED HUD MIXER DASHBOARD
// -------------------------------------------------------------
void extAudioGetHudMixerData(struct HudMixerData *data) {
if (!g_ExtAudioEnabled || ! g_ExtLogMusicEnabled) {
    if (!g_ExtAudioEnabled) {
        snprintf(data->line1, sizeof(data->line1), "EXT-AUDIO: DISABLED");
        data->line2[0] = '\0';
        data->line3[0] = '\0';
        data->line4[0] = '\0';
        return;
    }
}

    extern int extAudioGetActiveMusicVoices(int *vols, int *pans, int *fxs, int *percs, int *loops);
    int inst_vols[126] = {0};
    int inst_pans[126] = {0};
    int inst_fx[126] = {0};
    int inst_is_perc[126] = {0};
    int inst_has_loop[126] = {0};

    int active_voices = extAudioGetActiveMusicVoices(inst_vols, inst_pans, inst_fx, inst_is_perc, inst_has_loop);

    float master_sfx_vol = (float)g_SfxVolume / 20480.0f;
    if (master_sfx_vol > 1.0f) master_sfx_vol = 1.0f;
    snprintf(data->line1, sizeof(data->line1), "MIDI VOICES: %02d/%d  |  SFX VOL: %d%%", 
             active_voices, 96, (int)(master_sfx_vol * 100.0f));

    char col_inst[32], col_vol[32], col_fx[32];
    data->line2[0] = '\0';
    data->line3[0] = '\0';
    data->line4[0] = '\0';
    int printed = 0;

    for (int i = 0; i < 126; i++) {
        if (inst_vols[i] > 0) {
            int vol_pct = (int)((float)inst_vols[i] / 327.67f);
            if (vol_pct > 99) vol_pct = 99;

            char pan_str[8];
            int raw_pan = inst_pans[i];
            if (raw_pan < 48) {
                snprintf(pan_str, sizeof(pan_str), "L%d", (64 - raw_pan));
            } else if (raw_pan > 80) {
                snprintf(pan_str, sizeof(pan_str), "R%d", (raw_pan - 64));
            } else {
                snprintf(pan_str, sizeof(pan_str), "C");
            }

            snprintf(col_inst, sizeof(col_inst), "%s[%03d]    ", inst_is_perc[i] ? "Perc" : "Inst", i);
            snprintf(col_vol, sizeof(col_vol), "V%02d:%-3s    ", vol_pct, pan_str);
            snprintf(col_fx, sizeof(col_fx), "%s/F%02d     ", inst_has_loop[i] ? "LOOP" : "1SHT", inst_fx[i]);

            if (strlen(data->line2) + strlen(col_inst) < 128) {
                strcat(data->line2, col_inst);
                strcat(data->line3, col_vol);
                strcat(data->line4, col_fx);
                printed++;
                if (printed >= 5) break; 
            }
        }
    }

    if (printed == 0) {
        snprintf(data->line2, sizeof(data->line2), "Active Tracks: 0");
    }
}

// -------------------------------------------------------------
// EXTENDED AUDIO OPTIONS MENU DEFINITIONS & MIXING BOARD
// -------------------------------------------------------------
static s32 g_MusicPlayerSelectedTrack = 0;

extern s32 g_MusicDisableMpDeath;
extern void musicPlayTrackIsolated(s32 tracknum);

// The Global Stem Matrix (All enabled by default)
int g_ExtMusicCategoryEnabled[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static MenuItemHandlerResult menuhandlerExtAudioEnabled(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GET) return extAudioGetEnabled();
    if (op == MENUOP_SET) extAudioSetEnabled(d->checkbox.value);
    return 0;
}

static MenuItemHandlerResult menuhandlerDisableMpDeathMusic(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GET) return g_MusicDisableMpDeath;
    if (op == MENUOP_SET) g_MusicDisableMpDeath = d->checkbox.value;
    return 0;
}

static MenuItemHandlerResult menuhandlerLogSfx(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GET) return g_ExtLogSfxEnabled;
    if (op == MENUOP_SET) g_ExtLogSfxEnabled = d->checkbox.value;
    return 0;
}

static MenuItemHandlerResult menuhandlerLogMusic(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GET) return g_ExtLogMusicEnabled;
    if (op == MENUOP_SET) g_ExtLogMusicEnabled = d->checkbox.value;
    return 0;
}

static MenuItemHandlerResult menuhandlerLogVox(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GET) return g_ExtLogVoxEnabled;
    if (op == MENUOP_SET) g_ExtLogVoxEnabled = d->checkbox.value;
    return 0;
}

static MenuItemHandlerResult menuhandlerVoxVolume(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GETOPTIONCOUNT) {
        d->dropdown.value = ARRAYCOUNT(g_VoxVolNames);
    }
    else if (op == MENUOP_GETOPTIONTEXT) {
        return (intptr_t)g_VoxVolNames[d->dropdown.value];
    }
    else if (op == MENUOP_SET) {
        g_ExtVoxVolumeScale = g_VoxVolValues[d->dropdown.value];
    }
    else if (op == MENUOP_GETSELECTEDINDEX) {
        int idx = 5; // Default 50%
        for (int i = 0; i < ARRAYCOUNT(g_VoxVolValues); i++) {
            if (fabsf(g_ExtVoxVolumeScale - g_VoxVolValues[i]) < 0.01f) {
                idx = i;
                break;
            }
        }
        d->dropdown.value = idx;
    }
    return 0;
}

static MenuItemHandlerResult menuhandlerEasterEggChance(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GETOPTIONCOUNT) {
        d->dropdown.value = ARRAYCOUNT(g_EasterEggNames);
    }
    else if (op == MENUOP_GETOPTIONTEXT) {
        return (intptr_t)g_EasterEggNames[d->dropdown.value];
    }
    else if (op == MENUOP_SET) {
        g_ExtEasterEggChance = g_EasterEggValues[d->dropdown.value];
    }
    else if (op == MENUOP_GETSELECTEDINDEX) {
        int idx = 1; // Default 1%
        for (int i = 0; i < ARRAYCOUNT(g_EasterEggValues); i++) {
            if (g_ExtEasterEggChance == g_EasterEggValues[i]) {
                idx = i;
                break;
            }
        }
        d->dropdown.value = idx;
    }
    return 0;
}

static MenuItemHandlerResult menuhandlerMusicPlayerTrack(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_GETOPTIONCOUNT) d->dropdown.value = ARRAYCOUNT(g_MusicTrackNames);
    else if (op == MENUOP_GETOPTIONTEXT) return (intptr_t)g_MusicTrackNames[d->dropdown.value];
    else if (op == MENUOP_SET) g_MusicPlayerSelectedTrack = d->dropdown.value;
    else if (op == MENUOP_GETSELECTEDINDEX) d->dropdown.value = g_MusicPlayerSelectedTrack;
    return 0;
}

static MenuItemHandlerResult menuhandlerMusicPlayerPlay(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_SET) musicPlayTrackIsolated(g_MusicTrackIDs[g_MusicPlayerSelectedTrack]);
    return 0;
}

static MenuItemHandlerResult menuhandlerMusicPlayerStop(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_SET) musicPlayTrackIsolated(MUSIC_NONE);
    return 0;
}

// Macro to quickly generate handlers for all 11 checkboxes
#define MAKE_CAT_HANDLER(name, idx) \
static MenuItemHandlerResult name(s32 op, struct menuitem *i, union handlerdata *d) { \
    if (op == MENUOP_GET) return g_ExtMusicCategoryEnabled[idx]; \
    if (op == MENUOP_SET) g_ExtMusicCategoryEnabled[idx] = d->checkbox.value; \
    return 0; \
}

MAKE_CAT_HANDLER(menuCatAmbience, CAT_AMBIENCE)
MAKE_CAT_HANDLER(menuCatSFX, CAT_SFX)
MAKE_CAT_HANDLER(menuCatVOX, CAT_VOX)
MAKE_CAT_HANDLER(menuCatPerc, CAT_PERCUSSION)
MAKE_CAT_HANDLER(menuCatSynths, CAT_SYNTHS)
MAKE_CAT_HANDLER(menuCatPiano, CAT_PIANO)
MAKE_CAT_HANDLER(menuCatStrings, CAT_STRINGS)
MAKE_CAT_HANDLER(menuCatHorns, CAT_HORNS)
MAKE_CAT_HANDLER(menuCatGuitar, CAT_GUITAR)
MAKE_CAT_HANDLER(menuCatBass, CAT_BASS)
MAKE_CAT_HANDLER(menuCatBeep, CAT_BEEP)
MAKE_CAT_HANDLER(menuCatUnknown, CAT_UNKNOWN)

static MenuItemHandlerResult menuhandlerEnableAllStems(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_SET) {
        for (int i = 0; i < ARRAYCOUNT(g_ExtMusicCategoryEnabled); i++) {
            g_ExtMusicCategoryEnabled[i] = 1;
        }
    }
    return 0;
}

static MenuItemHandlerResult menuhandlerDisableAllStems(s32 op, struct menuitem *item, union handlerdata *d) {
    if (op == MENUOP_SET) {
        for (int i = 0; i < ARRAYCOUNT(g_ExtMusicCategoryEnabled); i++) {
            g_ExtMusicCategoryEnabled[i] = 0;
        }
    }
    return 0;
}

struct menuitem g_ExtendedAudioMenuItems[] = {
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Enable External Audio", 0, menuhandlerExtAudioEnabled },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Disable MP Death Music", 0, menuhandlerDisableMpDeathMusic },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    { MENUITEMTYPE_DROPDOWN, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"VOX Volume", 0, menuhandlerVoxVolume },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    { MENUITEMTYPE_LABEL, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Music Player\n", 0, NULL },
    { MENUITEMTYPE_DROPDOWN, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Track", 0, menuhandlerMusicPlayerTrack },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Play Track\n", 0, menuhandlerMusicPlayerPlay },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Stop Track\n", 0, menuhandlerMusicPlayerStop },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    
    // INSTRUMENT MIXING BOARD
    { MENUITEMTYPE_LABEL, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Instrument Mixer\n", 0, NULL },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Percussion", 0, menuCatPerc },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Bass", 0, menuCatBass },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"SFX", 0, menuCatSFX },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"VOX", 0, menuCatVOX },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Strings", 0, menuCatStrings },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Synths", 0, menuCatSynths },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Pulses", 0, menuCatBeep },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Piano", 0, menuCatPiano },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Horns", 0, menuCatHorns },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Guitar", 0, menuCatGuitar },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Ambience", 0, menuCatAmbience },
    // { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Uncategorized\n", 0, menuCatUnknown },
    // { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Enable All Instruments\n", 0, menuhandlerEnableAllStems },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Disable All Instruments\n", 0, menuhandlerDisableAllStems },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    { MENUITEMTYPE_LABEL, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Debug Options\n", 0, NULL },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Log SFX to Terminal", 0, menuhandlerLogSfx },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Log Music to Terminal", 0, menuhandlerLogMusic },	
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Log VOX to Terminal", 0, menuhandlerLogVox },
    { MENUITEMTYPE_DROPDOWN, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Easter Egg Chance", 0, menuhandlerEasterEggChance },	
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_SELECTABLE_CLOSESDIALOG, L_OPTIONS_213, 0, NULL },
    { MENUITEMTYPE_END },
};

struct menudialogdef g_ExtendedAudioMenuDialog = {
    MENUDIALOGTYPE_DEFAULT,
    (uintptr_t)"Extended Audio Options",
    g_ExtendedAudioMenuItems,
    NULL,
    MENUDIALOGFLAG_LITERAL_TEXT,
    NULL,
};

// Robust WAV chunk walker (replaces buggy linear scans)
int extAudioParseWavHeader(const u8* fileData, u32 fileSize, u32* out_sample_rate, u32* out_channels, u32* out_data_offset, u32* out_data_size) {
    if (fileSize < 44) return 0;
    if (memcmp(fileData, "RIFF", 4) != 0 || memcmp(fileData + 8, "WAVE", 4) != 0) return 0;

    u32 offset = 12;
    u32 found_fmt = 0;
    u32 found_data = 0;

    while (offset + 8 < fileSize) {
        char chunk_id[4];
        memcpy(chunk_id, fileData + offset, 4);
        u32 chunk_size = fileData[offset+4] | (fileData[offset+5]<<8) | (fileData[offset+6]<<16) | (fileData[offset+7]<<24);
        
        if (offset + 8 + chunk_size > fileSize) break; 

        if (memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            *out_sample_rate = fileData[offset + 8 + 4] | (fileData[offset + 8 + 5] << 8) | (fileData[offset + 8 + 6] << 16) | (fileData[offset + 8 + 7] << 24);
            // EXTRACT CHANNELS (Offset 2 of the fmt chunk)
            *out_channels = fileData[offset + 8 + 2] | (fileData[offset + 8 + 3] << 8);
            found_fmt = 1;
        }
        else if (memcmp(chunk_id, "data", 4) == 0) {
            *out_data_offset = offset + 8;
            *out_data_size = chunk_size;
            found_data = 1;
        }

        offset += 8 + chunk_size;
        if (chunk_size & 1) offset++; 
    }

    return found_fmt && found_data;
}

// Loads 8-bit, 16-bit, 24-bit, or 32-bit (Int/Float) WAV data and returns a newly malloc'd 16-bit PCM buffer.
// Sets *out_data_size to the byte size of the returned 16-bit buffer (num_samples * 2).
s16* extAudioLoadWavPcm16(const u8* fileData, u32 fileSize, u32* out_sample_rate, u32* out_channels, u32* out_data_size) {
    if (!fileData || fileSize < 44) return NULL;
    if (memcmp(fileData, "RIFF", 4) != 0 || memcmp(fileData + 8, "WAVE", 4) != 0) return NULL;

    u32 offset = 12;
    u32 found_fmt = 0;
    u32 found_data = 0;
    u16 audio_format = 1;
    u16 bits_per_sample = 16;
    u32 data_offset = 0;
    u32 raw_data_size = 0;

    while (offset + 8 <= fileSize) {
        char chunk_id[4];
        memcpy(chunk_id, fileData + offset, 4);
        u32 chunk_size = (u32)fileData[offset+4] | ((u32)fileData[offset+5]<<8) | ((u32)fileData[offset+6]<<16) | ((u32)fileData[offset+7]<<24);
        
        if (offset + 8 + chunk_size > fileSize) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            const u8 *fmt = fileData + offset + 8;
            audio_format = (u16)(fmt[0] | (fmt[1] << 8));
            *out_channels = (u32)(fmt[2] | (fmt[3] << 8));
            *out_sample_rate = (u32)fmt[4] | ((u32)fmt[5] << 8) | ((u32)fmt[6] << 16) | ((u32)fmt[7] << 24);
            bits_per_sample = (u16)(fmt[14] | (fmt[15] << 8));

            // Handle WAVE_FORMAT_EXTENSIBLE (0xFFFE) used by many DAWs for 24-bit/32-bit exports
            if (audio_format == 0xFFFE && chunk_size >= 40) {
                audio_format = (u16)(fmt[24] | (fmt[25] << 8)); // SubFormat GUID first 16 bits (1 = PCM, 3 = Float)
            }
            found_fmt = 1;
        }
        else if (memcmp(chunk_id, "data", 4) == 0) {
            data_offset = offset + 8;
            raw_data_size = chunk_size;
            found_data = 1;
        }

        offset += 8 + chunk_size;
        if (chunk_size & 1) offset++;
    }

    if (!found_fmt || !found_data || raw_data_size == 0 || *out_channels == 0) return NULL;

    const u8 *src = fileData + data_offset;

    // 1. Standard 16-bit PCM (Fast Path)
    if (audio_format == 1 && bits_per_sample == 16) {
        u32 num_samples = raw_data_size / 2;
        u32 out_bytes = num_samples * sizeof(s16);
        s16 *pcm16 = (s16*)malloc(out_bytes);
        if (!pcm16) return NULL;
        memcpy(pcm16, src, out_bytes);
        *out_data_size = out_bytes;
        return pcm16;
    }

    // 2. 24-bit PCM (3 bytes per sample, signed little-endian)
    if (audio_format == 1 && bits_per_sample == 24) {
        u32 num_samples = raw_data_size / 3;
        u32 out_bytes = num_samples * sizeof(s16);
        s16 *pcm16 = (s16*)malloc(out_bytes);
        if (!pcm16) return NULL;

        for (u32 i = 0; i < num_samples; i++) {
            u32 b_idx = i * 3;
            // Reconstruct signed 24-bit integer and round to 16-bit
            s32 val24 = (s32)src[b_idx] | ((s32)src[b_idx + 1] << 8) | ((s32)((s8)src[b_idx + 2]) << 16);
            s32 val16 = (val24 + 128) >> 8;
            if (val16 > 32767) val16 = 32767;
            if (val16 < -32768) val16 = -32768;
            pcm16[i] = (s16)val16;
        }

        *out_data_size = out_bytes;
        return pcm16;
    }

    // 3. 32-bit Integer PCM (4 bytes per sample)
    if (audio_format == 1 && bits_per_sample == 32) {
        u32 num_samples = raw_data_size / 4;
        u32 out_bytes = num_samples * sizeof(s16);
        s16 *pcm16 = (s16*)malloc(out_bytes);
        if (!pcm16) return NULL;

        const s32 *src32 = (const s32*)src;
        for (u32 i = 0; i < num_samples; i++) {
            s32 val16 = (src32[i] >> 16);
            pcm16[i] = (s16)val16;
        }

        *out_data_size = out_bytes;
        return pcm16;
    }

    // 4. 32-bit IEEE Float (audio_format == 3)
    if (audio_format == 3 && bits_per_sample == 32) {
        u32 num_samples = raw_data_size / 4;
        u32 out_bytes = num_samples * sizeof(s16);
        s16 *pcm16 = (s16*)malloc(out_bytes);
        if (!pcm16) return NULL;

        for (u32 i = 0; i < num_samples; i++) {
            float fval;
            memcpy(&fval, src + (i * 4), sizeof(float));
            s32 val16 = (s32)roundf(fval * 32767.0f);
            if (val16 > 32767) val16 = 32767;
            if (val16 < -32768) val16 = -32768;
            pcm16[i] = (s16)val16;
        }

        *out_data_size = out_bytes;
        return pcm16;
    }

    // 5. 8-bit Unsigned PCM
    if (audio_format == 1 && bits_per_sample == 8) {
        u32 num_samples = raw_data_size;
        u32 out_bytes = num_samples * sizeof(s16);
        s16 *pcm16 = (s16*)malloc(out_bytes);
        if (!pcm16) return NULL;

        for (u32 i = 0; i < num_samples; i++) {
            pcm16[i] = (s16)(((s32)src[i] - 128) << 8);
        }

        *out_data_size = out_bytes;
        return pcm16;
    }

    sysLogPrintf(LOG_WARNING, "EXT-AUDIO: Unsupported WAV format (tag=%u, bits=%u)", audio_format, bits_per_sample);
    return NULL;
}

// Round calculated rates to native N64/XBLA rates to handle modern DAW padding
u32 extAudioRoundToNearestStandardRate(u32 rate) {
    u32 standard_rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000 };
    u32 nearest = 22050;
    u32 min_diff = 999999;
    for (int i = 0; i < 7; i++) {
        u32 diff = (rate > standard_rates[i]) ? (rate - standard_rates[i]) : (standard_rates[i] - rate);
        if (diff < min_diff) {
            min_diff = diff;
            nearest = standard_rates[i];
        }
    }
    return nearest;
}

void extAudioNormalizePcm16(s16 *pcm16, u32 data_size_bytes, float target_level, int attenuate_only) {
    if (!pcm16 || data_size_bytes < 2 || target_level <= 0.0f) return;

    u32 num_samples = data_size_bytes / sizeof(s16);
    s32 max_peak = 0;

    // 1. Scan for the highest absolute peak in the sample
    for (u32 i = 0; i < num_samples; i++) {
        s32 val = (s32)pcm16[i];
        if (val < 0) val = -val;
        if (val > max_peak) max_peak = val;
    }

    // Ignore digital silence or near-zero noise floors
    if (max_peak < 64) return;

    float target_peak = target_level * 32767.0f;

    // In Attenuate-Only mode, leave samples alone if they are already quieter than the target ceiling
    if (attenuate_only && (float)max_peak <= target_peak) {
        return;
    }

    float scale = target_peak / (float)max_peak;

    // 2. Apply pure linear gain scaling across the buffer
    for (u32 i = 0; i < num_samples; i++) {
        s32 scaled = (s32)roundf((float)pcm16[i] * scale);
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;
        pcm16[i] = (s16)scaled;
    }
}