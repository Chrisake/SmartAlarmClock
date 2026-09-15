/**
 * @file audio_sink_sdl.c
 *
 * The simulator's audio output, through SDL2's queued audio.
 */

#if !defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "audio/audio_sink.h"

#include <SDL.h>
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/

/** Frames scaled for volume at a time, on the stack. */
#define BLOCK_FRAMES 1024

/**********************
 *  STATIC VARIABLES
 **********************/

static SDL_AudioDeviceID device;
static uint32_t          rate;
static uint8_t           channels;
static SDL_atomic_t      volume;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool audio_sink_init(void)
{
    SDL_AtomicSet(&volume, 100);

    if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "audio_sink: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

bool audio_sink_configure(uint32_t sample_rate, uint8_t channel_count)
{
    if(device && rate == sample_rate && channels == channel_count) return true;
    if(channel_count == 0 || channel_count > 2) return false;

    if(device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = (int)sample_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = channel_count;
    want.samples  = 1024;

    /*No allowed changes: SDL converts whatever the device wants from this.*/
    device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if(!device) {
        fprintf(stderr, "audio_sink: %s\n", SDL_GetError());
        return false;
    }

    rate     = sample_rate;
    channels = channel_count;    SDL_PauseAudioDevice(device, 0);
    return true;
}

void audio_sink_write(const int16_t * pcm, size_t frames)
{
    int16_t block[BLOCK_FRAMES * 2];

    if(!device) return;

    /*Loudness is heard roughly as the square of the amplitude, so square the
     *slider's percentage too, or the top half of its travel does little.*/
    int32_t percent = SDL_AtomicGet(&volume);
    int32_t gain    = percent * percent;

    while(frames > 0) {
        size_t n       = frames < BLOCK_FRAMES ? frames : BLOCK_FRAMES;
        size_t samples = n * channels;

        for(size_t i = 0; i < samples; i++) block[i] = (int16_t)(pcm[i] * gain / 10000);
        SDL_QueueAudio(device, block, (Uint32)(samples * sizeof(int16_t)));

        pcm += samples;
        frames -= n;
    }
}

uint32_t audio_sink_queued_ms(void)
{
    if(!device || rate == 0) return 0;
    return (uint32_t)((uint64_t)SDL_GetQueuedAudioSize(device) * 1000u / ((uint64_t)rate * channels * 2u));
}

void audio_sink_clear(void)
{
    if(device) SDL_ClearQueuedAudio(device);
}

void audio_sink_set_volume(int32_t percent)
{
    SDL_AtomicSet(&volume, percent < 0 ? 0 : percent > 100 ? 100 : percent);
}

#endif
