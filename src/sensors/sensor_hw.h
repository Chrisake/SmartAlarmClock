/**
 * @file sensor_hw.h
 *
 * The sensor parts themselves. Two implementations, picked at build time:
 * sensor_hw_esp.c talks I2C to the SEN69C and SHT45 on the clock, and
 * sensor_hw_sim.c makes up a believable bedroom for the simulator.
 *
 * Blocking: a reading takes some tens of milliseconds on the bus, so it is
 * made on sensor_sampler's thread, never the LVGL one.
 */

#ifndef SENSOR_HW_H
#define SENSOR_HW_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensors.h"

/**********************
 *      TYPEDEFS
 **********************/

/** The parts, as bits. */
typedef enum {
    SENSOR_HW_AIR     = 1 << 0,   /**< SEN69C: particles, VOC, NOx, HCHO, CO2 */
    SENSOR_HW_CLIMATE = 1 << 1,   /**< SHT45: temperature, humidity */
} sensor_hw_part_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Read every part, starting any that is not running yet. Metrics from a part
 * that does not answer are left as they were, so clear `out` first.
 * @param out   receives the values
 * @return      the sensor_hw_part_t bits of the parts that answered
 */
uint32_t sensor_hw_read(sensor_reading_t * out);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SENSOR_HW_H*/
