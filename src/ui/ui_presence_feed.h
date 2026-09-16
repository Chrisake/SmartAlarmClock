/**
 * @file ui_presence_feed.h
 *
 * Wakes the screen for whoever is coming to the clock, two ways.
 *
 * Face wake runs presence/face_wake.h while the screen is idle and face wake
 * is on in the settings, and calls ui_wake() when it sees a face for the
 * settings' number of frames in a row. The camera is closed the rest of the
 * time.
 *
 * Movement wake runs presence/radar_wake.h whenever it is on in the settings,
 * idle or not, so that whoever is already in the room is part of what the
 * radar takes for granted by the time the screen goes idle; a wake raised
 * while the screen is in use is dropped. It also passes what the radar says
 * about itself, and about the light in the room, to the settings page.
 *
 * Either failing to start, and a radar that does not answer, is said once in a
 * notice.
 *
 * In the simulator, hold F in the window for a face, and R, S or A for someone
 * moving in front of the radar; see hal.c.
 */

#ifndef UI_PRESENCE_FEED_H
#define UI_PRESENCE_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** Start watching the idle state. Call once, after the settings feed. */
void ui_presence_feed_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_PRESENCE_FEED_H*/
