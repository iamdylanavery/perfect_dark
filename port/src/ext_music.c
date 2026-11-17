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
#define FX_DELAY_LENGTH ((u32)(MODERN_MIX_RATE * 0.226757f))
#define FX_FEEDBACK_COEFF 0.3f // Prevents runaway gain stacking
#define FX_WET_MIX 0.4f        // 40% wet is typical for N64 room simulation

static float g_FxLpfL = 0.0f;
static float g_FxLpfR = 0.0f;  

// -------------------------------------------------------------
// DEBUG MIXER & VOLUME CONTROLS
// -------------------------------------------------------------
#define MUSIC_MASTER_BOOST 0.6f

#define DECLICK_ATTACK_FRAMES   256.0f   // Attack ramp on note start & instant volume changes
#define DECLICK_RELEASE_FRAMES  256.0f   // Fade-out ramp on note stop
#define DECLICK_STEAL_FRAMES    256.0f   // Fade-out ramp when an existing voice is stolen

// -------------------------------------------------------------
// MUSIC INSTRUMENTS MODULE
// -------------------------------------------------------------
#define MAX_HQ_MUSIC 4000
struct HqMusic {
    uintptr_t n64_base_addr; 
    u8* pcm_data;
    u32 pcm_size;
    u32 sample_rate;            
    u32 original_sample_rate;
    u32 channels;            
    int is_raw16; 
    u32 loop_start;
    u32 loop_end;
    int has_loop;
    int is_microsample;
    int inst_idx;
    int sound_idx;
    int is_perc;
};
static struct HqMusic g_HqMusic[MAX_HQ_MUSIC];
static u32 g_NumHqMusic = 0;

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
    float render_pitch;  
    float current_pitch;
    float unity_pitch;   
    
    float fx_mix;      
    int raw_pan;
    
    u32 loop_start;    
    u32 loop_end;
    int has_loop;
    int is_native_fallback; 
    int is_stopping;
    int active;
};
static struct MusicVoice g_MusicVoices[MAX_MUSIC_VOICES];

// Reverb/Echo Circular Buffers and local accumulators
static float g_FxDelayBufferL[32768] = {0}; 
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
    return 0; 
}

// -------------------------------------------------------------
// 48kHz+ MICROSAMPLE REMASTERER
// -------------------------------------------------------------
static inline float extAudioCosineBell(float x) {
    float ax = fabsf(x);
    if (ax >= 1.5f) return 0.0f;
    // Pure raised-cosine bell: strictly positive (0% clipping overshoot) and smooths ADPCM steps
    return 0.5f * (1.0f + cosf(3.14159265359f * (x / 1.5f)));
}

static void extAudioRemasterMicroSample(struct HqMusic *hq, u32 raw_loop_end) {
    if (!hq || !hq->pcm_data || hq->channels == 0) return;

    u32 wav_frames = (hq->pcm_size / 2) / hq->channels;
    if (wav_frames < 8) return;

    s16 *src_raw = (s16*)hq->pcm_data;

    // 1. Copy mono waveform into float space with 20% headroom so -32768 peaks never clip
    float *wave = (float*)malloc(wav_frames * sizeof(float));
    if (!wave) return;

    for (u32 i = 0; i < wav_frames; i++) {
        wave[i] = (float)src_raw[i * hq->channels] * 0.80f;
    }

    u32 oversample = 4; // 4x integer oversampling (e.g. 22050 Hz -> 88200 Hz)
    const int kernel_radius = 2;

    // Case A: Non-looping one-shot microsample (e.g. hi-hats / clicks)
    if (!hq->has_loop) {
        u32 out_frames = wav_frames * oversample;
        u32 out_bytes = out_frames * sizeof(s16);
        s16 *out_pcm = (s16*)malloc(out_bytes);
        if (!out_pcm) {
            free(wave);
            return;
        }

        for (u32 i = 0; i < out_frames; i++) {
            float src_pos = (float)i / (float)oversample;
            s32 center = (s32)floorf(src_pos);
            float sum = 0.0f;
            float w_sum = 0.0f;

            for (int k = -kernel_radius; k <= kernel_radius + 1; k++) {
                s32 idx = center + k;
                if (idx >= 0 && idx < (s32)wav_frames) {
                    float w = extAudioCosineBell(src_pos - (float)idx);
                    sum += wave[idx] * w;
                    w_sum += w;
                }
            }

            s32 s = (w_sum > 0.0001f) ? (s32)roundf(sum / w_sum) : 0;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            out_pcm[i] = (s16)s;
        }

        free(wave);
        free(hq->pcm_data);
        hq->pcm_data = (u8*)out_pcm;
        hq->pcm_size = out_bytes;
        hq->channels = 1;
        hq->sample_rate = hq->sample_rate * oversample;
        return;
    }

    // Case B: Looping instrument (Bass pulses 84-99, synths, horns, strings)
    u32 start_f = hq->loop_start;
    u32 end_f = (raw_loop_end <= wav_frames && raw_loop_end > start_f) ? raw_loop_end : wav_frames;
    if (start_f >= end_f) start_f = 0;

    u32 loop_len = end_f - start_f;
    if (loop_len < 4) {
        free(wave);
        return;
    }

    // 2. Short 6-sample cosine ease at the very end of the loop so [end_f - 1] meets [start_f]
    //    without altering the pulse width or waveform body!
    u32 blend_len = 6;
    if (blend_len > loop_len / 4) blend_len = loop_len / 4;
    if (blend_len >= 1) {
        float target_val = wave[start_f];
        float end_val = wave[end_f - 1];
        float diff = target_val - end_val;
        for (u32 b = 0; b < blend_len; b++) {
            float alpha = 0.5f - 0.5f * cosf(3.14159265359f * (float)(b + 1) / (float)blend_len);
            u32 tail_idx = end_f - blend_len + b;
            wave[tail_idx] += diff * alpha;
        }
    }

    u32 num_repeats = (loop_len < 256) ? 32 : 8;
    u32 hi_attack_frames = start_f * oversample;
    u32 hi_loop_len = loop_len * oversample;
    u32 out_total_frames = hi_attack_frames + (hi_loop_len * num_repeats);
    u32 out_bytes = out_total_frames * sizeof(s16);

    s16 *out_pcm = (s16*)malloc(out_bytes);
    if (!out_pcm) {
        free(wave);
        return;
    }

    // 3. Reconstruct the Attack [0 .. start_f) with unity-normalized cosine bell
    for (u32 i = 0; i < hi_attack_frames; i++) {
        float src_pos = (float)i / (float)oversample;
        s32 center = (s32)floorf(src_pos);
        float sum = 0.0f;
        float w_sum = 0.0f;

        for (int k = -kernel_radius; k <= kernel_radius + 1; k++) {
            s32 idx = center + k;
            float sample_val = 0.0f;
            if (idx < 0) {
                sample_val = wave[0];
            } else if (idx < (s32)end_f) {
                sample_val = wave[idx];
            } else {
                u32 wrapped = start_f + ((u32)(idx - (s32)start_f) % loop_len);
                sample_val = wave[wrapped];
            }
            float w = extAudioCosineBell(src_pos - (float)idx);
            sum += sample_val * w;
            w_sum += w;
        }

        s32 s = (w_sum > 0.0001f) ? (s32)roundf(sum / w_sum) : 0;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out_pcm[i] = (s16)s;
    }

    // 4. Reconstruct 1 master high-res loop cycle [start_f .. end_f)
    s16 *hi_loop = (s16*)malloc(hi_loop_len * sizeof(s16));
    if (!hi_loop) {
        free(out_pcm);
        free(wave);
        return;
    }

    for (u32 i = 0; i < hi_loop_len; i++) {
        float rel_pos = (float)i / (float)oversample;
        s32 rel_center = (s32)floorf(rel_pos);
        float sum = 0.0f;
        float w_sum = 0.0f;

        for (int k = -kernel_radius; k <= kernel_radius + 1; k++) {
            s32 rel_idx = rel_center + k;
            float w = extAudioCosineBell(rel_pos - (float)rel_idx);

            s32 wrapped_rel = rel_idx % (s32)loop_len;
            if (wrapped_rel < 0) wrapped_rel += (s32)loop_len;

            sum += wave[start_f + (u32)wrapped_rel] * w;
            w_sum += w;
        }

        s32 s = (w_sum > 0.0001f) ? (s32)roundf(sum / w_sum) : 0;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        hi_loop[i] = (s16)s;
    }

    // 5. Unroll the reconstructed loop cycle num_repeats times after the attack
    for (u32 r = 0; r < num_repeats; r++) {
        memcpy(out_pcm + hi_attack_frames + (r * hi_loop_len), hi_loop, hi_loop_len * sizeof(s16));
    }

    free(hi_loop);
    free(wave);
    free(hq->pcm_data);

    hq->pcm_data = (u8*)out_pcm;
    hq->pcm_size = out_bytes;
    hq->channels = 1;
    hq->sample_rate = hq->sample_rate * oversample;
    hq->loop_start = hi_attack_frames;
    hq->loop_end = out_total_frames;
    hq->has_loop = 1;
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
            u32 data_size = 0;

            u8 *pcmData = (u8*)extAudioLoadWavPcm16(fileData, fileSize, &sample_rate, &channels, &data_size);
            if (pcmData) {
                u32 hq_id = g_NumHqMusic++;
                g_HqMusic[hq_id].n64_base_addr = (uintptr_t)w->base;
                g_HqMusic[hq_id].pcm_data = pcmData;
                g_HqMusic[hq_id].channels = channels; 
                g_HqMusic[hq_id].pcm_size = data_size;
                g_HqMusic[hq_id].sample_rate = sample_rate;
                g_HqMusic[hq_id].is_microsample = (data_size < 2000) ? 1 : 0;

                // Sample rate deduction math
                u32 original_samples = 0;
                if (w->type == AL_ADPCM_WAVE) {
                    original_samples = (w->len / 9) * 16;
                } else if (w->type == AL_RAW16_WAVE) {
                    original_samples = w->len / 2;
                }

                // 1. Calculate the raw ratio
                u32 calculated_orig_rate = 22050; // Safe default
                u32 wav_frames = (data_size / 2) / channels;
                u32 override_rate = getOverrideOriginalRate(is_percussion, inst_idx, s);

                if (override_rate > 0) {
                    calculated_orig_rate = override_rate;
                } else {
                    if (wav_frames > 0 && original_samples > 0) {
                        u32 ratio_rate = ((unsigned long long)original_samples * sample_rate) / wav_frames;
                        calculated_orig_rate = extAudioRoundToNearestStandardRate(ratio_rate);
                        if (calculated_orig_rate > 32000) calculated_orig_rate = 22050;
                    }
                }

                g_HqMusic[hq_id].original_sample_rate = calculated_orig_rate;
                g_HqMusic[hq_id].inst_idx = inst_idx;
                g_HqMusic[hq_id].sound_idx = s;
                g_HqMusic[hq_id].is_perc = is_percussion;

                // Loop calculation using the deduced calculated_orig_rate
                g_HqMusic[hq_id].has_loop = 0;
                ALADPCMloop *l = NULL;
                if (w->type == AL_ADPCM_WAVE && w->waveInfo.adpcmWave.loop) {
                    l = (ALADPCMloop*)w->waveInfo.adpcmWave.loop;
                } else if (w->type == AL_RAW16_WAVE && w->waveInfo.rawWave.loop) {
                    l = (ALADPCMloop*)w->waveInfo.rawWave.loop; 
                }

                u32 raw_loop_end = 0;
                if (l && l->count != 0) { 
                    g_HqMusic[hq_id].loop_start = ((unsigned long long)l->start * sample_rate) / calculated_orig_rate;
                    raw_loop_end = ((unsigned long long)l->end * sample_rate) / calculated_orig_rate;
                    g_HqMusic[hq_id].loop_end = raw_loop_end;
                    
                    // Safety clamps to prevent out-of-bounds blips
                    if (g_HqMusic[hq_id].loop_end >= wav_frames) g_HqMusic[hq_id].loop_end = wav_frames - 1;
                    if (g_HqMusic[hq_id].loop_start >= g_HqMusic[hq_id].loop_end) g_HqMusic[hq_id].loop_start = 0;
                    g_HqMusic[hq_id].has_loop = 1;
                }

                // 2. If this is a looped microsample (< 2000 bytes), procedurally resynthesize it!
                if (g_ExtRemasterMicroSamples && g_HqMusic[hq_id].is_microsample) {
                    extAudioRemasterMicroSample(&g_HqMusic[hq_id], raw_loop_end);
                }

                // 3. Apply load-time peak normalization to the final buffer
                if (g_ExtNormalizeMusic) {
                    extAudioNormalizePcm16((s16*)g_HqMusic[hq_id].pcm_data, g_HqMusic[hq_id].pcm_size, g_ExtNormalizeMusicTarget, g_ExtNormalizeAttenuateOnly);
                }

                if (g_ExtLogMusicEnabled) {
                    sysLogPrintf(LOG_NOTE, "EXT-MUS | [SEQ LOAD] | %s: %03d | Sound: %02d | Orig: %uHz | New: %uHz", 
                                 is_percussion ? "PERC" : "INST", inst_idx, s, calculated_orig_rate, g_HqMusic[hq_id].sample_rate);
                }
            }
        } else {
            // Even if no HQ WAV exists on disk, record the bank instrument mapping
            // so fallback notes can still be tracked in the log and muted by the Stem Mixer
            if (g_NumHqMusic < MAX_HQ_MUSIC) {
                u32 hq_id = g_NumHqMusic++;
                g_HqMusic[hq_id].n64_base_addr = (uintptr_t)w->base;
                g_HqMusic[hq_id].pcm_data = NULL; // Native ROM sample
                g_HqMusic[hq_id].pcm_size = 0;
                g_HqMusic[hq_id].is_microsample = 0;
                g_HqMusic[hq_id].inst_idx = inst_idx;
                g_HqMusic[hq_id].sound_idx = s;
                g_HqMusic[hq_id].is_perc = is_percussion;
                g_HqMusic[hq_id].original_sample_rate = 22050;
                g_HqMusic[hq_id].sample_rate = 22050;
                g_HqMusic[hq_id].channels = 1;
                g_HqMusic[hq_id].has_loop = 0;
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

int extAudioSeqVoiceStart(void *n64_voice, ALWaveTable *w, float pitch, float unity, int vol, int pan, int fxmix, u8 arg6, float arg7, u8 arg8, ALMicroTime t) {
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

    InstrumentCategory cat = extAudioGetInstCategory(snd->inst_idx);

    // STEM MIXER MUTE
    if (!g_ExtMusicCategoryEnabled[cat]) {
        for (int j = 0; j < MAX_MUSIC_VOICES; j++) {
            if (g_MusicVoices[j].active && g_MusicVoices[j].n64_voice == n64_voice) {
                g_MusicVoices[j].active = 0;
                g_MusicVoices[j].is_stopping = 0;
            }
        }
        return 1;
    }

    // Native fallback pool (Inst 9 & 10 stay on N64 fallback; microsamples use g_ExtRemasterMicroSamples toggle)
    int is_fallback = (snd->pcm_data == NULL || 
                       (!g_ExtRemasterMicroSamples && snd->is_microsample) || 
                       snd->inst_idx == 10 || 
                       snd->inst_idx == 9);

    // CLEAN SLATE: Free any old voice sharing this N64 pointer
    for (int j = 0; j < MAX_MUSIC_VOICES; j++) {
        if (g_MusicVoices[j].active && g_MusicVoices[j].n64_voice == n64_voice) {
            g_MusicVoices[j].active = 0;
            g_MusicVoices[j].is_stopping = 0;
        }
    }

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (!g_MusicVoices[i].active) { 
            g_MusicVoices[i].n64_voice = n64_voice;
            g_MusicVoices[i].snd = snd;
            g_MusicVoices[i].cursor = 0.0f;
            
            // FULL VELOCITY ON ATTACK
            g_MusicVoices[i].current_vol = (float)vol;
            g_MusicVoices[i].target_vol  = (float)vol;
            g_MusicVoices[i].vol_step    = 0.0f;
            
            float final_pitch = pitch * (unity > 0.0f ? unity : 1.0f);
            g_MusicVoices[i].unity_pitch = (unity > 0.0f ? unity : 1.0f);
            g_MusicVoices[i].current_pitch = final_pitch;
            g_MusicVoices[i].render_pitch = final_pitch; 

            g_MusicVoices[i].raw_pan = pan;
            g_MusicVoices[i].fx_mix = (float)fxmix / 127.0f;
            g_MusicVoices[i].has_loop = snd->has_loop;
            g_MusicVoices[i].loop_start = snd->loop_start;
            g_MusicVoices[i].loop_end = snd->loop_end;
            g_MusicVoices[i].is_native_fallback = is_fallback;
            g_MusicVoices[i].is_stopping = 0;
            g_MusicVoices[i].active = 1;

            return is_fallback ? 0 : 1;
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

                // Calculate ramp frames (minimum 64 frames = ~1.3ms de-click protection)
                float frames = (t > 0) ? (((float)t / 1000000.0f) * 48000.0f) : 64.0f;
                if (frames < 64.0f) frames = 64.0f;

                g_MusicVoices[i].vol_step = (g_MusicVoices[i].target_vol - g_MusicVoices[i].current_vol) / frames;
            }
            found = 1; 
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
                float scaled_pitch = pitch * g_MusicVoices[i].unity_pitch;
                g_MusicVoices[i].current_pitch = scaled_pitch;
            }
            found = 1; 
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
            if (g_MusicVoices[i].current_vol <= 1.0f || g_MusicVoices[i].is_stopping) {
                g_MusicVoices[i].active = 0;
                g_MusicVoices[i].is_stopping = 0;
            } else {
                // Micro-ramp down to 0 over 128 frames (~2.6ms) to eliminate note-off pops
                g_MusicVoices[i].target_vol = 0.0f;
                g_MusicVoices[i].vol_step = -g_MusicVoices[i].current_vol / 128.0f;
                g_MusicVoices[i].is_stopping = 1;
            }
            found = 1; 
        }
    }
    return found;
}

// -------------------------------------------------------------
// LEGACY STUBS & LOGGING
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
        case CAT_BEEP:       return "PULS";
        default:             return "UNKN";
    }
}

// Terminal Dashboard with Distinct ROM Target vs. External WAV Displays
void extAudioPrintMusicVoices(const char* prefix) {
    int audible_voices = 0;
    
    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].current_vol > 0.0f) {
            InstrumentCategory cat = extAudioGetInstCategory(g_MusicVoices[i].snd->inst_idx);
            if (g_ExtMusicCategoryEnabled[cat]) {
                audible_voices++;
            }
        }
    }

    if (audible_voices == 0) return;

    sysLogPrintf(LOG_NOTE, "==========================================================================================");
    sysLogPrintf(LOG_NOTE, "[ %s MIXER DASHBOARD ] - Audible Voices: %d", prefix, audible_voices);
    sysLogPrintf(LOG_NOTE, "------------------------------------------------------------------------------------------");
    sysLogPrintf(LOG_NOTE, "V# | CAT  | SRC | ASSET / TARGET        | RATE     | N64 VOL | PAN | PITCH | STATUS");
    sysLogPrintf(LOG_NOTE, "------------------------------------------------------------------------------------------");

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        struct MusicVoice *v = &g_MusicVoices[i];
        if (v->active && v->current_vol > 0.0f) {
            InstrumentCategory cat = extAudioGetInstCategory(v->snd->inst_idx);
            
            if (!g_ExtMusicCategoryEnabled[cat]) continue;

            const char* cat_name = extAudioGetCatName(cat);
            const char* src_name = v->is_native_fallback ? "N64" : (v->snd->is_microsample ? "SYN" : "EXT");
            const char* status_str = v->has_loop ? "(LOOP)" : "(1SHT)";
            
            char file_str[24];
            char rate_str[12];

            if (v->is_native_fallback) {
                if (v->snd->is_perc) {
                    snprintf(file_str, sizeof(file_str), "ROM: Perc Snd %02d", v->snd->sound_idx);
                } else {
                    snprintf(file_str, sizeof(file_str), "ROM: Inst %03d Snd %02d", v->snd->inst_idx, v->snd->sound_idx);
                }
                snprintf(rate_str, sizeof(rate_str), "%5uHz", v->snd->original_sample_rate);
            } else {
                if (v->snd->is_perc) {
                    snprintf(file_str, sizeof(file_str), "seq_perc_snd%02d.wav", v->snd->sound_idx);
                } else {
                    snprintf(file_str, sizeof(file_str), "seq_inst%03d_snd%02d.wav", v->snd->inst_idx, v->snd->sound_idx);
                }
                snprintf(rate_str, sizeof(rate_str), "%5uHz", v->snd->sample_rate);
            }

            sysLogPrintf(LOG_NOTE, "%02d | %s | %-3s | %-21s | %-8s | %7d | %3d | %5.2f | %s",
                i, cat_name, src_name, file_str, rate_str,
                (int)v->current_vol, v->raw_pan, v->current_pitch, status_str);
        }
    }
    sysLogPrintf(LOG_NOTE, "==========================================================================================\n");
}

// -------------------------------------------------------------
// SEQUENCE SYNTHESIZER MIXER
// -------------------------------------------------------------
void extAudioMixMusic(s16 *mix_buffer, u32 num_frames, float crossfade_vol) {
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

        // Do not render PCM audio for native fallback voices (RSP handles them in n64_buf)
        if (voice->is_native_fallback) {
            continue;
        }

        s16 *samples = (s16*)voice->snd->pcm_data;
        u32 total_frames = (voice->snd->pcm_size / 2) / voice->snd->channels;
        float cursor = voice->cursor;
        
        float base_step = (float)voice->snd->sample_rate / (float)voice->snd->original_sample_rate;
        
        float pan_norm = (float)(voice->raw_pan & 0x7F) / 127.0f;
        float pan_l_scalar = cosf(pan_norm * 1.570796327f) * MUSIC_MASTER_BOOST * crossfade_vol;
        float pan_r_scalar = sinf(pan_norm * 1.570796327f) * MUSIC_MASTER_BOOST * crossfade_vol;

        InstrumentCategory cat = extAudioGetInstCategory(voice->snd->inst_idx);
        if (!g_ExtMusicCategoryEnabled[cat]) {
            pan_l_scalar = 0.0f;
            pan_r_scalar = 0.0f;
        }

        for (u32 i = 0; i < num_frames; i++) {
            // Volume ramping
            if (fabsf(voice->target_vol - voice->current_vol) > fabsf(voice->vol_step)) {
                voice->current_vol += voice->vol_step;
            } else {
                voice->current_vol = voice->target_vol;
                if (voice->is_stopping && voice->current_vol <= 0.001f) {
                    voice->active = 0;
                    voice->is_stopping = 0;
                    break;
                }
            }

            float step = base_step * voice->render_pitch * (22020.0f / 48000.0f);
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