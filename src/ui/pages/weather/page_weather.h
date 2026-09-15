/**
 * @file page_weather.h
 *
 * Weather page: current conditions, the next 20 hours, and the week ahead,
 * for the clock's own location or another city picked here.
 *
 * Layout:
 *
 *   +----------------------------------------------------------------+
 *   | (refresh) Updated 10:15       | FEELS LIKE HUMIDITY RAIN WIND   |
 *   | (icon) 24 deg Partly cloudy   |                                 |
 *   |               H 27  L 18      | SUNRISE    SUNSET   MOON NEW MOON|
 *   |               [Athens v]      |                                 |
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
 * The city's name under the conditions opens a list of the cities to pick
 * from, as the air quality page's sensor button does, once there is more than
 * one. While the page is covered -- a city still loading, or failing to -- the
 * same button stays up in the corner, so another city can still be picked.
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

/** Cities the page can be switched between, the clock's own included. */
#define PAGE_WEATHER_PLACES_MAX 8

/** Longest city name kept, in bytes with the terminator. */
#define PAGE_WEATHER_PLACE_LEN 48

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

/**
 * Called when another city is picked from the list.
 * @param index   into the names given to page_weather_set_places()
 */
typedef void (*page_weather_place_cb_t)(uint32_t index);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_weather_desc(void);

/**
 * Say how fresh the forecast is, in the card's top left corner beside the
 * refresh button.
 * @param updated    e.g. "Updated 10:15 AM", or NULL for nothing
 * @param stale      the last refresh failed: "Updated" is drawn as a warning
 */
void page_weather_set_updated(const char * updated, bool stale);

/**
 * The cities to pick from, and the one the forecast is for.
 * @param names      the clock's own location first; copied
 * @param count      clamped to PAGE_WEATHER_PLACES_MAX; with one, it is only named
 * @param selected   index of the one shown
 */
void page_weather_set_places(const char * const names[], uint32_t count, uint32_t selected);

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
 * forecast up: say so with page_weather_set_updated()'s `stale`.
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

/**
 * @param cb   called when another city is picked; NULL to clear
 */
void page_weather_set_place_cb(page_weather_place_cb_t cb);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_WEATHER_H*/
