/**
 * @file ui_presence_feed.h
 *
 * Wakes the screen for a face: runs presence/face_wake.h while the screen is
 * idle and face wake is on in the settings, and calls ui_wake() when it sees a
 * face for the settings' number of frames in a row. The camera is left
 * closed the rest of the time. If it cannot be started, a notice says so,
 * once.
 *
 * In the simulator, hold F in the window for a face.
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
