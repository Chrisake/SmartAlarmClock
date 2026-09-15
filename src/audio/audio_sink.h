/**
 * @file audio_sink.h
 *
 * Where decoded audio goes: 16-bit PCM in, sound out.
 *
 * Two implementations, picked at build time: audio_sink_sdl.c plays through
 * SDL2 in the simulator; audio_sink_esp.c plays through the board's ES8311
 * codec and speaker amplifier on the clock. The radio player calls these and
 * nothing platform-specific, which is what lets it run unchanged on both.
 *
 * The player serialises its calls: configure and write come from one thread
 * at a time. clear and set_volume may come from another.
 */

#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Bring up the audio hardware. Call once, from the main thread.
 * @return   false if there is no audio output
 */
bool audio_sink_init(void);

/**
 * Get ready for PCM in this format. Cheap when nothing changed, so call it
 * before every write.
 * @param sample_rate   Hz
 * @param channels      1 or 2, interleaved
 * @return              false if the format cannot be played
 */
bool audio_sink_configure(uint32_t sample_rate, uint8_t channels);

/**
 * Play PCM in the configured format. Returns quickly: in the simulator it
 * queues; on the clock it blocks only until the I2S buffers take it.
 * @param pcm      interleaved samples
 * @param frames   samples per channel
 */
void audio_sink_write(const int16_t * pcm, size_t frames);

/**
 * @return   milliseconds of audio written but not heard yet, so the caller can
 *           pace itself; 0 where the write itself paces
 */
uint32_t audio_sink_queued_ms(void);

/** Drop whatever is queued, as on a change of station. */
void audio_sink_clear(void);

/** @param percent   0..100 */
void audio_sink_set_volume(int32_t percent);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*AUDIO_SINK_H*/
