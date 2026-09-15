/**
 * @file sensors.h
 *
 * What the clock measures about the room: particles, gases and their indices
 * from a Sensirion SEN69C, temperature and humidity from an SHT45 -- the
 * SHT45's rather than the SEN69C's own, which sit by its fan and heater --
 * and the US EPA air quality index worked out from the particles.
 *
 * A reading holds every metric as a float, NAN where the value is not known:
 * a sensor that did not answer, or one still warming up.
 *
 * Plain C with no LVGL.
 */

#ifndef SENSORS_H
#define SENSORS_H

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

typedef enum {
    SENSOR_PM1,          /**< ug/m3 */
    SENSOR_PM25,
    SENSOR_PM4,
    SENSOR_PM10,
    SENSOR_VOC,          /**< Sensirion VOC index, 1..500; 100 is the room's usual */
    SENSOR_NOX,          /**< Sensirion NOx index, 1..500; 1 is the room's usual */
    SENSOR_HCHO,         /**< Formaldehyde, ppb */
    SENSOR_CO2,          /**< ppm */
    SENSOR_TEMPERATURE,  /**< Degrees C */
    SENSOR_HUMIDITY,     /**< Percent relative */
    SENSOR_AQI,          /**< US EPA index from PM2.5 and PM10 */
    SENSOR_METRIC_COUNT,
} sensor_metric_t;

typedef struct {
    float values[SENSOR_METRIC_COUNT];  /**< By sensor_metric_t; NAN where unknown */
} sensor_reading_t;

/** How a metric is named and sent. */
typedef struct {
    const char * key;           /**< In the MQTT payload, e.g. "pm2_5" */
    const char * name;          /**< e.g. "PM2.5" */
    const char * unit;          /**< UTF-8, e.g. "µg/m³"; NULL for an index */
    const char * device_class;  /**< Home Assistant's, or NULL if it has none */
    uint8_t      decimals;      /**< Worth sending */
} sensor_metric_info_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** Set every value of a reading to NAN. */
void sensor_reading_clear(sensor_reading_t * reading);

/** Fill in the metrics worked out from the others -- the air quality index. */
void sensor_reading_derive(sensor_reading_t * reading);

/** @return   how `metric` is named and sent */
const sensor_metric_info_t * sensor_metric_info(sensor_metric_t metric);

/**
 * The US EPA air quality index (2024 breakpoints) for particle concentrations:
 * the worse of the two pollutants' indices.
 * @param pm25   ug/m3, or NAN
 * @param pm10   ug/m3, or NAN
 * @return       0..500, or NAN if neither is known
 */
float sensor_aqi(float pm25, float pm10);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SENSORS_H*/
