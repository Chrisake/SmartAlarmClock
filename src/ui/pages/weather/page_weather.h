/**
 * @file page_weather.h
 *
 * Weather page: current conditions, the next 24 hours, and the week ahead.
 *
 * Layout:
 *
 *   +----------------------------------------------------------------+
 *   | (icon)  24 deg  Partly cloudy   |  FEELS LIKE  HUMIDITY  RAIN  |
 *   |                 H 27  L 18      |  WIND        SUNRISE   SUNSET|
 *   |                 Athens . 10:15  |                              |
 *   +----------------------------------------------------------------+
 *   |   2 PM     6 PM     10 PM    2 AM     6 AM     10 AM           |
 *   |   (icon)   (icon)   (icon)   (icon)   (icon)   (icon)          |
 *   |   26       23       20       18       17       22              |
 *   |   10%      60%      30%      0%       10%      0%              |
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
#include "ui/ui_weather_icon.h"

/*********************
 *      DEFINES
 *********************/

/** Slots in the 24-hour strip, one every four hours. */
#define PAGE_WEATHER_HOURS 6

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

/** One slot in the 24-hour strip. */
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

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_weather_desc(void);

/**
 * Set the line naming where the forecast is for and how fresh it is.
 * @param location   e.g. "Athens"
 * @param updated    e.g. "Updated 10:15 AM", or NULL to show only the location
 */
void page_weather_set_location(const char * location, const char * updated);

/**
 * Fill the current-conditions card. Strings are copied.
 * @param now   the conditions; NULL does nothing
 */
void page_weather_set_now(const page_weather_now_t * now);

/**
 * Fill the 24-hour strip. Slots past `count` are hidden and the rest spread
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

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_WEATHER_H*/
