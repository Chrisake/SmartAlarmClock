/**
 * @file face_detector.h
 *
 * Whether there is a face in front of the clock, one camera frame at a time.
 * Two implementations, picked at build time: face_detector_esp.cpp runs a
 * small YOLO face model with ESP-DL on frames from the board's MIPI-CSI
 * camera; face_detector_sim.c has no camera and reports a face while the
 * simulator is told there is one -- the F key, held in its window.
 *
 * Blocking: on the clock a frame and its inference take tens of milliseconds,
 * so these are only ever called from face_wake's thread.
 */

#ifndef FACE_DETECTOR_H
#define FACE_DETECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Start the camera and load the model, if they are not running already.
 * @return   false if either could not be
 */
bool face_detector_open(void);

/**
 * Look at the next frame.
 * @param face   receives true if it holds a face
 * @return       false if no frame could be had
 */
bool face_detector_detect(bool * face);

/** Stop the camera and free the model. Safe when not open. */
void face_detector_close(void);

#if !defined(ESP_PLATFORM)
/** The simulator's stand-in for a camera: whether a face is in front of it. */
void face_detector_sim_set_present(bool present);
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*FACE_DETECTOR_H*/
