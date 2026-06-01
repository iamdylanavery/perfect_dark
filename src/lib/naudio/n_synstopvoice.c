#include <os_internal.h>
#include <ultraerror.h>
#include "n_synthInternals.h"

void n_alSynStopVoice(N_ALVoice *v)
{
	ALParam *update;
	ALFilter *f;

	#ifndef PLATFORM_N64
	// Free our active modern music voice
	extern int extAudioSeqVoiceStop(void *n64_voice);
	extAudioSeqVoiceStop(v);
	#endif

	if (v->pvoice) {
		update = __n_allocParam();
		ALFailIf(update == 0, ERR_ALSYN_NO_UPDATE);

		update->delta = n_syn->paramSamples + v->pvoice->offset;
		update->type = AL_FILTER_STOP_VOICE;
		update->next = 0;

		n_alEnvmixerParam(v->pvoice, AL_FILTER_ADD_UPDATE, update);
	}
}