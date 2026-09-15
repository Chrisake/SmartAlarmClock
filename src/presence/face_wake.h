/**
 * @file face_wake.h
 *
 * Wakes the screen for someone looking at it. While started, a thread of its
 * own looks at a camera frame some 5 or 10 times a second through
 * face_detector.h. A face in a number of frames in a row -- 3 to 7, so a face
 * passing through the picture for a moment does not count -- raises a wake,
 * which the UI takes with face_wake_take().
 *
 * The UI only starts it while the screen is idle, when there is something to
 * wake; the camera is closed the rest of the time.
 *
 * Plain C with no LVGL; every call returns at once.
 */

#ifndef FACE_WAKE_H
#define FACE_WAKE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Start looking, or carry on with new settings if already looking.
 * @param fps      frames looked at a second, at most: fewer when the model is slower than that
 * @param frames   frames in a row that must hold a face
 * @return         false if the thread could not be started
 */
bool face_wake_start(uint8_t fps, uint8_t frames);

/** Stop looking and close the camera. A wake not yet taken is dropped. */
void face_wake_stop(void);

/** @return   true while started */
bool face_wake_is_running(void);

/** @return   true, once, when a face has been held for enough frames since the last call */
bool face_wake_take(void);

/** @return   true if the camera or the model could not be started on the last attempt */
bool face_wake_failed(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*FACE_WAKE_H*/
