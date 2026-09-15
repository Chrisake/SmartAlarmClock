/**
 * @file face_detector_sim.c
 *
 * The simulator has no camera. A face is "seen" in every frame while it is
 * told there is one; hal.c tells it while F is held down in the window.
 */

#if !defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "presence/face_detector.h"

/**********************
 *  STATIC VARIABLES
 **********************/

/*Written on the LVGL thread, read on face_wake's. One flag, set whole: a
 *frame that reads it a moment late is no harm.*/
static volatile bool present;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool face_detector_open(void)
{
    return true;
}

bool face_detector_detect(bool * face)
{
    *face = present;
    return true;
}

void face_detector_close(void)
{
}

void face_detector_sim_set_present(bool value)
{
    present = value;
}

#endif /*!defined(ESP_PLATFORM)*/
