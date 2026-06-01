#ifndef EXT_AUDIO_H
#define EXT_AUDIO_H

#include <ultra64.h>

// Toggle System Functions
int extAudioGetEnabled(void);
void extAudioSetEnabled(int state);

void extAudioInit(void);
void extAudioResetCache(void);

// Trojan Horse Music Hooks
void extAudioLoadWavetable(ALWaveTable *w, uintptr_t tbl_segment_start);
void extAudioScanSequenceBank(void *bankfile_ptr); // NEW: XBLA Sequence Scanner
int extAudioDmaIntercept(uintptr_t offset, uintptr_t *out_ptr);
int extAudioMixerPaint(uint16_t dest_addr, uintptr_t source_addr, uint16_t nbytes);
int extAudioMixerDecode(int nbytes, uint16_t inofs, void* dest_buf);

// VOX Hooks
int extAudioCheckVox(const char* name, u8 **out_data, u32 *out_size);
int extAudioVoxKeepAlive(const char* name, int source);

// Modern SDL2 SFX Hooks
int extAudioModernStart(void *n64_handle, int sfx_id, int vol, int pan, float pitch);
int extAudioModernAdjust(void *n64_handle, int vol, int pan, float pitch);
s16* extAudioProcessSDL(const s16 *n64_buf, u32 num_bytes);

#ifndef PLATFORM_N64
// HLE Music Voice Hooks
int extAudioSeqVoiceStart(void *n64_voice, ALWaveTable *w, float pitch, int vol, int pan);
int extAudioSeqVoiceSetVol(void *n64_voice, int vol);
int extAudioSeqVoiceSetPan(void *n64_voice, int pan);
int extAudioSeqVoiceSetPitch(void *n64_voice, float pitch);
int extAudioSeqVoiceStop(void *n64_voice);
#endif

// --- Self-Contained Options Menu ---
struct menudialogdef;
extern struct menudialogdef g_ExtendedAudioMenuDialog;

#endif