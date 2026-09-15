/**
 * @file ui_air_feed.h
 *
 * Feeds the air quality page and the clock page's air section from the
 * clock's own sensors and from the outdoor forecast, and sends the readings
 * to the MQTT broker.
 *
 * Indoor: sensor_sampler reads the SEN69C and SHT45 every two seconds. Each
 * reading goes to the tiles, into sensor_history for the chart's hour, day and
 * week -- kept on the SD card, so a restart does not lose them -- and,
 * averaged over the settings' publish interval, to the broker as one JSON
 * message. With discovery on, each metric is announced to Home Assistant,
 * retained, whenever the broker connection comes up.
 *
 * Outdoor: the second entry in the sensor selector, named after the place,
 * from the Open-Meteo air quality forecast the weather feed fetches -- PM2.5,
 * PM10 and the index, hourly over the past week -- with the temperature and
 * humidity from the weather forecast. The same forecast fills the forecast
 * strip, whose refresh button asks the weather feed to fetch it again.
 */

#ifndef UI_AIR_FEED_H
#define UI_AIR_FEED_H

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

/**
 * Start the sensors and feeding the pages. Call once, after ui_init() has
 * built the pages, and before the broker connection is started.
 */
void ui_air_feed_init(void);

/** Tell the pages again everything the feed last told them. For after ui_rebuild(). */
void ui_air_feed_republish(void);

/** The weather feed's air quality forecast, place or outdoor climate changed. */
void ui_air_feed_outdoor_changed(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_AIR_FEED_H*/
