/**
 * @file ui_weather_feed.h
 *
 * Fetches the forecasts from Open-Meteo (weather/open_meteo.h) and feeds them
 * to the weather page, the clock page's weather section, and -- through the
 * air feed -- the air quality page's forecast strip and outdoor readings.
 *
 * Where: the location in the settings or, when that is automatic, the public
 * IP's. When: at start, then every half hour for the weather and every hour
 * for the air quality; after a failure, again in a minute, then less and less
 * often; at once when a refresh button is tapped; and when the location or the
 * units change.
 *
 * Until the first forecast arrives the pages show a spinner, or why it could
 * not be fetched. After that a failed refresh leaves the last forecast up,
 * its "Updated" line marked, and says so in a notice.
 *
 * TODO: on the clock, hold off until Wi-Fi is up.
 */

#ifndef UI_WEATHER_FEED_H
#define UI_WEATHER_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"
#include "ui/ui_status.h"
#include "settings/settings.h"
#include "weather/open_meteo.h"

/**********************
 *      TYPEDEFS
 **********************/

/** The air quality forecast, and how it stands. */
typedef struct {
    ui_status_state_t        state;       /**< READY once there is a forecast, however old */
    const char *             error;       /**< Why the last attempt failed, if it did */
    bool                     stale;       /**< READY, but the last refresh failed */
    bool                     refreshing;  /**< READY, and fetching again */
    const char *             updated;     /**< e.g. "Updated 10:15 AM"; empty before the first */
    const open_meteo_air_t * data;        /**< NULL until the first forecast */
} ui_weather_feed_air_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** Start fetching. Call once, after ui_init() has built the pages. */
void ui_weather_feed_init(void);

/** Tell the pages again everything the feed last told them. For after ui_rebuild(). */
void ui_weather_feed_republish(void);

/**
 * Fetch afresh if the location or the units changed.
 * @param before   the settings as they were
 * @param after    the settings now in force
 */
void ui_weather_feed_settings_changed(const settings_t * before, const settings_t * after);

/** Fetch the air quality forecast now, as its refresh button asks. */
void ui_weather_feed_refresh_air(void);

/** @param out   receives the air quality forecast and how it stands; valid until the next forecast */
void ui_weather_feed_get_air(ui_weather_feed_air_t * out);

/** @return   the name of the place the forecasts are for, or NULL until it is known */
const char * ui_weather_feed_place(void);

/**
 * The outdoor temperature and humidity now, in the forecast's units.
 * @return   false until there is a forecast
 */
bool ui_weather_feed_outdoor_climate(float * temperature, float * humidity);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_WEATHER_FEED_H*/
