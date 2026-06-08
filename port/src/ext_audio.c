#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fs.h"
#include "system.h"
#include "platform.h"
#include "game/menu.h"
#include "types.h"

#include "ext_audio.h"
#include "ext_audio_tracks.h"
#include "ext_music_categories.h"

extern u16 g_SfxVolume;

// -------------------------------------------------------------
// GLOBAL SETTINGS & TOGGLE STATE
// -------------------------------------------------------------
int g_ExtAudioEnabled = 1;
int g_ExtLogSfxEnabled = 1;
int g_ExtLogMusicEnabled = 1;

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
    "Disabled (0%)", "Ultra Rare (1%)", "Rare (5%)", "Occasional (15%)", "Common (33%)", "Chaos (100%)"
};
static const int g_EasterEggValues[] = {
    0, 1, 5, 15, 33, 100
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

s16* extAudioProcessSDL(const s16 *n64_buf, u32 num_bytes) {
    if (num_bytes > sizeof(g_MixBuffer)) {
        sysLogPrintf(LOG_WARNING, "EXT-AUDIO: Segfault prevented! Buffer spike: %u bytes.", num_bytes);
        return (s16*)n64_buf; 
    }

    if (g_ExtAudioEnabled) {
        extern void extAudioMonitorDashboard(void);
        extAudioMonitorDashboard();
    }

    memcpy(g_MixBuffer, n64_buf, num_bytes);
    
    // --- CROSSFADE LOGIC ---
    if (g_ExtAudioEnabled) {
        if (g_ExtCrossfadeVol < 1.0f) {
            g_ExtCrossfadeVol += 0.02f; // ~1-second fade IN
            if (g_ExtCrossfadeVol > 1.0f) g_ExtCrossfadeVol = 1.0f;
        }
    } else {
        if (g_ExtCrossfadeVol > 0.0f) {
            g_ExtCrossfadeVol -= 0.02f; // ~1-second fade OUT
            if (g_ExtCrossfadeVol < 0.0f) g_ExtCrossfadeVol = 0.0f;
        }
    }

    // Only run the External Mixer if it's audible during the fade!
    if (g_ExtCrossfadeVol > 0.0f) {
        u32 num_samples = num_bytes / 2; 
        u32 num_frames = num_samples / 2; 

        extern void extAudioMixSFX(s16 *mix_buffer, u32 num_frames, float crossfade_vol);
        extAudioMixSFX(g_MixBuffer, num_frames, g_ExtCrossfadeVol);

        extern void extAudioMixMusic(s16 *mix_buffer, u32 num_frames, float crossfade_vol);
        extAudioMixMusic(g_MixBuffer, num_frames, g_ExtCrossfadeVol);

	extern void extAudioMixVox(s16 *mix_buffer, u32 num_frames, float crossfade_vol);
        extAudioMixVox(g_MixBuffer, num_frames, g_ExtCrossfadeVol);
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

struct menuitem g_ExtendedAudioMenuItems[] = {
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Enable External Audio", 0, menuhandlerExtAudioEnabled },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Log SFX to Terminal", 0, menuhandlerLogSfx },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Log Music to Terminal", 0, menuhandlerLogMusic },	
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Disable MP Death Music", 0, menuhandlerDisableMpDeathMusic },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
// NEW: Inject Dialogue Volume & Easter Egg Chance controls!
    { MENUITEMTYPE_DROPDOWN, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Dialogue Vol (VOX)", 0, menuhandlerVoxVolume },
    { MENUITEMTYPE_DROPDOWN, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Easter Egg Chance", 0, menuhandlerEasterEggChance },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    { MENUITEMTYPE_LABEL, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Sound Test Music Player\n", 0, NULL },
    { MENUITEMTYPE_DROPDOWN, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Track", 0, menuhandlerMusicPlayerTrack },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Play Selected Track\n", 0, menuhandlerMusicPlayerPlay },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Stop Music\n", 0, menuhandlerMusicPlayerStop },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    
    // THE INSTRUMENT MIXING BOARD
    { MENUITEMTYPE_LABEL, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Instrument Mixer (Stems)\n", 0, NULL },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Ambience", 0, menuCatAmbience },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"SFX", 0, menuCatSFX },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"VOX", 0, menuCatVOX },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Percussion", 0, menuCatPerc },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Synths", 0, menuCatSynths },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Piano", 0, menuCatPiano },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Strings", 0, menuCatStrings },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Horns", 0, menuCatHorns },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Guitar", 0, menuCatGuitar },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Bass", 0, menuCatBass },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Beeps", 0, menuCatBeep },
    { MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Uncategorized\n", 0, menuCatUnknown },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
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