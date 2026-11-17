#include <PR/ultratypes.h>
#include <stdio.h>
#include <SDL.h>
#include "platform.h"
#include "config.h"
#include "audio.h"
#include "system.h"

#include "ext_audio.h"

static SDL_AudioDeviceID dev;
static const s16 *nextBuf;
static u32 nextSize = 0;

static s32 bufferSize = 512;
static s32 queueLimit = 8192;

s32 audioInit(void)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		sysLogPrintf(LOG_ERROR, "SDL audio init error: %s", SDL_GetError());
		return -1;
	}

	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = 48000; // Upgraded from N64 22020Hz
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	// Scale the 22020Hz config bufferSize (default 512) up to 1024 for 48000Hz (~21.3ms)
	want.samples = (bufferSize <= 512) ? 1024 : bufferSize;
	want.callback = NULL;

	nextBuf = NULL;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0) {
		sysLogPrintf(LOG_ERROR, "SDL_OpenAudio error: %s", SDL_GetError());
		return -1;
	}

	SDL_PauseAudioDevice(dev, 0);

	return 0;
}

s32 audioGetBytesBuffered(void)
{
	// Convert 48000Hz SDL queue bytes back to N64 22020Hz equivalent bytes
	// so osAiGetLength(), audiomgr, and queueLimit see the exact timing they expect!
	u32 sdlBytes = SDL_GetQueuedAudioSize(dev);
	u32 n64Bytes = (u32)(((u64)sdlBytes * 22020ULL) / 48000ULL);
	return (s32)(n64Bytes & ~3U);
}

s32 audioGetSamplesBuffered(void)
{
	return audioGetBytesBuffered() / 4;
}

void audioSetNextBuffer(const s16 *buf, u32 len)
{
	nextBuf = buf;
	nextSize = len;
}

void audioEndFrame(void)
{
	if (nextBuf && nextSize) {
		if (audioGetSamplesBuffered() < queueLimit) {
#ifndef PLATFORM_N64
			u32 outBytes = 0;
			s16 *finalBuf = extAudioProcessSDL(nextBuf, nextSize, &outBytes);
			if (outBytes > 0) {
				SDL_QueueAudio(dev, finalBuf, outBytes);
			}
#else
			SDL_QueueAudio(dev, nextBuf, nextSize);
#endif
		}
		nextBuf = NULL;
		nextSize = 0;
	}
}

PD_CONSTRUCTOR static void audioConfigInit(void)
{
	configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
	configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);
}