/**
 * @file open_meteo.h
 *
 * The weather and air quality forecasts, from Open-Meteo
 * (https://open-meteo.com): free for non-commercial use, and needing no key.
 * Where the clock is, when the settings leave that automatic, comes from the
 * public IP's location, from ip-api.com.
 *
 * Builds the request URLs and reads the answers into plain structs. Times are
 * asked for as Unix time, so they are shown in the clock's own zone with
 * clock_time_local(); the forecast's days still run midnight to midnight
 * where the forecast is for.
 *
 * Plain C with no LVGL: the answers are read on http_worker's threads.
 */

#ifndef OPEN_METEO_H
#define OPEN_METEO_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*********************
 *      DEFINES
 *********************/

/** Hourly forecast kept: the hour under way, then a day ahead. */
#define OPEN_METEO_HOURS 25

/** Daily forecast kept, today first. */
#define OPEN_METEO_DAYS 7

/** Days the forecast is asked for, today included: as far as Open-Meteo reaches. */
#define OPEN_METEO_FORECAST_DAYS 16

/** Days before today the daily data is also asked for: more than a lunation, so the last new moon is among them. */
#define OPEN_METEO_MOON_PAST_DAYS 31

/** Days of moon phase kept: the past days, then the forecast's. */
#define OPEN_METEO_MOON_DAYS (OPEN_METEO_MOON_PAST_DAYS + OPEN_METEO_FORECAST_DAYS)

/** Quarter-hourly precipitation kept: the quarter under way, then two hours and a quarter ahead. */
#define OPEN_METEO_QUARTERS 10

/** Hourly air quality kept: the past week, today and tomorrow. */
#define OPEN_METEO_AIR_HOURS (9 * 24)

/** Cities a search by name gives at most, best match first. */
#define OPEN_METEO_SEARCH_RESULTS 5

/** The public IP's location. Plain HTTP: ip-api.com's free tier has no HTTPS. */
#define OPEN_METEO_IP_LOCATION_URL "http://ip-api.com/json/?fields=status,message,city,lat,lon"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    time_t time;
    float  temperature;
    float  rain_chance;   /**< Percent */
    int    code;          /**< WMO weather code; -1 if unknown */
    bool   is_day;
} open_meteo_hour_t;

typedef struct {
    time_t date;          /**< The midnight it starts, where the forecast is for */
    float  high;
    float  low;
    float  rain_chance;   /**< Percent */
    int    code;
    time_t sunrise;
    time_t sunset;
} open_meteo_day_t;

typedef struct {
    time_t time;           /**< Start of the quarter hour */
    float  precipitation;  /**< mm over the quarter hour */
} open_meteo_quarter_t;

typedef struct {
    time_t date;    /**< The midnight the day starts, where the forecast is for */
    float  phase;   /**< 0..1 through the lunation: 0 new, 0.5 full; NAN if not given */
} open_meteo_moon_t;

/** Temperatures and wind in the units asked for. NAN where not given. */
typedef struct {
    struct {
        float temperature;
        float feels_like;
        float dew_point;        /**< The temperature the air would have to cool to for dew */
        float humidity;         /**< Percent */
        float wind_speed;
        float wind_direction;   /**< Degrees the wind comes from */
        int   code;
        bool  is_day;
    } now;

    open_meteo_hour_t    hours[OPEN_METEO_HOURS];
    uint32_t             hour_count;
    open_meteo_day_t     days[OPEN_METEO_DAYS];
    uint32_t             day_count;
    open_meteo_quarter_t quarters[OPEN_METEO_QUARTERS];
    uint32_t             quarter_count;
    open_meteo_moon_t    moon[OPEN_METEO_MOON_DAYS];   /**< From OPEN_METEO_MOON_PAST_DAYS before today */
    uint32_t             moon_count;
} open_meteo_forecast_t;

typedef struct {
    time_t time;
    float  aqi;    /**< US AQI */
    float  pm25;   /**< ug/m3 */
    float  pm10;
} open_meteo_air_hour_t;

typedef struct {
    float                 aqi;   /**< Now */
    float                 pm25;
    float                 pm10;
    open_meteo_air_hour_t hours[OPEN_METEO_AIR_HOURS];
    uint32_t              hour_count;
} open_meteo_air_t;

typedef struct {
    char   name[48];     /**< The city */
    char   region[64];   /**< Where it is, e.g. "Île-de-France, France"; empty from the IP's location */
    double latitude;
    double longitude;
} open_meteo_place_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @param fahrenheit   temperatures in F and wind in mph, rather than C and km/h
 * @return             false if the URL did not fit
 */
bool open_meteo_forecast_url(double latitude, double longitude, bool fahrenheit, char * buf, size_t size);

/** @return   false if the URL did not fit */
bool open_meteo_air_url(double latitude, double longitude, char * buf, size_t size);

/** @return   false if the answer is not a forecast */
bool open_meteo_parse_forecast(const char * json, size_t len, open_meteo_forecast_t * out);

/** @return   false if the answer is not an air quality forecast */
bool open_meteo_parse_air(const char * json, size_t len, open_meteo_air_t * out);

/**
 * Read ip-api.com's answer to OPEN_METEO_IP_LOCATION_URL.
 * @return   false if it found no location
 */
bool open_meteo_parse_place(const char * json, size_t len, open_meteo_place_t * out);

/**
 * Search Open-Meteo's geocoding for the cities with a name, in any script.
 * @param name   what was typed, e.g. "Paris"; percent-encoded here
 * @return       false if the URL did not fit
 */
bool open_meteo_search_url(const char * name, char * buf, size_t size);

/**
 * Read the answer to open_meteo_search_url(). Names are cut short, if need
 * be, between characters rather than inside one.
 * @param out   receives the cities, best match first
 * @param max   room in `out`
 * @return      cities found, 0 for none; -1 if the answer is not a search's
 */
int32_t open_meteo_parse_search(const char * json, size_t len, open_meteo_place_t out[], uint32_t max);

/** @return   what a WMO weather code means, e.g. "Partly cloudy"; "" if unknown */
const char * open_meteo_code_text(int code);

/** @return   the compass point the wind comes from, e.g. "NE"; "" if NAN */
const char * open_meteo_compass(float degrees);

/**
 * The first new moon at or after a moment, from the daily phases, each taken to
 * stand for the middle of its day. A new moon falls between the two days where
 * the phase wraps round, placed between their middles in proportion. One
 * further off than the forecast reaches is the last before the moment plus a
 * mean lunation, which the real one keeps within some hours of.
 * @param forecast   the forecast
 * @param from       e.g. the midnight starting today, so a new moon earlier today counts
 * @return           the moment; 0 if the forecast has no phases
 */
time_t open_meteo_next_new_moon(const open_meteo_forecast_t * forecast, time_t from);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*OPEN_METEO_H*/
