/**
 * @file page_weather.h
 *
 * Weather page: current conditions, the next 20 hours, and the week ahead.
 *
 * Layout:
 *
 *   +----------------------------------------------------------------+
 *   |                                        Updated 10:15 (refresh) |
 *   | (icon) 24 deg Partly cloudy | FEELS LIKE HUMIDITY RAIN WIND    |
 *   |               H 27  L 18    | SUNRISE    SUNSET   MOON NEW MOON|
 *   |               Athens        |                                  |
 *   +----------------------------------------------------------------+
 *   |  2 PM  4 PM  6 PM  8 PM  10 PM  12 AM  2 AM  4 AM  6 AM  8 AM  |
 *   |  (ic)  (ic)  (ic)  (ic)  (ic)   (ic)   (ic)  (ic)  (ic)  (ic)  |
 *   |  26    25    23    21    20     19     18    17    17    19    |
 *   |  10%   20%   60%   40%   30%    10%    0%    0%    10%   0%    |
 *   +----------------------------------------------------------------+
 *   |  [Today]   Tue      Wed      Thu      Fri      Sat      Sun    |
 *   |  (icon)%   (icon)%  ...                                        |
 *   |   27       22       21       23       26       29       25     |
 *   |    #        |        |        #        #        #        #     |
 *   |    #        #        #        #        #        #        #     |
 *   |   18       17       16       16       17       19       18     |
 *   +----------------------------------------------------------------+
 *
 * Each day's low and high are joined by a vertical bar, and every bar in the
 * week is drawn on the same scale -- the coldest low at the bottom of the
 * track, the warmest high at the top -- so warmer and cooler days stand out
 * at a glance. The bar's colour runs from cool to warm over that same scale.
 *
 * The first day is taken to be today and is highlighted.
 *
 * Temperatures are whole degrees in whatever unit the weather service uses;
 * the page only appends a degree sign.
 *
 * Tapping the weather section of the clock page opens this page.
 */

#ifndef PAGE_WEATHER_H
#define PAGE_WEATHER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_page.h"
#include "ui/ui_status.h"
#include "ui/ui_weather_icon.h"

/*********************
 *      DEFINES
 *********************/

/** Slots in the hourly strip, one every two hours: the next 20 hours. */
#define PAGE_WEATHER_HOURS 10

/** Days in the weekly forecast, today included. */
#define PAGE_WEATHER_DAYS 7

/**********************
 *      TYPEDEFS
 **********************/

/** Current conditions, for the card across the top. */
typedef struct {
    ui_weather_t condition;
    const char * summary;      /**< e.g. "Partly cloudy" */
    int32_t      temp;
    int32_t      feels_like;
    int32_t      high;         /**< Today's forecast high */
    int32_t      low;          /**< Today's forecast low */
    int32_t      humidity;     /**< Percent */
    int32_t      rain_chance;  /**< Percent */
    const char * wind;         /**< e.g. "14 km/h N" */
    const char * sunrise;      /**< e.g. "7:04 AM" */
    const char * sunset;       /**< e.g. "7:31 PM" */
} page_weather_now_t;

/** One slot in the hourly strip. */
typedef struct {
    const char * when;         /**< e.g. "2 PM" */
    ui_weather_t condition;    /**< Use the _NIGHT variants after dark */
    int32_t      temp;
    int32_t      rain_chance;  /**< Percent */
} page_weather_hour_t;

/** One day in the weekly forecast. */
typedef struct {
    const char * day;          /**< e.g. "Today", "Tue" */
    ui_weather_t condition;
    int32_t      low;
    int32_t      high;
    int32_t      rain_chance;  /**< Percent */
} page_weather_day_t;

/** The moon, for the current-conditions card. */
typedef struct {
    float        age;           /**< 0..1 through the cycle: 0 new, 0.5 full; draws the icon */
    bool         southern;      /**< Seen from south of the equator: lit from the other side */
    int32_t      illumination;  /**< Percent of the disc lit; negative when unknown, which hides the icon and arrow */
    bool         waxing;        /**< More of it lit in the days ahead: a green arrow up, otherwise a red one down */
    const char * new_moon;      /**< When the next new moon is, e.g. "Tomorrow" or "Sat 10 Oct" */
} page_weather_moon_t;

/** Called when the refresh button, or Try again, is tapped. */
typedef void (*page_weather_refresh_cb_t)(void);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_weather_desc(void);

/**
 * Say where the forecast is for, under the conditions, and how fresh it is, in
 * the card's top right corner beside the refresh button.
 * @param location   e.g. "Athens"
 * @param updated    e.g. "Updated 10:15 AM", or NULL for nothing
 * @param stale      the last refresh failed: "Updated" is drawn as a warning
 */
void page_weather_set_location(const char * location, const char * updated, bool stale);

/**
 * Fill the current-conditions card. Strings are copied.
 * @param now   the conditions; NULL does nothing
 */
void page_weather_set_now(const page_weather_now_t * now);

/**
 * Fill the hourly strip. Slots past `count` are hidden and the rest spread
 * out to fill the card. Strings are copied.
 * @param hours   array of slots, earliest first
 * @param count   number of entries, clamped to PAGE_WEATHER_HOURS
 */
void page_weather_set_hourly(const page_weather_hour_t hours[], uint32_t count);

/**
 * Fill the weekly forecast. The range bars are rescaled to the coldest low
 * and warmest high among the entries given. Days past `count` are hidden.
 * Strings are copied.
 * @param days    array of days, today first
 * @param count   number of entries, clamped to PAGE_WEATHER_DAYS
 */
void page_weather_set_daily(const page_weather_day_t days[], uint32_t count);

/**
 * Fill the moon's readouts: the moon drawn as it looks, how much of it is lit
 * and whether that is growing, and the next new moon. Strings are copied.
 * @param moon   the moon; NULL does nothing
 */
void page_weather_set_moon(const page_weather_moon_t * moon);

/**
 * Say whether there is a forecast to show. Until the first one arrives the
 * page is covered: a spinner while it loads, or why it could not be fetched
 * with a button to try again. Once READY, a failed refresh keeps the old
 * forecast up: say so with page_weather_set_location()'s `stale`.
 * @param state     loading, ready or failed
 * @param message   why, when failed
 */
void page_weather_set_state(ui_status_state_t state, const char * message);

/**
 * @param refreshing   true while fetching: the refresh button turns into a spinner
 */
void page_weather_set_refreshing(bool refreshing);

/**
 * @param cb   called when the refresh button or Try again is tapped; NULL to clear
 */
void page_weather_set_refresh_cb(page_weather_refresh_cb_t cb);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_WEATHER_H*/
