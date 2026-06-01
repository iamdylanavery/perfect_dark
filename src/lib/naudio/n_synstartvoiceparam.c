#include "n_synthInternals.h"
#include <ultraerror.h>
#include <os_internal.h>

void n_alSynStartVoiceParams(N_ALVoice *v, ALWaveTable *w, f32 pitch, s16 vol,
		ALPan pan, u8 fxmix, u8 arg6, f32 arg7, u8 arg8, ALMicroTime t)
{
	ALStartParamAlt *update;

	#ifndef PLATFORM_N64
	// Intercept note startup and route it to our high-fidelity SDL2 voice mixer
	extern int extAudioSeqVoiceStart(void *n64_voice, ALWaveTable *w, float pitch, int vol, int pan);
	if (extAudioSeqVoiceStart(v, w, pitch, vol, pan)) {
		vol = 0; // Forces N64 native volume envelope to 0 (silence on RSP)!
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
		update->volume  = vol; // Will be 0 if hijacked
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