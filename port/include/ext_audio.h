#ifndef EXT_AUDIO_H
#define EXT_AUDIO_H

#include <ultra64.h>

// Struct for the multi-line stacked mixer HUD
struct HudMixerData {
    char line1[128]; // Header
    char line2[128]; // Instruments
    char line3[128]; // Volumes & Panning
    char line4[128]; // Loop & FX status
};

// Toggle System Functions
int extAudioGetEnabled(void);
void extAudioSetEnabled(int state);

void extAudioInit(void);
void extAudioResetCache(void);

// Diagnostic Control Flags
extern int g_ExtLogSfxEnabled;
extern int g_ExtLogMusicEnabled;

#ifndef PLATFORM_N64
void extAudioGetActiveInstrumentsStr(char *buffer, size_t max_len);
#endif

// Trojan Horse Music Hooks
void extAudioLoadWavetable(ALWaveTable *w, uintptr_t tbl_segment_start);
void extAudioScanSequenceBank(void *bankfile_ptr); // NEW: XBLA Sequence Scanner
void extAudioGetHudMixerData(struct HudMixerData *data); // NEW: Stacked HUD fetcher
int extAudioDmaIntercept(uintptr_t offset, uintptr_t *out_ptr);
int extAudioMixerPaint(uint16_t dest_addr, uintptr_t source_addr, uint16_t nbytes);
int extAudioMixerDecode(int nbytes, uint16_t inofs, void* dest_buf);

// VOX Hooks
int extAudioCheckVox(const char* name, u8 **out_data, u32 *out_size);
int extAudioVoxKeepAlive(const char* name, int source);
int extAudioVoxStart(void *n64_handle, const char* name, int vol, int pan, float pitch);
int extAudioVoxAdjust(void *n64_handle, int vol, int pan, float pitch);
int extAudioVoxStop(void *n64_handle);

// Modern SDL2 SFX Hooks
int extAudioModernStart(void *n64_handle, int sfx_id, int vol, int pan, float pitch);
int extAudioModernAdjust(void *n64_handle, int vol, int pan, float pitch);
s16* extAudioProcessSDL(const s16 *n64_buf, u32 num_bytes);

#ifndef PLATFORM_N64
// HLE Music Voice Hooks
int extAudioSeqVoiceStart(void *n64_voice, ALWaveTable *w, float pitch, int vol, int pan, int fxmix, s32 t);
int extAudioSeqVoiceSetVol(void *n64_voice, int vol, s32 t);
int extAudioSeqVoiceSetPan(void *n64_voice, int pan);
int extAudioSeqVoiceSetPitch(void *n64_voice, float pitch);
int extAudioSeqVoiceStop(void *n64_voice);
#endif

// Change these two lines:
void extAudioMixSFX(s16 *mix_buffer, u32 num_frames, float crossfade_vol);
void extAudioMixMusic(s16 *mix_buffer, u32 num_frames, float crossfade_vol);

// Robust WAV parsing and rate rounding helpers
int extAudioParseWavHeader(const u8* fileData, u32 fileSize, u32* out_sample_rate, u32* out_channels, u32* out_data_offset, u32* out_data_size);
u32 extAudioRoundToNearestStandardRate(u32 rate);

// --- Self-Contained Options Menu ---
struct menudialogdef;
extern struct menudialogdef g_ExtendedAudioMenuDialog;

#endif