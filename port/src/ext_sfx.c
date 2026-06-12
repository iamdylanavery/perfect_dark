#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fs.h"
#include "system.h"
#include "platform.h"
#include "ext_audio.h"
#include "sfx_strings.h"

extern int g_ExtAudioEnabled;
extern u16 g_SfxVolume;
extern s16 g_MixBuffer[8192 * 2];

// Volume scaling configuration for modern SFX samples
#define EXT_AUDIO_VOLUME_SCALE 0.4f 

// -------------------------------------------------------------
// EASTER EGG CONFIGURATION (The Wilhelm Scream)
// -------------------------------------------------------------
#define SURPRISE_SCREAM_CHANCE 100 // 1 in 100 chance
#define SURPRISE_SFX_SLOT      1999 // Dedicated cache slot 
#define GUARD_DEATH_SFX_MIN    133
#define GUARD_DEATH_SFX_MAX    157

// -------------------------------------------------------------
// SFX CACHE MODULE
// -------------------------------------------------------------
struct CachedSfx {
    int state; 
    u32 sample_rate;
    u32 channels; // NEW: Track Mono/Stereo
    u32 sample_count;
    s16 *samples;
};
static struct CachedSfx g_SfxCache[2000];

#define MAX_MODERN_VOICES 64 
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
static struct ModernVoice g_ModernVoices[MAX_MODERN_VOICES];

void extAudioSFXInit(void) {
    memset(g_SfxCache, 0, sizeof(g_SfxCache));
    memset(g_ModernVoices, 0, sizeof(g_ModernVoices));
}

void extAudioSFXReset(void) {
    for (u32 i = 0; i < 2000; i++) {
        if (g_SfxCache[i].state == 1) free(g_SfxCache[i].samples);
        g_SfxCache[i].state = 0;
    }
    memset(g_ModernVoices, 0, sizeof(g_ModernVoices));
}

static void getSfxFilename(int sfx_id, char *out_str, size_t max_len) {
    int bank_index = sfx_id - 1;
    if (bank_index >= 0) {
        snprintf(out_str, max_len, "sfx_%04d.wav", bank_index);
    } else {
        snprintf(out_str, max_len, "N/A");
    }
}

// --- Lightweight Base64 Decoder for Easter Egg ---
static u8* extAudioDecodeBase64(const char* input, u32 in_len, u32* out_len) {
    if (in_len % 4 != 0) return NULL; 

    u32 pad = 0;
    if (in_len > 0 && input[in_len - 1] == '=') pad++;
    if (in_len > 1 && input[in_len - 2] == '=') pad++;

    *out_len = (in_len / 4) * 3 - pad;
    u8* out = malloc(*out_len);
    if (!out) return NULL;

    u32 i = 0, j = 0;
    while (i < in_len) {
        u32 b[4];
        for (int k = 0; k < 4; k++) {
            char c = input[i++];
            if (c >= 'A' && c <= 'Z') b[k] = c - 'A';
            else if (c >= 'a' && c <= 'z') b[k] = c - 'a' + 26;
            else if (c >= '0' && c <= '9') b[k] = c - '0' + 52;
            else if (c == '+') b[k] = 62;
            else if (c == '/') b[k] = 63;
            else b[k] = 0; 
        }
        
        out[j++] = (b[0] << 2) | (b[1] >> 4);
        if (j < *out_len) out[j++] = (b[1] << 4) | (b[2] >> 2);
        if (j < *out_len) out[j++] = (b[2] << 6) | b[3];
    }
    return out;
}

static void loadSfxFromDisk(int sfx_id) {
    char filepath[256];
    int bank_index = sfx_id - 1;
    
    // NEW: Get the human-readable name!
    const char* internal_name = (sfx_id == SURPRISE_SFX_SLOT) ? "SFX_WILHELM_SCREAM" : sfx_names[bank_index];

    char filename[32];
    getSfxFilename(sfx_id, filename, sizeof(filename));
    const char* prefix = g_ExtAudioEnabled ? "EXT-AUDIO" : "N64-AUDIO";

    if (bank_index < 0) {
        g_SfxCache[sfx_id].state = -1;
        return;
    }

    snprintf(filepath, sizeof(filepath), "ext_sfx/sfx_%04d.wav", bank_index);
    
    // Updated log to include the Internal Name
        if (g_ExtLogSfxEnabled) {
    sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | Name: %-24s | File: %-16s | Loading...", 
                 prefix, "[DISK LOAD]", sfx_id, internal_name, filename);
	}
    u32 fileSize = 0;
    u8 *fileData = fsFileLoad(filepath, &fileSize);
    
    if (!fileData || fileSize < 44) {
        g_SfxCache[sfx_id].state = -1; 
        if (fileData) free(fileData);
        return;
    }

    u32 sample_rate = 22050;
    u32 channels = 1;      // 1. DECLARE THIS LOCAL VARIABLE!
    u32 data_offset = 0;
    u32 data_size = 0;

    // 2. PASS &channels AS THE 4th ARGUMENT (expected 6 arguments total)
    if (extAudioParseWavHeader(fileData, fileSize, &sample_rate, &channels, &data_offset, &data_size)) {
        s16 *pcmData = malloc(data_size);
        if (pcmData) {
            memcpy(pcmData, fileData + data_offset, data_size);
            g_SfxCache[sfx_id].sample_rate = sample_rate;
            g_SfxCache[sfx_id].channels = channels; // 3. This error will now vanish!
            g_SfxCache[sfx_id].sample_count = data_size / 2;
            g_SfxCache[sfx_id].samples = pcmData;
            g_SfxCache[sfx_id].state = 1;
        }
    } else {
        g_SfxCache[sfx_id].state = -1;
    }

    free(fileData);
}

static int loadSurpriseScream(void) {
    u32 fileSize = 0;
    u8 *fileData = fsFileLoad("ext_sfx/surprise.json", &fileSize);
    
    if (!fileData || fileSize == 0) {
        g_SfxCache[SURPRISE_SFX_SLOT].state = -1;
        if (fileData) free(fileData);
        return 0;
    }

    // Parse JSON for payload
    char* jsonStr = malloc(fileSize + 1);
    memcpy(jsonStr, fileData, fileSize);
    jsonStr[fileSize] = '\0';
    free(fileData); // Free original buffer

    char* payload_start = strstr(jsonStr, "\"payload\": \"");
    if (!payload_start) {
        free(jsonStr);
        g_SfxCache[SURPRISE_SFX_SLOT].state = -1;
        return 0;
    }

    payload_start += 12;
    char* payload_end = strchr(payload_start, '"');
    if (payload_end) *payload_end = '\0';

    // Decode base64 to binary WAV
    u32 decodedSize = 0;
    u8* decodedWav = extAudioDecodeBase64(payload_start, strlen(payload_start), &decodedSize);
    free(jsonStr); // Free the json string

    if (!decodedWav || decodedSize < 44) {
        if (decodedWav) free(decodedWav);
        g_SfxCache[SURPRISE_SFX_SLOT].state = -1;
        return 0;
    }

    // Parse the decoded WAV sitting in memory!
    u32 sample_rate = 22050;
    u32 channels = 1;
    u32 data_offset = 0;
    u32 data_size = 0;

    if (extAudioParseWavHeader(decodedWav, decodedSize, &sample_rate, &channels, &data_offset, &data_size)) {
        s16 *pcmData = malloc(data_size);
        if (pcmData) {
            memcpy(pcmData, decodedWav + data_offset, data_size);
            g_SfxCache[SURPRISE_SFX_SLOT].sample_rate = sample_rate;
            g_SfxCache[SURPRISE_SFX_SLOT].channels = channels;
            g_SfxCache[SURPRISE_SFX_SLOT].sample_count = data_size / 2;
            g_SfxCache[SURPRISE_SFX_SLOT].samples = pcmData;
            g_SfxCache[SURPRISE_SFX_SLOT].state = 1;
            
            free(decodedWav); // Clean up the raw WAV buffer
            return 1;
        }
    }
    
    g_SfxCache[SURPRISE_SFX_SLOT].state = -1;
    free(decodedWav);
    return 0;
}

int extAudioModernStart(void *n64_handle, int sfx_id, int vol, int pan, float pitch) {
    // 1. Determine names, logging prefix, and CALCULATE FILENAME immediately
    extern const char* sfx_names[];
    const char* internal_name = (sfx_id > 0 && sfx_id <= 2000) ? sfx_names[sfx_id - 1] : "SFX_UNKNOWN";
    const char* prefix = g_ExtAudioEnabled ? "EXT-AUDIO" : "N64-AUDIO";
    
    char filename[32];
    getSfxFilename(sfx_id, filename, sizeof(filename));

    // 2. RESTORE: "N64-AUDIO | [FALLBACK]" (Omit file name for native ROM sounds)
    if (!g_ExtAudioEnabled) {
        if (g_ExtLogSfxEnabled) {
            sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | Name: %-24s | Yielding to ROM.", 
                         prefix, "[FALLBACK]", sfx_id, internal_name);
        }
        return 0;
    }

    // 3. RESTORE: "EXT-AUDIO | [REQUEST]" (Includes BOTH internal name and file name)
    if (g_ExtLogSfxEnabled) {
        sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | Name: %-24s | File: %s", 
                     prefix, "[REQUEST]", sfx_id, internal_name, filename);
    }

    // 4. --- EASTER EGG SHIELD ---
    extern int g_ExtEasterEggChance;
    int is_guard_death = (sfx_id >= GUARD_DEATH_SFX_MIN && sfx_id <= GUARD_DEATH_SFX_MAX);
    if (is_guard_death && g_ExtEasterEggChance > 0) {
        if ((rand() % 100) < g_ExtEasterEggChance) {
            if (g_SfxCache[SURPRISE_SFX_SLOT].state == 0) loadSurpriseScream();
            if (g_SfxCache[SURPRISE_SFX_SLOT].state == 1) {
                sfx_id = SURPRISE_SFX_SLOT; 
                internal_name = "SFX_WILHELM_SCREAM";
                strcpy(filename, "surprise.json"); // Update filename for the surprise!
                if (g_ExtLogSfxEnabled) {
                    sysLogPrintf(LOG_NOTE, "EXT-AUDIO  | [SURPRISE] | *** WILHELM SCREAM TRIGGERED! ***");
                }
            }
        }
    }

    // 5. Cache Check
    if (g_SfxCache[sfx_id].state == 0) loadSfxFromDisk(sfx_id);
    
    if (g_SfxCache[sfx_id].state != 1) {
        if (g_ExtLogSfxEnabled) {
            sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | Name: %-24s | Missing [%s].", 
                         prefix, "[FALLBACK]", sfx_id, internal_name, filename);
        }
        return 0;
    }

    // 6. Voice Allocation
    for (int i = 0; i < MAX_MODERN_VOICES; i++) {
        if (!g_ModernVoices[i].active) {
            g_ModernVoices[i].n64_handle = n64_handle;
            g_ModernVoices[i].sfx_id = sfx_id;
            g_ModernVoices[i].cursor = 0.0f;
            g_ModernVoices[i].pitch = (pitch > 0) ? pitch : 1.0f;
            g_ModernVoices[i].raw_vol = (vol != -1) ? vol : 32767;
            g_ModernVoices[i].raw_pan = (pan != -1) ? pan : 64;
            g_ModernVoices[i].active = 1; 

            float vol_norm = (float)g_ModernVoices[i].raw_vol / 32767.0f;
            float pan_norm = (float)(g_ModernVoices[i].raw_pan & 0x7F) / 127.0f;
            g_ModernVoices[i].vol_l = vol_norm * cosf(pan_norm * 1.570796327f);
            g_ModernVoices[i].vol_r = vol_norm * sinf(pan_norm * 1.570796327f);
            
            // Log Success (Includes BOTH names and the parsed Rate)
            if (g_ExtLogSfxEnabled) {
                sysLogPrintf(LOG_NOTE, "%-10s | %-12s | ID: %4d | Name: %-24s | File: %s | Rate: %uHz", 
                             prefix, "[SUCCESS]", sfx_id, internal_name, filename, g_SfxCache[sfx_id].sample_rate);
            }
            
            return 1;
        }
    }
    return 0;
}

int extAudioModernAdjust(void *n64_handle, int vol, int pan, float pitch) {
    // if (!g_ExtAudioEnabled) return 0;

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

// SFX sub-mix engine called natively from our master processing loop
void extAudioMixSFX(s16 *mix_buffer, u32 num_frames, float crossfade_vol) {
    float master_sfx_vol = (float)g_SfxVolume / 20480.0f;
    if (master_sfx_vol > 1.0f) master_sfx_vol = 1.0f;

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
        
        // Count actual audio frames (total samples / channels)
        u32 total_frames = cache->sample_count / cache->channels;

        for (u32 i = 0; i < num_frames; i++) {
            if ((u32)cursor >= total_frames) {
                voice->active = 0;
                break;
            }
            
            // DYNAMIC CHANNEL DESERIALIZATION
            s16 sample_l, sample_r;
            if (cache->channels == 2) {
                sample_l = cache->samples[(u32)cursor * 2];
                sample_r = cache->samples[(u32)cursor * 2 + 1];
            } else {
                sample_l = cache->samples[(u32)cursor];
                sample_r = sample_l; // Duplicate Mono
            }
            
            cursor += step;
            
            float scale_factor = EXT_AUDIO_VOLUME_SCALE * master_sfx_vol * crossfade_vol;

            s32 mix_l = mix_buffer[i*2] + (s32)(sample_l * voice->vol_l * scale_factor);
            if (mix_l > 32767) mix_l = 32767;
            if (mix_l < -32768) mix_l = -32768;
            mix_buffer[i*2] = (s16)mix_l;
            
            s32 mix_r = mix_buffer[i*2 + 1] + (s32)(sample_r * voice->vol_r * scale_factor);
            if (mix_r > 32767) mix_r = 32767;
            if (mix_r < -32768) mix_r = -32768;
            mix_buffer[i*2 + 1] = (s16)mix_r;
        }
        voice->cursor = cursor;
    }
}