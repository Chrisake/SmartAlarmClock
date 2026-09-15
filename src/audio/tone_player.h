/**
 * @file tone_player.h
 *
 * The alarm tones, synthesised: nothing to store, nothing to decode, and the
 * same sound in the simulator as on the clock.
 *
 * A tone repeats until stopped, on a thread of its own that writes to
 * audio_sink.h. The sink takes one writer at a time, so stop the radio player
 * before starting a tone, and stop the tone before playing the radio. Volume
 * is the sink's, set with audio_sink_set_volume().
 *
 * Plain C with no LVGL: every call returns at once.
 */

#ifndef TONE_PLAYER_H
#define TONE_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

/**********************
 *      TYPEDEFS
 **********************/

/** The tones, in the order the alarm editor lists them. */
typedef enum {
    TONE_PLAYER_RADAR,     /**< Four quick pips, then a rest */
    TONE_PLAYER_CHIMES,    /**< A bell arpeggio */
    TONE_PLAYER_BEACON,    /**< Two soft notes, rising */
    TONE_PLAYER_SIGNAL,    /**< The classic digital alarm clock */
    TONE_PLAYER_BIRDSONG,  /**< Chirps */
    TONE_PLAYER_COUNT,
} tone_player_tone_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Start a tone, replacing any tone playing.
 * @param tone   a tone_player_tone_t; out of range plays the first
 * @return       false if it could not be started
 */
bool tone_player_start(uint8_t tone);

/** Stop the tone, if one is playing. */
void tone_player_stop(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*TONE_PLAYER_H*/
