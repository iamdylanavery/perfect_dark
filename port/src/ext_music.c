#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fs.h"
#include "system.h"
#include "platform.h"
#include "ext_audio.h"
#include "ext_music_categories.h"
#include "ext_seq_rates.h"

extern int g_ExtAudioEnabled;
extern u16 g_SfxVolume;  

// Circular Reverb / Echo settings
#define MODERN_MIX_RATE 48000
// 5000 N64 samples @ 22050Hz = ~226.7ms. We calculate that time dynamically for the modern rate!
#define FX_DELAY_LENGTH ((u32)(MODERN_MIX_RATE * 0.226757f))
#define FX_FEEDBACK_COEFF 0.45f  // Increased to ring out longer
#define FX_WET_MIX 0.50f    

// NEW: Dark Reverb EQ (Lowpass Filter) States
static float g_FxLpfL = 0.0f;
static float g_FxLpfR = 0.0f;  

// -------------------------------------------------------------
// NEW: DEBUG MIXER & VOLUME CONTROLS
// -------------------------------------------------------------
#define MUSIC_MASTER_BOOST 1.0f 

// The Category Map based exactly on your spreadsheet
InstrumentCategory extAudioGetInstCategory(int inst_idx) {
    switch (inst_idx) {
        case 107: case 108: case 124: case 125:
            return CAT_AMBIENCE;
            
        case 2: case 4: case 26: case 27: case 29: case 30: case 33:
        case 34: case 35: case 38: case 42: case 45: case 47: case 49: case 51:
        case 59: case 62: case 71: case 72: case 78: case 79:
//        case 84: case 85: 
        case 101: case 104: case 105: case 106: case 110: case 114: case 119:
        case 120: case 121: case 123:
            return CAT_SFX;
            
        case 6: case 20: case 74: case 118:
            return CAT_VOX;
            
        case 0: case 1: case 3: case 5: case 8: case 15: case 18: case 19:
        case 21: case 24: case 31: case 37: case 41: case 43: case 46: case 57:
        case 60: case 68: case 73: case 75: case 83: case 113: case 116:
            return CAT_PERCUSSION;
            
        case 9: case 10: case 12: case 13: case 14: case 16: case 50: case 52:
        case 55: case 80: case 81:
            return CAT_SYNTHS;
            
        case 11: case 44: case 63: case 64: case 70:
            return CAT_PIANO;
            
        case 17: case 32:
            return CAT_STRINGS;
            
        case 22: case 23:
            return CAT_HORNS;
            
        case 28:
            return CAT_GUITAR;
            
	case 84: case 85: 
	case 86: case 87: case 88: case 89: case 90: case 91:
        case 92: case 93: case 94: case 95: case 96: case 97: case 98: case 99:
	    return CAT_BASS;
	
        case 25: case 36: case 39: case 40: case 48: case 53: case 54: case 56:
        case 58: case 61: case 65: case 66: case 67: case 69: case 76: case 77:
        case 82: case 100: case 102: case 103: case 109: case 111: case 112:
        case 115: case 117: case 122: case 7: 
            return CAT_BEEP;
            
        default:
            return CAT_UNKNOWN;
    }
}   

// -------------------------------------------------------------
// MUSIC INSTRUMENTS MODULE
// -------------------------------------------------------------
#define MAX_HQ_MUSIC 4000
struct HqMusic {
    uintptr_t n64_base_addr; 
    u8* pcm_data;
    u32 pcm_size;
    u32 sample_rate;            // NEW: Parsed WAV sample rate (e.g. 44100)
    u32 original_sample_rate;
    u32 channels;               // NEW: Track Mono/Stereo
    int is_raw16; 
    u32 loop_start;
    u32 loop_end;
    int has_loop;
    int inst_idx;
    int sound_idx;
    int is_perc;
};
static struct HqMusic g_HqMusic[MAX_HQ_MUSIC];
static u32 g_NumHqMusic = 0;
// Note: g_HqTokenAllocator and g_HqFakeOffsets have been deleted!

#define MAX_MUSIC_VOICES 96 
struct MusicVoice {
    void *n64_voice;   
    struct HqMusic *snd;
    float cursor;
    
    // Smooth Envelope Ramping
    float current_vol;
    float target_vol;
    float vol_step;


    // Pitch & Diagnostics
    float render_pitch;  // Snapshotted by SDL Thread
    
    // Smooth Pitch Ramping
    float current_pitch;
//    float target_pitch;
//    float pitch_step;
    
    float fx_mix;      
    int raw_pan;

    // Remove vol_render, pitch_render, fx_render, pan_render (we won't need snapshotting anymore)
    
    u32 loop_start;    
    u32 loop_end;
    int has_loop;
    int active;
};
static struct MusicVoice g_MusicVoices[MAX_MUSIC_VOICES];

// Reverb/Echo Circular Buffers and local accumulators
static float g_FxDelayBufferL[32768] = {0}; // Increased size to safely hold modern sample rates
static float g_FxDelayBufferR[32768] = {0};
static u32 g_FxWritePtr = 0;
static float g_FxSendL[8192] = {0};
static float g_FxSendR[8192] = {0};

void extAudioMusicInit(void) {
    g_NumHqMusic = 0;
    memset(g_HqMusic, 0, sizeof(g_HqMusic));
    memset(g_MusicVoices, 0, sizeof(g_MusicVoices));
    memset(g_FxDelayBufferL, 0, sizeof(g_FxDelayBufferL));
    memset(g_FxDelayBufferR, 0, sizeof(g_FxDelayBufferR));
    g_FxWritePtr = 0;
    g_FxLpfL = 0.0f;
    g_FxLpfR = 0.0f;
}

void extAudioMusicReset(void) {
    for (u32 i = 0; i < g_NumHqMusic; i++) {
        if (g_HqMusic[i].pcm_data) free(g_HqMusic[i].pcm_data);
    }
    g_NumHqMusic = 0;
    memset(g_HqMusic, 0, sizeof(g_HqMusic));
    memset(g_MusicVoices, 0, sizeof(g_MusicVoices));
    memset(g_FxDelayBufferL, 0, sizeof(g_FxDelayBufferL));
    memset(g_FxDelayBufferR, 0, sizeof(g_FxDelayBufferR));
    g_FxWritePtr = 0;
    g_FxLpfL = 0.0f;
    g_FxLpfR = 0.0f;
}

// Helper function to query the Python-generated master table
static u32 getOverrideOriginalRate(int is_perc, s16 inst_idx, s16 sound_idx) {
    for (int i = 0; g_RateOverrides[i].inst_idx != -1; i++) {
        if (g_RateOverrides[i].is_perc == is_perc &&
            g_RateOverrides[i].inst_idx == inst_idx && 
            g_RateOverrides[i].sound_idx == sound_idx) {
            return g_RateOverrides[i].original_rate;
        }
    }
    return 0; // Not found
}

// -------------------------------------------------------------
// SEQUENCE INSTRUMENT BANK PARSING (XBLA offset 16 structs)
// -------------------------------------------------------------
static void scanInstrument(ALInstrument *inst, int inst_idx, int is_percussion, const char* prefix) {
    if (!inst) return;

    for (s32 s = 0; s < inst->soundCount; s++) {
        ALSound *sound = inst->soundArray[s];
        if (!sound || !sound->wavetable) continue;
        ALWaveTable *w = sound->wavetable;

        char filepath[256];
        if (is_percussion) {
            snprintf(filepath, sizeof(filepath), "ext_seq/seq_perc_snd%02d.wav", s);
        } else {
            snprintf(filepath, sizeof(filepath), "ext_seq/seq_inst%03d_snd%02d.wav", inst_idx, s);
        }

        u32 fileSize = 0;
        u8 *fileData = fsFileLoad(filepath, &fileSize);
        
        if (!fileData && is_percussion) {
            snprintf(filepath, sizeof(filepath), "output_xbla_seq/seq_inst_perc_snd%02d.wav", s);
            fileData = fsFileLoad(filepath, &fileSize);
        }

        if (fileData && fileSize > 44) {
            u32 sample_rate = 22050;
            u32 channels = 1;
            u32 data_offset = 0;
            u32 data_size = 0;

            if (extAudioParseWavHeader(fileData, fileSize, &sample_rate, &channels, &data_offset, &data_size)) {
                u8 *pcmData = malloc(data_size);
                if (pcmData) {
                    memcpy(pcmData, fileData + data_offset, data_size);

                    u32 hq_id = g_NumHqMusic++;
                    g_HqMusic[hq_id].n64_base_addr = (uintptr_t)w->base;
                    g_HqMusic[hq_id].pcm_data = pcmData;
                    g_HqMusic[hq_id].channels = channels; 
                    g_HqMusic[hq_id].pcm_size = data_size;
                    g_HqMusic[hq_id].sample_rate = sample_rate;

                    // RESTORED: Your original sample rate guesser math (No double declarations!)
                    u32 original_samples = 0;
                    if (w->type == AL_ADPCM_WAVE) {
                        original_samples = (w->len / 9) * 16;
                    } else if (w->type == AL_RAW16_WAVE) {
                        original_samples = w->len / 2;
                    }

                    // 1. Calculate the raw ratio
                    u32 calculated_orig_rate = 22050; // Safe default
                    u32 wav_frames = (data_size / 2) / channels; // MOVED UP HERE!
                    u32 override_rate = getOverrideOriginalRate(is_percussion, inst_idx, s);

                    if (override_rate > 0) {
                        // 1. Trust the Python-generated table 100%!
                        calculated_orig_rate = override_rate;
                    } else {
                        // 2. Modder Fallback: If a custom WAV is injected, guess its rate via math
                        if (wav_frames > 0 && original_samples > 0) {
                            u32 ratio_rate = ((unsigned long long)original_samples * sample_rate) / wav_frames;
                            calculated_orig_rate = extAudioRoundToNearestStandardRate(ratio_rate);
                            
                            // Hard-clamp to prevent the 48000Hz transient blip bug for custom sounds
                            if (calculated_orig_rate > 32000) calculated_orig_rate = 22050;
                        }
                    }

                    g_HqMusic[hq_id].original_sample_rate = calculated_orig_rate;
                    g_HqMusic[hq_id].inst_idx = inst_idx;
                    g_HqMusic[hq_id].sound_idx = s;
                    g_HqMusic[hq_id].is_perc = is_percussion;

                    // Restored: Original loop calculation using the deduced calculated_orig_rate
                    g_HqMusic[hq_id].has_loop = 0;
                    ALADPCMloop *l = NULL;
                    if (w->type == AL_ADPCM_WAVE && w->waveInfo.adpcmWave.loop) {
                        l = (ALADPCMloop*)w->waveInfo.adpcmWave.loop;
                    } else if (w->type == AL_RAW16_WAVE && w->waveInfo.rawWave.loop) {
                        l = (ALADPCMloop*)w->waveInfo.rawWave.loop; 
                    }

                    if (l && l->count != 0) { 
                        g_HqMusic[hq_id].loop_start = ((unsigned long long)l->start * sample_rate) / calculated_orig_rate;
                        g_HqMusic[hq_id].loop_end = ((unsigned long long)l->end * sample_rate) / calculated_orig_rate;
                        
                        // Safety clamps to prevent out-of-bounds blips
                        if (g_HqMusic[hq_id].loop_end >= wav_frames) g_HqMusic[hq_id].loop_end = wav_frames - 1;
                        if (g_HqMusic[hq_id].loop_start >= g_HqMusic[hq_id].loop_end) g_HqMusic[hq_id].loop_start = 0;
                        g_HqMusic[hq_id].has_loop = 1;
                    }

                    if (g_ExtLogMusicEnabled) {
                        sysLogPrintf(LOG_NOTE, "EXT-MUS | [SEQ LOAD] | %s: %03d | Sound: %02d | Orig: %uHz | New: %uHz", 
                                     is_percussion ? "PERC" : "INST", inst_idx, s, calculated_orig_rate, sample_rate);
                    }
                }
            }
        }
        if (fileData) free(fileData);
    }
}

void extAudioScanSequenceBank(void *bankfile_ptr) {
    if (!bankfile_ptr) return;
    ALBankFile *bf = (ALBankFile*)bankfile_ptr;
    const char* prefix = g_ExtAudioEnabled ? "EXT-AUDIO" : "N64-AUDIO";

    for (s32 b = 0; b < bf->bankCount; b++) {
        ALBank *bank = bf->bankArray[b];
        if (!bank) continue;

        for (s32 i = 0; i < bank->instCount; i++) {
            scanInstrument(bank->instArray[i], i, 0, prefix);
        }

        if (bank->percussion) {
            scanInstrument(bank->percussion, 0, 1, prefix);
        }
    }
}

// Helper getter function called by ext_audio.c to compile the Stacked HUD 
int extAudioGetActiveMusicVoices(int *vols, int *pans, int *fxs, int *percs, int *loops) {
    int active_voices = 0;
    for (int m = 0; m < MAX_MUSIC_VOICES; m++) {
        if (g_MusicVoices[m].active && g_MusicVoices[m].current_vol > 0.0f) {
            active_voices++;
            int inst_idx = g_MusicVoices[m].snd->inst_idx;
            if (inst_idx >= 0 && inst_idx < 126) {
                if ((int)g_MusicVoices[m].current_vol > vols[inst_idx]) {
                    vols[inst_idx] = (int)g_MusicVoices[m].current_vol;
                    pans[inst_idx] = g_MusicVoices[m].raw_pan;
                    fxs[inst_idx] = (int)(g_MusicVoices[m].fx_mix * 100.0f);
                    percs[inst_idx] = g_MusicVoices[m].snd->is_perc;
                    loops[inst_idx] = g_MusicVoices[m].snd->has_loop;
                }
            }
        }
    }
    return active_voices;
}

// -------------------------------------------------------------
// MUSIC (HLE) SYNTHESIZER VOICE HOOKS
// -------------------------------------------------------------

static int g_NextVoiceAlloc = 0;

int extAudioSeqVoiceStart(void *n64_voice, ALWaveTable *w, float pitch, int vol, int pan, int fxmix, ALMicroTime t) {
    if (!g_ExtAudioEnabled || !n64_voice || !w) return 0;
    uintptr_t base_addr = (uintptr_t)w->base;

    struct HqMusic *snd = NULL;
    for (u32 i = 0; i < g_NumHqMusic; i++) {
        if (base_addr == g_HqMusic[i].n64_base_addr) {
            snd = &g_HqMusic[i]; 
            break;
        }
    }
    if (!snd) return 0;

// --- NEW: MICRO-LOOP NATIVE FALLBACK ---
    // If the sample is extremely short (e.g., < 2000 bytes, which is ~45ms at 22k mono),
    // refuse the HLE allocation. Returning 0 bypasses the stealth mute, allowing 
    // the N64 RSP to natively synthesize these tricky bass blips!
    if (snd->pcm_size < 2000) {
        return 0;
    }

    // CLEAN SLATE: Scrub ALL ghosts!
    for (int j = 0; j < MAX_MUSIC_VOICES; j++) {
        if (g_MusicVoices[j].active && g_MusicVoices[j].n64_voice == n64_voice) {
            g_MusicVoices[j].active = 0;
        }
    }

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (!g_MusicVoices[i].active) { 
            g_MusicVoices[i].n64_voice = n64_voice;
            g_MusicVoices[i].snd = snd;
            g_MusicVoices[i].cursor = 0.0f;
            
            g_MusicVoices[i].current_vol = (float)vol;
            g_MusicVoices[i].target_vol = (float)vol;
            g_MusicVoices[i].vol_step = 0.0f;
            
            g_MusicVoices[i].current_pitch = pitch;
            g_MusicVoices[i].render_pitch = pitch; 

            g_MusicVoices[i].raw_pan = pan;
            g_MusicVoices[i].fx_mix = (float)fxmix / 127.0f;
            g_MusicVoices[i].has_loop = snd->has_loop;
            g_MusicVoices[i].loop_start = snd->loop_start;
            g_MusicVoices[i].loop_end = snd->loop_end;
            g_MusicVoices[i].active = 1;
            return 1;
        }
    }
    return 0;
}

int extAudioSeqVoiceSetVol(void *n64_voice, int vol, ALMicroTime t) {
    if (!n64_voice) return 0;
    int found = 0;
    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            if (vol != -1) {
                g_MusicVoices[i].target_vol = (float)vol;
                if (t > 0) {
                    float frames = ((float)t / 1000000.0f) * 22050.0f; 
                    g_MusicVoices[i].vol_step = (g_MusicVoices[i].target_vol - g_MusicVoices[i].current_vol) / frames;
                } else {
                    g_MusicVoices[i].current_vol = (float)vol;
                    g_MusicVoices[i].vol_step = 0.0f;
                }
            }
            found = 1; // Mark found, but DO NOT return yet! Keep updating ghosts!
        }
    }
    return found;
}

int extAudioSeqVoiceSetPitch(void *n64_voice, float pitch) {
    if (!n64_voice) return 0;
    int found = 0;
    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            if (pitch > 0.0f) {
                g_MusicVoices[i].current_pitch = pitch;
            }
            found = 1; // Mark found, but DO NOT return yet! Keep updating ghosts!
        }
    }
    return found;
}

int extAudioSeqVoiceSetPan(void *n64_voice, int pan) {
    if (!n64_voice) return 0;
    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            if (pan != -1) g_MusicVoices[i].raw_pan = pan;
            return 1;
        }
    }
    return 0;
}

int extAudioSeqVoiceStop(void *n64_voice) {
    if (!n64_voice) return 0;
    int found = 0;
    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            g_MusicVoices[i].active = 0; 
            found = 1; // Mark found, but DO NOT return yet! Keep killing ghosts!
        }
    }
    return found;
}

// -------------------------------------------------------------
// TROJAN HORSE LEGACY STUBS & LOGGING
// -------------------------------------------------------------
void extAudioLoadWavetable(ALWaveTable *w, uintptr_t tbl_segment_start) { }

int extAudioDmaIntercept(uintptr_t offset, uintptr_t *out_ptr) {
    return 0;
}

int extAudioMixerPaint(uint16_t dest_addr, uintptr_t source_addr, uint16_t nbytes) {
    return 0;
}

int extAudioMixerDecode(int nbytes, uint16_t inofs, void* dest_buf) {
    return 0; 
}

// NEW: Helper function to get category string
static const char* extAudioGetCatName(InstrumentCategory cat) {
    switch (cat) {
        case CAT_AMBIENCE:   return "AMB ";
        case CAT_SFX:        return "SFX ";
        case CAT_VOX:        return "VOX ";
        case CAT_PERCUSSION: return "PERC";
        case CAT_SYNTHS:     return "SYNT";
        case CAT_PIANO:      return "PNO ";
        case CAT_STRINGS:    return "STR ";
        case CAT_HORNS:      return "HRN ";
        case CAT_GUITAR:     return "GTR ";
        case CAT_BASS:       return "BASS";
        case CAT_BEEP:       return "BEEP";
        default:             return "UNKN";
    }
}

// UPGRADED: Terminal Dashboard
void extAudioPrintMusicVoices(const char* prefix) {
    int audible_voices = 0;
    
    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].current_vol > 0.0f) {
            InstrumentCategory cat = extAudioGetInstCategory(g_MusicVoices[i].snd->inst_idx);
            // Only count voices that are enabled in your Stem Mixer UI
            if (g_ExtMusicCategoryEnabled[cat]) {
                audible_voices++;
            }
        }
    }

    if (audible_voices == 0) return; // Don't spam if everything is stopped/muted

    sysLogPrintf(LOG_NOTE, "================================================================================");
    sysLogPrintf(LOG_NOTE, "[ %s MIXER DASHBOARD ] - Audible Voices: %d", prefix, audible_voices);
    sysLogPrintf(LOG_NOTE, "--------------------------------------------------------------------------------");
    sysLogPrintf(LOG_NOTE, "V# | CAT  | INST | SND | N64 VOL | PAN | PITCH | FX_MIX | STATUS");
    sysLogPrintf(LOG_NOTE, "--------------------------------------------------------------------------------");

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        struct MusicVoice *v = &g_MusicVoices[i];
        if (v->active && v->current_vol > 0.0f) {
            InstrumentCategory cat = extAudioGetInstCategory(v->snd->inst_idx);
            
            // Skip printing muted stems so you can isolate your debug targets!
            if (!g_ExtMusicCategoryEnabled[cat]) continue;

            const char* cat_name = extAudioGetCatName(cat);
            const char* status = v->has_loop ? "(LOOP)" : "(1SHT)";
            
            sysLogPrintf(LOG_NOTE, "%02d | %s | %04d | %03d | %7d | %3d | %5.2f | %6.2f | %s",
                i, cat_name, v->snd->inst_idx, v->snd->sound_idx,
                (int)v->current_vol, v->raw_pan, v->current_pitch, v->fx_mix, status);
        }
    }
    sysLogPrintf(LOG_NOTE, "================================================================================\n");
}

// -------------------------------------------------------------
// SEQUENCE SYNTHESIZER MIXER (With Category Board & Linear Interpolation)
// -------------------------------------------------------------
void extAudioMixMusic(s16 *mix_buffer, u32 num_frames, float crossfade_vol) {
    u32 num_samples = num_frames * 2;
    memset(g_FxSendL, 0, num_frames * sizeof(float));
    memset(g_FxSendR, 0, num_frames * sizeof(float));

    // SNAPSHOT PITCH ONLY: Force memory read to prevent compiler register caching!
    for (int m = 0; m < MAX_MUSIC_VOICES; m++) {
        if (g_MusicVoices[m].active) {
            g_MusicVoices[m].render_pitch = g_MusicVoices[m].current_pitch;
        }
    }

    for (int m = 0; m < MAX_MUSIC_VOICES; m++) {
        if (!g_MusicVoices[m].active) continue;
        
        struct MusicVoice *voice = &g_MusicVoices[m];
        s16 *samples = (s16*)voice->snd->pcm_data;
        u32 total_frames = (voice->snd->pcm_size / 2) / voice->snd->channels;
        float cursor = voice->cursor;
        
        float base_step = (float)voice->snd->sample_rate / (float)voice->snd->original_sample_rate;
        
        // Use raw_pan directly
        float pan_norm = (float)(voice->raw_pan & 0x7F) / 127.0f;
        float pan_l_scalar = cosf(pan_norm * 1.570796327f) * MUSIC_MASTER_BOOST * crossfade_vol;
        float pan_r_scalar = sinf(pan_norm * 1.570796327f) * MUSIC_MASTER_BOOST * crossfade_vol;

        InstrumentCategory cat = extAudioGetInstCategory(voice->snd->inst_idx);
        if (!g_ExtMusicCategoryEnabled[cat]) {
            pan_l_scalar = 0.0f;
            pan_r_scalar = 0.0f;
        }

for (u32 i = 0; i < num_frames; i++) {
            
            // --- ENVELOPE RAMPING LOGIC (Volume only!) ---
            if (fabsf(voice->target_vol - voice->current_vol) > fabsf(voice->vol_step)) {
                voice->current_vol += voice->vol_step;
            } else {
                voice->current_vol = voice->target_vol;
            }

            // Calculate dynamic step using the snapshotted render_pitch
            float step = base_step * voice->render_pitch;
            float final_vol_l = (voice->current_vol / 32767.0f) * pan_l_scalar;
            float final_vol_r = (voice->current_vol / 32767.0f) * pan_r_scalar;

            if ((u32)cursor >= total_frames) {
                if (voice->has_loop) cursor = (float)voice->loop_start;
                else { voice->active = 0; break; }
            }

            u32 idx = (u32)cursor;
            float frac = cursor - (float)idx;
            u32 next_idx = idx + 1;

            if (voice->has_loop && next_idx >= voice->loop_end) {
                next_idx = voice->loop_start;
            } else if (next_idx >= total_frames) {
                next_idx = idx;
            }

            float sample_l, sample_r;
            if (voice->snd->channels == 2) {
                float s1_l = (float)samples[idx * 2];
                float s1_r = (float)samples[idx * 2 + 1];
                float s2_l = (float)samples[next_idx * 2];
                float s2_r = (float)samples[next_idx * 2 + 1];
                sample_l = s1_l + (s2_l - s1_l) * frac;
                sample_r = s1_r + (s2_r - s1_r) * frac;
            } else {
                float s1 = (float)samples[idx];
                float s2 = (float)samples[next_idx];
                sample_l = s1 + (s2 - s1) * frac;
                sample_r = sample_l; 
            }

            cursor += step;
            
            if (voice->has_loop && (u32)cursor >= voice->loop_end) {
                if (voice->loop_end > voice->loop_start) {
                    while ((u32)cursor >= voice->loop_end) {
                        cursor -= (float)(voice->loop_end - voice->loop_start);
                    }
                } else {
                    voice->has_loop = 0;
                }
            }
            
            // Use fx_mix directly
            if (voice->fx_mix > 0.001f) {
                g_FxSendL[i] += sample_l * final_vol_l * voice->fx_mix;
                g_FxSendR[i] += sample_r * final_vol_r * voice->fx_mix;
            }

            s32 mix_l = mix_buffer[i*2] + (s32)(sample_l * final_vol_l);
            if (mix_l > 32767) mix_l = 32767;
            if (mix_l < -32768) mix_l = -32768;
            mix_buffer[i*2] = (s16)mix_l;
            
            s32 mix_r = mix_buffer[i*2 + 1] + (s32)(sample_r * final_vol_r);
            if (mix_r > 32767) mix_r = 32767;
            if (mix_r < -32768) mix_r = -32768;
            mix_buffer[i*2 + 1] = (s16)mix_r;
        }
        voice->cursor = cursor;
    }

    // --- REVERB BUS ---
    for (u32 i = 0; i < num_frames; i++) {
        float delay_out_l = g_FxDelayBufferL[g_FxWritePtr];
        float delay_out_r = g_FxDelayBufferR[g_FxWritePtr];

        g_FxLpfL += (delay_out_l - g_FxLpfL) * 0.3f;
        g_FxLpfR += (delay_out_r - g_FxLpfR) * 0.3f;

        g_FxDelayBufferL[g_FxWritePtr] = g_FxSendL[i] + (g_FxLpfL * FX_FEEDBACK_COEFF);
        g_FxDelayBufferR[g_FxWritePtr] = g_FxSendR[i] + (g_FxLpfR * FX_FEEDBACK_COEFF);

        g_FxWritePtr = (g_FxWritePtr + 1) % FX_DELAY_LENGTH;

        s32 mix_l = mix_buffer[i*2] + (s32)(g_FxLpfL * FX_WET_MIX);
        if (mix_l > 32767) mix_l = 32767;
        if (mix_l < -32768) mix_l = -32768;
        mix_buffer[i*2] = (s16)mix_l;

        s32 mix_r = mix_buffer[i*2 + 1] + (s32)(g_FxLpfR * FX_WET_MIX);
        if (mix_r > 32767) mix_r = 32767;
        if (mix_r < -32768) mix_r = -32768;
        mix_buffer[i*2 + 1] = (s16)mix_r;
    }
}