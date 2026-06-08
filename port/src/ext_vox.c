#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fs.h"
#include "system.h"
#include "platform.h"
#include "ext_audio.h"

extern int g_ExtAudioEnabled;
extern u16 g_SfxVolume;
float g_ExtVoxVolumeScale = 0.70f;

// Legacy Stubs
int extAudioCheckVox(const char* name, u8 **out_data, u32 *out_size) {
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    return 0; 
}
int extAudioVoxKeepAlive(const char* name, int source) { return 0; }

// -------------------------------------------------------------
// MODERN VOX SYNTHESIZER with Watchdog Timer
// -------------------------------------------------------------
#define MAX_VOX_VOICES 4
struct VoxVoice {
    void *n64_handle;
    char name[64]; // Track active name to handle consecutive lines
    u32 sample_rate;
    u32 sample_count;
    s16 *samples;
    float cursor;
    float pitch;
    float vol_l;
    float vol_r;
    int raw_vol;
    int raw_pan;
    int active;
};
static struct VoxVoice g_VoxVoices[MAX_VOX_VOICES];
static int g_VoxWatchdog = 0; // The frame watchdog

void extAudioVoxInit(void) {
    memset(g_VoxVoices, 0, sizeof(g_VoxVoices));
    g_VoxWatchdog = 0;
}

void extAudioVoxReset(void) {
    for (int i = 0; i < MAX_VOX_VOICES; i++) {
        if (g_VoxVoices[i].samples) free(g_VoxVoices[i].samples);
        g_VoxVoices[i].active = 0;
        g_VoxVoices[i].samples = NULL;
        g_VoxVoices[i].name[0] = '\0';
    }
    g_VoxWatchdog = 0;
}

int extAudioVoxStart(void *n64_handle, const char* name, int vol, int pan, float pitch) {
    if (!g_ExtAudioEnabled || !name) return 0;

    // Check if this handle is ALREADY playing this EXACT dialogue file
    for (int i = 0; i < MAX_VOX_VOICES; i++) {
        if (g_VoxVoices[i].active && g_VoxVoices[i].n64_handle == n64_handle) {
            if (strcmp(g_VoxVoices[i].name, name) == 0) {
                g_VoxWatchdog = 15; // Feed the watchdog
                return 1; 
            } else {
                // If the file changed, cleanly free the old one
                g_VoxVoices[i].active = 0;
                if (g_VoxVoices[i].samples) {
                    free(g_VoxVoices[i].samples);
                    g_VoxVoices[i].samples = NULL;
                }
            }
        }
    }
    
    // Allocate slot
    int slot = -1;
    for (int i = 0; i < MAX_VOX_VOICES; i++) {
        if (!g_VoxVoices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return 0; 

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "ext_vox/%s.wav", name); 

    u32 fileSize = 0;
    u8 *fileData = fsFileLoad(filepath, &fileSize);

    if (!fileData || fileSize < 44) {
        if (fileData) free(fileData);
        sysLogPrintf(LOG_WARNING, "EXT-AUDIO-VOX: Missing [%s]! Yielding to N64 ROM.", filepath);
        return 0; 
    }

    // Parse WAV
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
            free(fileData);

            if (g_VoxVoices[slot].samples) free(g_VoxVoices[slot].samples);
            
            g_VoxVoices[slot].n64_handle = n64_handle;
            strncpy(g_VoxVoices[slot].name, name, sizeof(g_VoxVoices[slot].name));
            g_VoxVoices[slot].sample_rate = sample_rate;
            g_VoxVoices[slot].sample_count = dataSize / 2;
            g_VoxVoices[slot].samples = pcmData;
            g_VoxVoices[slot].cursor = 0.0f;
            g_VoxVoices[slot].pitch = (pitch > 0.0f) ? pitch : 1.0f;
            g_VoxVoices[slot].raw_vol = (vol != -1) ? vol : 32767;
            g_VoxVoices[slot].raw_pan = (pan != -1) ? pan : 64;
            g_VoxVoices[slot].active = 1;

            g_VoxWatchdog = 15; // Initial feed
            extAudioVoxAdjust(n64_handle, vol, pan, pitch);
            sysLogPrintf(LOG_NOTE, "EXT-AUDIO-VOX: Playing EXTERNAL Dialogue -> [%s]", filepath);
            return 1;
        }
    }

    free(fileData);
    return 0;
}

int extAudioVoxAdjust(void *n64_handle, int vol, int pan, float pitch) {
    if (!n64_handle) return 0;
    for (int i = 0; i < MAX_VOX_VOICES; i++) {
        if (g_VoxVoices[i].active && g_VoxVoices[i].n64_handle == n64_handle) {
            g_VoxWatchdog = 15; // Keep dialogue alive!
            
            if (pitch != -1.0f) g_VoxVoices[i].pitch = pitch;
            if (vol != -1) g_VoxVoices[i].raw_vol = vol;
            if (pan != -1) g_VoxVoices[i].raw_pan = pan;
            
            float vol_norm = (float)g_VoxVoices[i].raw_vol / 32767.0f;
            float pan_norm = (float)(g_VoxVoices[i].raw_pan & 0x7F) / 127.0f;
            
            g_VoxVoices[i].vol_l = vol_norm * cosf(pan_norm * 1.570796327f);
            g_VoxVoices[i].vol_r = vol_norm * sinf(pan_norm * 1.570796327f);
            return 1;
        }
    }
    return 0;
}

int extAudioVoxStop(void *n64_handle) {
    if (!n64_handle) return 0;
    for (int i = 0; i < MAX_VOX_VOICES; i++) {
        if (g_VoxVoices[i].active && g_VoxVoices[i].n64_handle == n64_handle) {
            g_VoxVoices[i].active = 0;
            if (g_VoxVoices[i].samples) {
                free(g_VoxVoices[i].samples);
                g_VoxVoices[i].samples = NULL;
            }
            g_VoxVoices[i].name[0] = '\0';
            return 1;
        }
    }
    return 0;
}

void extAudioMixVox(s16 *mix_buffer, u32 num_frames, float crossfade_vol) {
    // Watchdog Tick: If the N64 engine stops sending stream frames, cleanly halt our WAV
    if (g_VoxWatchdog > 0) {
        g_VoxWatchdog--;
        if (g_VoxWatchdog == 0) {
            extAudioVoxReset(); 
            return;
        }
    }

    float master_sfx_vol = (float)g_SfxVolume / 20480.0f;
    if (master_sfx_vol > 1.0f) master_sfx_vol = 1.0f;

    for (int v = 0; v < MAX_VOX_VOICES; v++) {
        if (!g_VoxVoices[v].active || !g_VoxVoices[v].samples) continue;
        
        struct VoxVoice *voice = &g_VoxVoices[v];
        float cursor = voice->cursor;
        float base_step = (float)voice->sample_rate / 22050.0f; 
        float step = base_step * voice->pitch;
        
        for (u32 i = 0; i < num_frames; i++) {
            if ((u32)cursor >= voice->sample_count) {
                voice->active = 0;
                free(voice->samples);
                voice->samples = NULL;
                voice->name[0] = '\0';
                break;
            }
            
            s16 sample = voice->samples[(u32)cursor];
            cursor += step;
            
	    float scale_factor = g_ExtVoxVolumeScale * master_sfx_vol * crossfade_vol;

            s32 mix_l = mix_buffer[i*2] + (s32)(sample * voice->vol_l * scale_factor);
            if (mix_l > 32767) mix_l = 32767;
            if (mix_l < -32768) mix_l = -32768;
            mix_buffer[i*2] = (s16)mix_l;
            
            s32 mix_r = mix_buffer[i*2 + 1] + (s32)(sample * voice->vol_r * scale_factor);
            if (mix_r > 32767) mix_r = 32767;
            if (mix_r < -32768) mix_r = -32768;
            mix_buffer[i*2 + 1] = (s16)mix_r;
        }
        voice->cursor = cursor;
    }
}