/**
 * @file sensor_history.h
 *
 * The readings of the last hour, day and week, for the air quality page's
 * chart: SENSOR_HISTORY_POINTS averages over each window, every metric.
 *
 * Each window is cut into buckets of a fixed length -- 37.5 seconds for the
 * hour, 15 minutes for the day, 105 minutes for the week -- aligned to the
 * epoch, so they line up however often readings come. A reading adds to the
 * bucket it falls in; a bucket with no readings, as while the clock was off,
 * is a gap.
 *
 * Kept on the SD card, so a restart does not start the week over. Only the
 * finished buckets are stored: a restart loses at most the one being filled.
 *
 * Plain C with no LVGL. Not thread-safe: the air feed uses it from the LVGL
 * thread.
 */

#ifndef SENSOR_HISTORY_H
#define SENSOR_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensors.h"

#include <time.h>

/*********************
 *      DEFINES
 *********************/

/** Points per window: one per pixel column of the chart is plenty. */
#define SENSOR_HISTORY_POINTS 96

#if defined(ESP_PLATFORM)
  #define SENSOR_HISTORY_PATH "/sdcard/sensors/history.bin"
#else
  /** The simulator's SD card, as for the radio stations. */
  #define SENSOR_HISTORY_PATH "data/sdcard/sensors/history.bin"
#endif

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    SENSOR_RANGE_HOUR,
    SENSOR_RANGE_DAY,
    SENSOR_RANGE_WEEK,
    SENSOR_RANGE_COUNT,
} sensor_range_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** Empty every window. */
void sensor_history_init(void);

/**
 * Add a reading. Its NAN values add nothing.
 * @param now       when it was taken
 * @param reading   the reading
 * @return          true if a bucket finished in any window, so a chart moves on
 */
bool sensor_history_add(time_t now, const sensor_reading_t * reading);

/**
 * One metric over a window, oldest first. The last point is the bucket still
 * being filled, so the chart reaches the present.
 * @param range    which window
 * @param metric   which metric
 * @param out      receives SENSOR_HISTORY_POINTS values, NAN for gaps
 */
void sensor_history_get(sensor_range_t range, sensor_metric_t metric, float out[]);

/** @return   the window's length in seconds */
uint32_t sensor_history_window(sensor_range_t range);

/**
 * @param path   file to read
 * @return       false if there is none, or it is not a history this build wrote;
 *               the history is then empty
 */
bool sensor_history_load(const char * path);

/**
 * @param path   file to write; its directory is created
 * @return       false if it could not be written
 */
bool sensor_history_save(const char * path);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SENSOR_HISTORY_H*/
