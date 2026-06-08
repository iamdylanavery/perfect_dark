#include "n_synthInternals.h"
#include <ultraerror.h>
#include <os_internal.h>

// Custom compliant structs to prevent compiler warnings
typedef struct {
    s32 order;
    s32 npredictors;
    s16 book[128]; // Heavily padded to prevent buffer overflows
} DummyADPCMBook;

typedef struct {
    u32 start;
    u32 end;
    u32 count;
    s16 state[16];
} DummyADPCMLoop;

static DummyADPCMBook g_SilentBook = {
    .order = 2,
    .npredictors = 1,
    .book = {0} // Zeroed prediction coefficients
};

static DummyADPCMLoop g_SilentLoop = {
    .start = 0,
    .end = 16,
    .count = 0xffffffff // Infinite loop
};

static u8 g_SilentADPCMBuffer[64] = {0}; // Pure zero residual ADPCM frames
static ALWaveTable g_SilentWaveTable = {
    .base = g_SilentADPCMBuffer,
    .len = 64,
    .type = AL_ADPCM_WAVE, // Configured as ADPCM to satisfy the DSP filter chain
    .flags = 0,
    .waveInfo = {
        .adpcmWave = {
            .book = (ALADPCMBook*)&g_SilentBook,
            .loop = (ALADPCMloop*)&g_SilentLoop
        }
    }
};

void n_alSynStartVoiceParams(N_ALVoice *v, ALWaveTable *w, f32 pitch, s16 vol,
		ALPan pan, u8 fxmix, u8 arg6, f32 arg7, u8 arg8, ALMicroTime t)
{
	ALStartParamAlt *update;

	#ifndef PLATFORM_N64
	extern int extAudioSeqVoiceStart(void *n64_voice, ALWaveTable *w, float pitch, int vol, int pan, int fxmix, u8 arg6, float arg7, u8 arg8, ALMicroTime t);
	if (extAudioSeqVoiceStart(v, w, pitch, vol, pan, fxmix, arg6, arg7, arg8, t)) {
		w = &g_SilentWaveTable;
	}
	#endif

	if (v->pvoice) {
		/*
		 * get new update struct from the free list
		 */
		update = (ALStartParamAlt *)__n_allocParam();
		ALFailIf(update == 0, ERR_ALSYN_NO_UPDATE);

		/*
		 * set offset and fxmix data
		 */
		update->delta   = n_syn->paramSamples + v->pvoice->offset;
		update->next    = 0;
		update->type    = AL_FILTER_START_VOICE_ALT;
		update->unity   = v->unityPitch;
		update->pan     = pan;
		update->volume  = vol; // Native volume passes safely to keep the state machine alive
		update->fxMix   = fxmix;
		update->pitch   = pitch;
		update->unk14   = arg8;
		update->unk15   = arg6;
		update->unk18   = arg7;
		update->samples = _n_timeToSamples(t);
		update->wave    = w;

		n_alEnvmixerParam(v->pvoice, AL_FILTER_ADD_UPDATE, update);
	}
}