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

extern u16 snd0000e9dc(void); 
extern u16 g_SfxVolume;
#ifdef MAXFLOAT
#undef MAXFLOAT   // Prevents compiler redefined warnings on macOS
#endif

#define MUSIC_MASTER_BOOST 0.7f  // Boost music samples to match SFX punch
#define SEQ_VOLUME_DIVISOR 1     // Keep raw WAV volume at 100%

// -------------------------------------------------------------
// GLOBAL SETTINGS & TOGGLE STATE
// -------------------------------------------------------------
static int g_ExtAudioEnabled = 1;

// NEW: 50% Volume scale happy-medium for external samples
#define EXT_AUDIO_VOLUME_SCALE 0.33f 

int extAudioGetEnabled(void) { return g_ExtAudioEnabled; }
void extAudioSetEnabled(int state) {
    g_ExtAudioEnabled = state;
    sysLogPrintf(LOG_NOTE, "EXT-AUDIO: External Audio has been %s.", state ? "ENABLED" : "DISABLED");
}

// -------------------------------------------------------------
// MUSIC CACHE (Trojan Horse)
// -------------------------------------------------------------

#define MAX_HQ_MUSIC 4000
struct HqMusic {
    u32 token_start;
    u32 token_end;
    u8* pcm_data;
    u32 pcm_size;
    int is_raw16; 
    u32 loop_start;
    u32 loop_end;
    int has_loop;
// NEW: Dashboard tracking
    int inst_idx;
    int sound_idx;
    int is_perc;
};

static struct HqMusic g_HqMusic[MAX_HQ_MUSIC];
static u32 g_NumHqMusic = 0;
static u32 g_HqTokenAllocator = 0x40000000;
uintptr_t g_HqFakeOffsets[4096] = {0};

// -------------------------------------------------------------
// SFX CACHE (Modern SDL2 Bypass)
// -------------------------------------------------------------
struct CachedSfx {
    int state; // 0 = uninitialized, 1 = loaded, -1 = missing
    u32 sample_rate;
    u32 sample_count;
    s16 *samples;
};
static struct CachedSfx g_SfxCache[2000];

#define MAX_MODERN_VOICES 64 // BUMPED: Supports up to 64 modern voices
struct ModernVoice {
    void *n64_handle;
    int sfx_id;
    float cursor;
    float pitch;
    float vol_l;
    float vol_r;
    int raw_vol;   
    int raw_pan;   
    int active;
};

#define MAX_MUSIC_VOICES 96 // Plenty of voices for complex, dense sequences
struct MusicVoice {
    void *n64_voice;   // Matches N_ALVoice* pointer
    struct HqMusic *snd;
    float cursor;
    float pitch;
    float vol_l;
    float vol_r;
    int raw_vol;
    int raw_pan;
    u32 loop_start;    // Loop points in raw samples
    u32 loop_end;
    int has_loop;
    int active;
};

static struct MusicVoice g_MusicVoices[MAX_MUSIC_VOICES];
static struct ModernVoice g_ModernVoices[MAX_MODERN_VOICES];
static s16 g_MixBuffer[8192 * 2]; // SDL Mix Buffer


// -------------------------------------------------------------
// TERMINAL MIXER DASHBOARD
// -------------------------------------------------------------
static int g_DashTimer = 0;

void extAudioMonitorDashboard(void) {
    g_DashTimer++;
    // Run exactly once every ~60 frames (1 second)
    if (g_DashTimer < 60) return;
    g_DashTimer = 0;

    int active_voices = 0;
    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].raw_vol > 0) active_voices++;
    }

    if (active_voices == 0) return; // Don't spam if music is stopped

    sysLogPrintf(LOG_NOTE, "================================================================================");
    sysLogPrintf(LOG_NOTE, "[ EXT-AUDIO MIXER DASHBOARD ] - Active Music Voices: %d", active_voices);
    sysLogPrintf(LOG_NOTE, "--------------------------------------------------------------------------------");
    sysLogPrintf(LOG_NOTE, "V# | TYPE | INST | SND | N64 VOL | PAN | PITCH | L_VOL | R_VOL | STATUS");
    sysLogPrintf(LOG_NOTE, "--------------------------------------------------------------------------------");

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        struct MusicVoice *v = &g_MusicVoices[i];
        if (v->active && v->raw_vol > 0) {
            const char* type = v->snd->is_perc ? "PERC" : "INST";
            const char* status = v->has_loop ? "(LOOPING)" : "(ONESHOT)";
            
            sysLogPrintf(LOG_NOTE, "%02d | %s | %04d | %03d | %7d | %3d | %5.2f | %5.2f | %5.2f | %s",
                i, type, v->snd->inst_idx, v->snd->sound_idx,
                v->raw_vol, v->raw_pan, v->pitch, v->vol_l, v->vol_r, status);
        }
    }
    sysLogPrintf(LOG_NOTE, "================================================================================\n");
}

// Helper to format consistent sequential filenames (sfx_id - 1)
static void getSfxFilename(int sfx_id, char *out_str, size_t max_len) {
    int bank_index = sfx_id - 1;
    if (bank_index >= 0) {
        snprintf(out_str, max_len, "sfx_%04d.wav", bank_index);
    } else {
        snprintf(out_str, max_len, "N/A");
    }
}

static void loadSfxFromDisk(int sfx_id) {
    char filepath[256];
    int bank_index = sfx_id - 1;
    
    char filename[32];
    getSfxFilename(sfx_id, filename, sizeof(filename));
    const char* prefix = g_ExtAudioEnabled ? "EXT-AUDIO" : "N64-AUDIO";

    if (bank_index < 0) {
        g_SfxCache[sfx_id].state = -1;
        return;
    }

    snprintf(filepath, sizeof(filepath), "ext_sfx/sfx_%04d.wav", bank_index);
    
    // [STEP B] Aligned File Lookup
    sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | File: %-16s | Loading raw PCM data...", 
                 prefix, "[DISK LOAD]", sfx_id, filename);
    
    u32 fileSize = 0;
    u8 *fileData = fsFileLoad(filepath, &fileSize);
    
    if (!fileData || fileSize < 44) {
        g_SfxCache[sfx_id].state = -1; 
        if (fileData) free(fileData);
        return;
    }

    u32 dataOffset = 0, dataSize = 0;
    for (u32 k = 12; k < fileSize - 4; k++) {
        if (memcmp(fileData + k, "data", 4) == 0) {
            dataSize = fileData[k+4] | (fileData[k+5]<<8) | (fileData[k+6]<<16) | (fileData[k+7]<<24);
            dataOffset = k + 8;
            break;
        }
    }

    if (dataOffset > 0 && dataSize > 0) {
        u32 sample_rate = 22050; // Default
        for (u32 k = 12; k < fileSize - 8; k++) {
            if (memcmp(fileData + k, "fmt ", 4) == 0) {
                u32 fmtOffset = k + 8;
                sample_rate = fileData[fmtOffset + 4] | (fileData[fmtOffset + 5] << 8) | (fileData[fmtOffset + 6] << 16) | (fileData[fmtOffset + 7] << 24);
                break;
            }
        }

        s16 *pcmData = malloc(dataSize);
        if (pcmData) {
            memcpy(pcmData, fileData + dataOffset, dataSize);
            s16 *samples = (s16*)pcmData;

            g_SfxCache[sfx_id].sample_rate = sample_rate;
            g_SfxCache[sfx_id].sample_count = dataSize / 2; // 16-bit mono
            g_SfxCache[sfx_id].samples = samples;
            g_SfxCache[sfx_id].state = 1;
        }
    }
    free(fileData);
}

int extAudioModernStart(void *n64_handle, int sfx_id, int vol, int pan, float pitch) {
    char filename[32];
    getSfxFilename(sfx_id, filename, sizeof(filename));
    const char* prefix = g_ExtAudioEnabled ? "EXT-AUDIO" : "N64-AUDIO";

    // [STEP A] Column-Aligned Request Log (Always fires so you can trace N64 outputs)
    sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | File: %-16s | Vol: %5d | Pan: %3d | Pitch: %1.2f | Voices: --/--", 
                 prefix, "[REQUEST]", sfx_id, filename, vol, pan, pitch);
    
    // [STEP C] Dynamic Fallback Log if the F2 toggle is turned off
    if (!g_ExtAudioEnabled) {
        sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | File: %-16s | Yielding to N64 microcode.", 
                     prefix, "[FALLBACK]", sfx_id, filename);
        return 0;
    }
    
    if (g_SfxCache[sfx_id].state == 0) {
        loadSfxFromDisk(sfx_id);
    }
    
    if (g_SfxCache[sfx_id].state != 1) {
        // [STEP C] Dynamic Fallback Log if file physically missing from disk
        sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | File: %-16s | Yielding to N64 microcode (Missing).", 
                     prefix, "[FALLBACK]", sfx_id, filename);
        return 0;
    }

    // Find a free voice
    for (int i = 0; i < MAX_MODERN_VOICES; i++) {
        if (!g_ModernVoices[i].active) {
            g_ModernVoices[i].n64_handle = n64_handle;
            g_ModernVoices[i].sfx_id = sfx_id;
            g_ModernVoices[i].cursor = 0.0f;
            g_ModernVoices[i].pitch = (pitch > 0) ? pitch : 1.0f;
            g_ModernVoices[i].raw_vol = (vol != -1) ? vol : 32767;
            g_ModernVoices[i].raw_pan = (pan != -1) ? pan : 64;
            g_ModernVoices[i].active = 1; 

            extAudioModernAdjust(n64_handle, vol, pan, pitch); 
            
            int active_count = 0;
            for (int v = 0; v < MAX_MODERN_VOICES; v++) {
                if (g_ModernVoices[v].active) active_count++;
            }

            // [STEP D] Success Column Log containing File Name and Active Voice counts
            sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | File: %-16s | Vol: %5d | Pan: %3d | Pitch: %1.2f | Voices: %2d/%2d", 
                         prefix, "[SUCCESS]", sfx_id, filename, vol, pan, pitch, active_count, MAX_MODERN_VOICES);
            
            return 1;
        }
    }
    return 0;
}

int extAudioModernAdjust(void *n64_handle, int vol, int pan, float pitch) {
    if (!g_ExtAudioEnabled) return 0;

    for (int i = 0; i < MAX_MODERN_VOICES; i++) {
        if (g_ModernVoices[i].active && g_ModernVoices[i].n64_handle == n64_handle) {
            if (pitch != -1.0f) g_ModernVoices[i].pitch = pitch;
            
            if (vol != -1) g_ModernVoices[i].raw_vol = vol;
            if (pan != -1) g_ModernVoices[i].raw_pan = pan;
            
            float vol_norm = (float)g_ModernVoices[i].raw_vol / 32767.0f;
            float pan_norm = (float)(g_ModernVoices[i].raw_pan & 0x7F) / 127.0f;
            
            g_ModernVoices[i].vol_l = vol_norm * cosf(pan_norm * 1.570796327f);
            g_ModernVoices[i].vol_r = vol_norm * sinf(pan_norm * 1.570796327f);
            
            return 1;
        }
    }
    return 0;
}

s16* extAudioProcessSDL(const s16 *n64_buf, u32 num_bytes) {

    // NEW: Call the Dashboard monitor
    if (g_ExtAudioEnabled) extAudioMonitorDashboard();

    if (num_bytes > sizeof(g_MixBuffer)) {
        sysLogPrintf(LOG_WARNING, "EXT-AUDIO: Segfault prevented! Buffer spike: %u bytes.", num_bytes);
        return (s16*)n64_buf; 
    }

    memcpy(g_MixBuffer, n64_buf, num_bytes);
    if (!g_ExtAudioEnabled) return g_MixBuffer;
    
    // Normalize the global master volume variable (max is 20480 / 0x5000)
    float master_sfx_vol = (float)g_SfxVolume / 20480.0f;
    if (master_sfx_vol > 1.0f) master_sfx_vol = 1.0f;

    u32 num_samples = num_bytes / 2; 
    u32 num_frames = num_samples / 2; 
    
    // --- 1. MIX MODERN SFX VOICES ---
    for (int v = 0; v < MAX_MODERN_VOICES; v++) {
        if (!g_ModernVoices[v].active) continue;
        
        struct ModernVoice *voice = &g_ModernVoices[v];
        struct CachedSfx *cache = &g_SfxCache[voice->sfx_id];
        
        if (cache->state != 1 || cache->samples == NULL) {
            voice->active = 0;
            continue;
        }
        
        float cursor = voice->cursor;
        float base_step = (float)cache->sample_rate / 22050.0f;
        float step = base_step * voice->pitch;
        
        for (u32 i = 0; i < num_frames; i++) {
            if ((u32)cursor >= cache->sample_count) {
                voice->active = 0;
                break;
            }
            
            s16 sample = cache->samples[(u32)cursor];
            cursor += step;
            
            float scale_factor = EXT_AUDIO_VOLUME_SCALE * master_sfx_vol;

            s32 mix_l = g_MixBuffer[i*2] + (s32)(sample * voice->vol_l * scale_factor);
            if (mix_l > 32767) mix_l = 32767;
            if (mix_l < -32768) mix_l = -32768;
            g_MixBuffer[i*2] = (s16)mix_l;
            
            s32 mix_r = g_MixBuffer[i*2 + 1] + (s32)(sample * voice->vol_r * scale_factor);
            if (mix_r > 32767) mix_r = 32767;
            if (mix_r < -32768) mix_r = -32768;
            g_MixBuffer[i*2 + 1] = (s16)mix_r;
        }
        voice->cursor = cursor;
    }

    // --- 2. MIX HIGH-FIDELITY MUSIC INSTRUMENTS ---
    for (int m = 0; m < MAX_MUSIC_VOICES; m++) {
        if (!g_MusicVoices[m].active) continue;
        
        struct MusicVoice *voice = &g_MusicVoices[m];
        s16 *samples = (s16*)voice->snd->pcm_data;
        u32 sample_count = voice->snd->pcm_size / 2;
        
        float cursor = voice->cursor;
        float step = voice->pitch; 
        
        for (u32 i = 0; i < num_frames; i++) {
            // 1. SAFETY FIRST: Check EOF before attempting to fetch a sample
            if ((u32)cursor >= sample_count) {
                voice->active = 0;
                break;
            }
            
            // 2. Fetch sample and advance cursor
            s16 sample = samples[(u32)cursor];
            cursor += step;
            
            // 3. PERFECT SUSTAIN: Check loop points immediately
            if (voice->has_loop && (u32)cursor >= voice->loop_end) {
                if (voice->loop_end > voice->loop_start) {
                    // Use a while-loop just in case of massive pitch-bend overshoots
                    while ((u32)cursor >= voice->loop_end) {
                        cursor -= (float)(voice->loop_end - voice->loop_start);
                    }
                } else {
                    voice->has_loop = 0; // Invalid loop failsafe
                }
            }
            
            // 4. MIX: Restored volume (no double-attenuation, no base_vol)
            float final_vol_l = voice->vol_l * MUSIC_MASTER_BOOST;
            float final_vol_r = voice->vol_r * MUSIC_MASTER_BOOST;

            s32 mix_l = g_MixBuffer[i*2] + (s32)(sample * final_vol_l);
            if (mix_l > 32767) mix_l = 32767;
            if (mix_l < -32768) mix_l = -32768;
            g_MixBuffer[i*2] = (s16)mix_l;
            
            s32 mix_r = g_MixBuffer[i*2 + 1] + (s32)(sample * final_vol_r);
            if (mix_r > 32767) mix_r = 32767;
            if (mix_r < -32768) mix_r = -32768;
            g_MixBuffer[i*2 + 1] = (s16)mix_r;
        }
        voice->cursor = cursor;
    }
    
    return g_MixBuffer;
}

// -------------------------------------------------------------
// INIT & CACHE
// -------------------------------------------------------------

void extAudioInit(void) {
    g_NumHqMusic = 0;
    g_HqTokenAllocator = 0x40000000;
    memset(g_HqFakeOffsets, 0, sizeof(g_HqFakeOffsets));
    memset(g_SfxCache, 0, sizeof(g_SfxCache));
    memset(g_ModernVoices, 0, sizeof(g_ModernVoices));
}

void extAudioResetCache(void) {
    for (u32 i = 0; i < g_NumHqMusic; i++) {
        if (g_HqMusic[i].pcm_data) free(g_HqMusic[i].pcm_data);
    }
    g_NumHqMusic = 0;
    g_HqTokenAllocator = 0x40000000;

    for (u32 i = 0; i < 2000; i++) {
        if (g_SfxCache[i].state == 1) free(g_SfxCache[i].samples);
        g_SfxCache[i].state = 0;
    }
    memset(g_ModernVoices, 0, sizeof(g_ModernVoices));
    sysLogPrintf(LOG_NOTE, "EXT-AUDIO: Audio Cache has been reset.");
}

// -------------------------------------------------------------
// TROJAN HORSE MUSIC ENGINE (XBLA 16-Bit RAW / ADPCM intercepts)
// -------------------------------------------------------------
void extAudioLoadWavetable(ALWaveTable *w, uintptr_t tbl_segment_start) {
    // Leftover/Legacy clean stub. Bypassed entirely by extAudioScanSequenceBank.
}

// HELPER: Process a single ALInstrument (Used for both Melodic and Percussion)
static void scanInstrument(ALInstrument *inst, int inst_idx, int is_percussion, const char* prefix) {
    if (!inst) return;

    for (s32 s = 0; s < inst->soundCount; s++) {
        ALSound *sound = inst->soundArray[s];
        if (!sound || !sound->wavetable) continue;
        ALWaveTable *w = sound->wavetable;

        char filepath[256];
        if (is_percussion) {
            // Note: Adjust this naming convention if your percussion is named differently
            snprintf(filepath, sizeof(filepath), "ext_seq/seq_perc_snd%02d.wav", s);
        } else {
            snprintf(filepath, sizeof(filepath), "ext_seq/seq_inst%03d_snd%02d.wav", inst_idx, s);
        }

        u32 fileSize = 0;
        u8 *fileData = fsFileLoad(filepath, &fileSize);
        
        // If file not found with perc naming, try standard naming as fallback
        if (!fileData && is_percussion) {
            snprintf(filepath, sizeof(filepath), "output_xbla_seq/seq_inst_perc_snd%02d.wav", s);
            fileData = fsFileLoad(filepath, &fileSize);
        }

        if (fileData && fileSize > 44) {
            u32 dataOffset = 0, dataSize = 0;
            for (u32 k = 12; k < fileSize - 4; k++) {
                if (memcmp(fileData + k, "data", 4) == 0) {
                    dataSize = fileData[k+4] | (fileData[k+5]<<8) | (fileData[k+6]<<16) | (fileData[k+7]<<24);
                    dataOffset = k + 8; break;
                }
            }

            if (dataOffset > 0 && g_NumHqMusic < MAX_HQ_MUSIC) {
                u8 *pcmData = malloc(dataSize);
                if (pcmData) {
                    memcpy(pcmData, fileData + dataOffset, dataSize);
                    
                    u32 token = g_HqTokenAllocator;
                    u32 new_adpcm_len = (((dataSize / 2) + 15) / 16) * 9;
                    g_HqTokenAllocator += new_adpcm_len + 4096;

                    u32 hq_id = g_NumHqMusic++;
                    g_HqMusic[hq_id].token_start = token;
                    g_HqMusic[hq_id].token_end = token + new_adpcm_len;
                    g_HqMusic[hq_id].pcm_data = pcmData;
                    g_HqMusic[hq_id].pcm_size = dataSize;
                    g_HqMusic[hq_id].is_raw16 = 0; 
                    
                    // NEW: Save IDs for the Dashboard!
                    g_HqMusic[hq_id].inst_idx = inst_idx;
                    g_HqMusic[hq_id].sound_idx = s;
                    g_HqMusic[hq_id].is_perc = is_percussion;
                    
                    // REMOVED: base_vol double-attenuation math is gone!

                    g_HqMusic[hq_id].has_loop = 0;
                    if (w->type == AL_ADPCM_WAVE && w->waveInfo.adpcmWave.loop) {
                        ALADPCMloop *l = (ALADPCMloop*)w->waveInfo.adpcmWave.loop;
                        if (l->count != 0) { // <-- THE PIANO FIX
                            g_HqMusic[hq_id].loop_start = l->start;
                            g_HqMusic[hq_id].loop_end = l->end;
                            g_HqMusic[hq_id].has_loop = 1;
                        }
                    } else if (w->type == AL_RAW16_WAVE && w->waveInfo.rawWave.loop) {
                        ALRawLoop *l = (ALRawLoop*)w->waveInfo.rawWave.loop;
                        if (l->count != 0) { // <-- THE PIANO FIX
                            g_HqMusic[hq_id].loop_start = l->start;
                            g_HqMusic[hq_id].loop_end = l->end;
                            g_HqMusic[hq_id].has_loop = 1;
                        }
                    }

                    w->len = new_adpcm_len;
                    w->base = (void*)(uintptr_t)token;
                    w->type = AL_ADPCM_WAVE;

                    // FIXED: Restored the Mac Freeze Safety Shield!
                    if (g_HqMusic[hq_id].has_loop) {
                        ALADPCMloop *new_loop = malloc(sizeof(ALADPCMloop));
                        u32 start_frame = g_HqMusic[hq_id].loop_start / 16;
                        u32 end_frame = g_HqMusic[hq_id].loop_end / 16;
                        
                        // Safety Shield: Prevents 0-byte infinite loops on the N64 RSP
                        if (end_frame <= start_frame) {
                            end_frame = start_frame + 1;
                        }
                        
                        new_loop->start = start_frame * 9;
                        new_loop->end = end_frame * 9;
                        new_loop->count = -1;
                        memset(new_loop->state, 0, sizeof(new_loop->state));
                        w->waveInfo.adpcmWave.loop = new_loop;
                    }

                    ALADPCMBook *dummy_book = malloc(sizeof(ALADPCMBook));
                    dummy_book->order = 2; dummy_book->npredictors = 1;
                    memset(dummy_book->book, 0, sizeof(dummy_book->book));
                    w->waveInfo.adpcmWave.book = dummy_book;
                    
                    sysLogPrintf(LOG_NOTE, "%-10s | %-12s | %s: %03d | Sound: %02d | %s", 
                                 prefix, "[SEQ LOAD]", is_percussion ? "PERC" : "INST", 
                                 inst_idx, s, filepath);
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

        // 1. Scan the standard melodic instruments
        for (s32 i = 0; i < bank->instCount; i++) {
            scanInstrument(bank->instArray[i], i, 0, prefix);
        }

        // 2. NEW: Scan the Percussion instrument (Drums)
        if (bank->percussion) {
            scanInstrument(bank->percussion, 0, 1, prefix);
        }
    }
}

int extAudioDmaIntercept(uintptr_t offset, uintptr_t *out_ptr) {
    if (offset >= 0x40000000 && offset < 0x80000000) { *out_ptr = offset; return 1; }
    return 0;
}

int extAudioMixerPaint(uint16_t dest_addr, uintptr_t source_addr, uint16_t nbytes) {
    if (source_addr >= 0x40000000 && source_addr < 0x80000000) {
        for (int i = 0; i < nbytes; i++) if (dest_addr + i < 4096) g_HqFakeOffsets[dest_addr + i] = source_addr + i;
        return 1;
    } else {
        for (int i = 0; i < nbytes; i++) if (dest_addr + i < 4096) g_HqFakeOffsets[dest_addr + i] = 0;
        return 0;
    }
}

int extAudioMixerDecode(int nbytes, uint16_t inofs, void* dest_buf) {
    uintptr_t src = g_HqFakeOffsets[inofs];
    if (src >= 0x40000000 && src < 0x80000000) {
        struct HqMusic* snd = NULL;
        for (u32 i = 0; i < g_NumHqMusic; i++) {
            if (src >= g_HqMusic[i].token_start && src < g_HqMusic[i].token_end) { snd = &g_HqMusic[i]; break; }
        }
        if (snd && snd->pcm_data) {
            u32 pcm_cursor;
            
            if (snd->is_raw16) {
                pcm_cursor = src - snd->token_start;
            } else {
                // FIXED: Multiplication-first maps sub-frame cursors smoothly, completely removing step-jitter
                u32 adpcm_cursor = src - snd->token_start;
                pcm_cursor = (adpcm_cursor * 32) / 9;
            }
            
            u32 copy_bytes = nbytes;
            if (pcm_cursor >= snd->pcm_size) copy_bytes = 0;
            else if (pcm_cursor + nbytes > snd->pcm_size) copy_bytes = snd->pcm_size - pcm_cursor;
            
            if (copy_bytes > 0) memcpy(dest_buf, snd->pcm_data + pcm_cursor, copy_bytes);
            if (copy_bytes < (u32)nbytes) memset((u8*)dest_buf + copy_bytes, 0, nbytes - copy_bytes);
        } else memset(dest_buf, 0, nbytes);
        return 1; 
    }
    return 0; 
}

// -------------------------------------------------------------
// HIGH-FIDELITY HLE MUSIC MIXER
// -------------------------------------------------------------

// Clean out tracking arrays in initialization
void extAudioInitMusicVoices(void) {
    memset(g_MusicVoices, 0, sizeof(g_MusicVoices));
}

// 1. Intercept a Note On event from the N64 sequencer

int extAudioSeqVoiceStart(void *n64_voice, ALWaveTable *w, float pitch, int vol, int pan) {
    if (!g_ExtAudioEnabled || !n64_voice || !w) return 0;
    uintptr_t base_addr = (uintptr_t)w->base;
    if (base_addr < 0x40000000) return 0;

    struct HqMusic *snd = NULL;
    for (u32 i = 0; i < g_NumHqMusic; i++) {
        if (base_addr >= g_HqMusic[i].token_start && base_addr < g_HqMusic[i].token_end) {
            snd = &g_HqMusic[i]; break;
        }
    }
    if (!snd) return 0;

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (!g_MusicVoices[i].active || g_MusicVoices[i].raw_vol == 0) { 
            g_MusicVoices[i].n64_voice = n64_voice;
            g_MusicVoices[i].snd = snd;
            g_MusicVoices[i].cursor = 0.0f;
            g_MusicVoices[i].pitch = pitch;
            g_MusicVoices[i].raw_pan = pan;
            
            // NEW: Use the PRESERVED loop points for perfect sustains
            g_MusicVoices[i].has_loop = snd->has_loop;
            g_MusicVoices[i].loop_start = snd->loop_start;
            g_MusicVoices[i].loop_end = snd->loop_end;

            g_MusicVoices[i].active = 1;
            extAudioSeqVoiceSetVol(n64_voice, vol);
            return 1;
        }
    }
    return 0;
}

// 2. Intercept volume changes (fades, envelopes)
int extAudioSeqVoiceSetVol(void *n64_voice, int vol) {
    if (!g_ExtAudioEnabled || !n64_voice) return 0;

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            if (vol != -1) g_MusicVoices[i].raw_vol = vol;
            
            float vol_norm = (float)g_MusicVoices[i].raw_vol / 32767.0f;
            float pan_norm = (float)(g_MusicVoices[i].raw_pan & 0x7F) / 127.0f;
            
            g_MusicVoices[i].vol_l = vol_norm * cosf(pan_norm * 1.570796327f);
            g_MusicVoices[i].vol_r = vol_norm * sinf(pan_norm * 1.570796327f);
            return 1;
        }
    }
    return 0;
}

// 3. Intercept panning sweeps
int extAudioSeqVoiceSetPan(void *n64_voice, int pan) {
    if (!g_ExtAudioEnabled || !n64_voice) return 0;

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            if (pan != -1) g_MusicVoices[i].raw_pan = pan;
            
            float vol_norm = (float)g_MusicVoices[i].raw_vol / 32767.0f;
            float pan_norm = (float)(g_MusicVoices[i].raw_pan & 0x7F) / 127.0f;
            
            g_MusicVoices[i].vol_l = vol_norm * cosf(pan_norm * 1.570796327f);
            g_MusicVoices[i].vol_r = vol_norm * sinf(pan_norm * 1.570796327f);
            return 1;
        }
    }
    return 0;
}

// 4. Intercept pitch-bends and slides
int extAudioSeqVoiceSetPitch(void *n64_voice, float pitch) {
    if (!g_ExtAudioEnabled || !n64_voice) return 0;

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            if (pitch > 0.0f) g_MusicVoices[i].pitch = pitch;
            return 1;
        }
    }
    return 0;
}

// 5. Intercept Note Off/Release events
int extAudioSeqVoiceStop(void *n64_voice) {
    if (!g_ExtAudioEnabled || !n64_voice) return 0;

    for (int i = 0; i < MAX_MUSIC_VOICES; i++) {
        if (g_MusicVoices[i].active && g_MusicVoices[i].n64_voice == n64_voice) {
            // REMOVED: g_MusicVoices[i].active = 0; 
            // We now let the N64 envelope fade the volume to 0 naturally!
            return 1;
        }
    }
    return 0;
}

// -------------------------------------------------------------
// VOX HOOKS (Untouched - Dialogue / MP3)
// -------------------------------------------------------------
int extAudioCheckVox(const char* name, u8 **out_data, u32 *out_size) {
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;

    if (!name) return 0;
    int len = strlen(name);
    
    if (len > 0 && (name[len - 1] == 'M' || name[len - 1] == 'm')) {
        if (!g_ExtAudioEnabled) { return 0; }

        char filepath[256]; 
        snprintf(filepath, sizeof(filepath), "ext_vox/%s.mp3", name);
        
        u32 temp_size = 0;
        u8* temp_data = fsFileLoad(filepath, &temp_size);
        
        if (temp_data && temp_size > 0) {
            if (out_data) *out_data = temp_data;
            if (out_size) *out_size = temp_size;
            sysLogPrintf(LOG_NOTE, "EXT-AUDIO-VOX: Loaded EXTERNAL Dialogue -> [%s]", filepath);
            return 1; 
        } else {
            sysLogPrintf(LOG_WARNING, "EXT-AUDIO-VOX: Missing [%s]! Yielding to N64 ROM.", filepath);
            return 0;
        }
    } 
    return 0;
}

int extAudioVoxKeepAlive(const char* name, int source) {
    if (!name || source != 2) return 0;
    int len = strlen(name);
    if (len > 0 && (name[len - 1] == 'M' || name[len - 1] == 'm')) return 1;
    return 0;
}

// ============================================================================
// SELF-CONTAINED EXTENDED AUDIO OPTIONS MENU
// ============================================================================

static s32 g_MusicPlayerSelectedTrack = 0;

// Access to core game variables & functions
extern s32 g_MusicDisableMpDeath;
extern void musicPlayTrackIsolated(s32 tracknum);

// Standard localized N64 "Back" string ID
// #define L_OPTIONS_213 0x00d5 

static MenuItemHandlerResult menuhandlerExtAudioEnabled(s32 operation, struct menuitem *item, union handlerdata *data) {
	if (operation == MENUOP_GET) return extAudioGetEnabled();
	if (operation == MENUOP_SET) extAudioSetEnabled(data->checkbox.value);
	return 0;
}

static MenuItemHandlerResult menuhandlerDisableMpDeathMusic(s32 operation, struct menuitem *item, union handlerdata *data) {
	if (operation == MENUOP_GET) return g_MusicDisableMpDeath;
	if (operation == MENUOP_SET) g_MusicDisableMpDeath = data->checkbox.value;
	return 0;
}

static MenuItemHandlerResult menuhandlerMusicPlayerTrack(s32 operation, struct menuitem *item, union handlerdata *data) {
	if (operation == MENUOP_GETOPTIONCOUNT) {
		data->dropdown.value = ARRAYCOUNT(g_MusicTrackNames);
	} else if (operation == MENUOP_GETOPTIONTEXT) {
		return (intptr_t)g_MusicTrackNames[data->dropdown.value];
	} else if (operation == MENUOP_SET) {
		g_MusicPlayerSelectedTrack = data->dropdown.value;
	} else if (operation == MENUOP_GETSELECTEDINDEX) {
		data->dropdown.value = g_MusicPlayerSelectedTrack;
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerMusicPlayerPlay(s32 operation, struct menuitem *item, union handlerdata *data) {
	if (operation == MENUOP_SET) {
		// Play the mapped N64 track ID from the Lookup Table!
		musicPlayTrackIsolated(g_MusicTrackIDs[g_MusicPlayerSelectedTrack]);
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerMusicPlayerStop(s32 operation, struct menuitem *item, union handlerdata *data) {
	if (operation == MENUOP_SET) {
		musicPlayTrackIsolated(MUSIC_NONE);
	}
	return 0;
}

struct menuitem g_ExtendedAudioMenuItems[] = {
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Enable External Audio",
		0,
		menuhandlerExtAudioEnabled,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Disable MP Death Music",
		0,
		menuhandlerDisableMpDeathMusic,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Sound Test Music Player\n", // FIXED: Added "\n" to resolve vertical crushing!
		0,
		NULL,
	},
	{
		MENUITEMTYPE_DROPDOWN,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Track",
		0,
		menuhandlerMusicPlayerTrack,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Play Selected Track\n",
		0,
		menuhandlerMusicPlayerPlay,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Stop Music\n",
		0,
		menuhandlerMusicPlayerStop,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		L_OPTIONS_213, // Native "Back" ID (resolves scroll out of bounds!)
		0,
		NULL,
	},
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