/**
 * @file sensor_sampler.h
 *
 * Reads the sensors every couple of seconds on a thread of its own, so the
 * I2C bus never holds up the screen, and keeps the latest reading for
 * whoever polls: the air feed, on the LVGL thread.
 *
 * Plain C with no LVGL.
 */

#ifndef SENSOR_SAMPLER_H
#define SENSOR_SAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensors.h"

/*********************
 *      DEFINES
 *********************/

/** Between readings. The SEN69C has a new one every second. */
#define SENSOR_SAMPLE_MS 2000

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Start reading. Call once.
 * @return   false if the thread could not be started
 */
bool sensor_sampler_start(void);

/**
 * @param out     receives the latest reading, derived metrics included; all
 *                NAN before the first
 * @param parts   receives the sensor_hw_part_t bits of the parts that answered
 *                the latest reading; @nullable
 * @return        a count that goes up with every reading; 0 before the first
 */
uint32_t sensor_sampler_get(sensor_reading_t * out, uint32_t * parts);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SENSOR_SAMPLER_H*/
