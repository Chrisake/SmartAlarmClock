/**
 * @file radio_player.h
 *
 * Plays an internet radio stream: the same code in the simulator and on the
 * clock.
 *
 * It reads the stream on one background thread and decodes it on another,
 * with a quarter of a megabyte of compressed audio between them, so a
 * stuttering connection pauses once rather than breaking up. Decoded audio
 * goes to audio_sink.h, the only part that differs between the simulator
 * (SDL2) and the board (ES8311 codec).
 *
 * What it plays:
 *  - Icecast and Shoutcast streams of MP3 (minimp3) or AAC and HE-AAC (Helix),
 *    with the track titles those servers interleave (ICY metadata);
 *  - HLS: the playlist, a variant from a master playlist, and segments of
 *    MPEG-TS or packed audio carrying AAC or MP3. Encrypted HLS is not played.
 *
 * A dropped stream is reconnected a few times before giving up.
 *
 * Plain C with no LVGL: every call returns at once, and the UI reads what the
 * player is doing with radio_player_get_status().
 */

#ifndef RADIO_PLAYER_H
#define RADIO_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

#define RADIO_PLAYER_TITLE_LEN 128
#define RADIO_PLAYER_ERROR_LEN 64

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    RADIO_PLAYER_STOPPED,
    RADIO_PLAYER_CONNECTING,
    RADIO_PLAYER_BUFFERING,   /**< Connected, filling up before playing -- or after running dry */
    RADIO_PLAYER_PLAYING,
    RADIO_PLAYER_ERROR,       /**< Stopped, for the reason in `error` */
} radio_player_state_t;

typedef struct {
    radio_player_state_t state;
    char title[RADIO_PLAYER_TITLE_LEN];  /**< The stream's current track, if it says; else empty */
    char error[RADIO_PLAYER_ERROR_LEN];  /**< Why, in RADIO_PLAYER_ERROR */
} radio_player_status_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Set up the player and the audio output. Call once, from the main thread.
 * @return   false if there is no audio output; streams then end in an error
 */
bool radio_player_init(void);

/**
 * Play a stream, stopping whatever was playing.
 * @param url   the stream -- Radio Browser's url_resolved
 * @param hls   it is an HLS playlist -- Radio Browser's hls flag
 */
void radio_player_play(const char * url, bool hls);

/** Stop playing. */
void radio_player_stop(void);

/** @param percent   0..100 */
void radio_player_set_volume(int32_t percent);

/**
 * @param out   receives what the player is doing
 * @return      a number that changes whenever the status does, so a poller can
 *              skip redrawing when nothing happened
 */
uint32_t radio_player_get_status(radio_player_status_t * out);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*RADIO_PLAYER_H*/
