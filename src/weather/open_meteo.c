/**
 * @file open_meteo.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "weather/open_meteo.h"

#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define FORECAST_API "https://api.open-meteo.com/v1/forecast"
#define AIR_API      "https://air-quality-api.open-meteo.com/v1/air-quality"

/** The mean length of a lunation, in seconds. */
#define SYNODIC_SECONDS (29.530588861 * 86400.0)

#define HALF_DAY_SECONDS 43200

/**********************
 *  STATIC PROTOTYPES
 **********************/

static const cJSON * member(const cJSON * object, const char * key);
static float         number(const cJSON * item);
static uint32_t      numbers(const cJSON * array, float out[], uint32_t max);
static uint32_t      instants(const cJSON * array, time_t out[], uint32_t max);
static int           code_of(float value);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool open_meteo_forecast_url(double latitude, double longitude, bool fahrenheit, char * buf, size_t size)
{
    int n = snprintf(buf, size,
                     FORECAST_API "?latitude=%.4f&longitude=%.4f"
                     "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,"
                     "wind_speed_10m,wind_direction_10m"
                     "&hourly=temperature_2m,precipitation_probability,weather_code,is_day"
                     "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,"
                     "precipitation_probability_max,moon_phase"
                     "&minutely_15=precipitation"
                     "&past_hours=1&forecast_hours=%d&past_minutely_15=1&forecast_minutely_15=%d"
                     "&past_days=%d&forecast_days=%d"
                     "&timezone=auto&timeformat=unixtime%s",
                     latitude, longitude, OPEN_METEO_HOURS - 1, OPEN_METEO_QUARTERS - 1, OPEN_METEO_MOON_PAST_DAYS,
                     OPEN_METEO_FORECAST_DAYS,
                     fahrenheit ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : "");

    return n > 0 && (size_t)n < size;
}

bool open_meteo_air_url(double latitude, double longitude, char * buf, size_t size)
{
    int n = snprintf(buf, size,
                     AIR_API "?latitude=%.4f&longitude=%.4f"
                     "&current=us_aqi,pm2_5,pm10&hourly=us_aqi,pm2_5,pm10"
                     "&past_days=7&forecast_days=2&timezone=auto&timeformat=unixtime",
                     latitude, longitude);

    return n > 0 && (size_t)n < size;
}

bool open_meteo_parse_forecast(const char * json, size_t len, open_meteo_forecast_t * out)
{
    memset(out, 0, sizeof(*out));

    cJSON *       root     = cJSON_ParseWithLength(json, len);
    const cJSON * current  = member(root, "current");
    const cJSON * hourly   = member(root, "hourly");
    const cJSON * daily    = member(root, "daily");
    const cJSON * quarters = member(root, "minutely_15");

    if(!current || !hourly || !daily) {
        cJSON_Delete(root);
        return false;
    }

    out->now.temperature    = number(member(current, "temperature_2m"));
    out->now.feels_like     = number(member(current, "apparent_temperature"));
    out->now.humidity       = number(member(current, "relative_humidity_2m"));
    out->now.wind_speed     = number(member(current, "wind_speed_10m"));
    out->now.wind_direction = number(member(current, "wind_direction_10m"));
    out->now.code           = code_of(number(member(current, "weather_code")));
    out->now.is_day         = number(member(current, "is_day")) == 1.0f;

    {
        time_t   when[OPEN_METEO_HOURS];
        float    temperature[OPEN_METEO_HOURS], chance[OPEN_METEO_HOURS];
        float    code[OPEN_METEO_HOURS], day[OPEN_METEO_HOURS];
        uint32_t n = instants(member(hourly, "time"), when, OPEN_METEO_HOURS);

        numbers(member(hourly, "temperature_2m"), temperature, OPEN_METEO_HOURS);
        numbers(member(hourly, "precipitation_probability"), chance, OPEN_METEO_HOURS);
        numbers(member(hourly, "weather_code"), code, OPEN_METEO_HOURS);
        numbers(member(hourly, "is_day"), day, OPEN_METEO_HOURS);

        for(uint32_t i = 0; i < n; i++) {
            out->hours[i].time        = when[i];
            out->hours[i].temperature = temperature[i];
            out->hours[i].rain_chance = chance[i];
            out->hours[i].code        = code_of(code[i]);
            out->hours[i].is_day      = day[i] == 1.0f;
        }
        out->hour_count = n;
    }

    {
        time_t   when[OPEN_METEO_MOON_DAYS], sunrise[OPEN_METEO_MOON_DAYS], sunset[OPEN_METEO_MOON_DAYS];
        float    high[OPEN_METEO_MOON_DAYS], low[OPEN_METEO_MOON_DAYS], chance[OPEN_METEO_MOON_DAYS];
        float    code[OPEN_METEO_MOON_DAYS], phase[OPEN_METEO_MOON_DAYS];
        uint32_t n = instants(member(daily, "time"), when, OPEN_METEO_MOON_DAYS);

        memset(sunrise, 0, sizeof(sunrise));
        memset(sunset, 0, sizeof(sunset));
        instants(member(daily, "sunrise"), sunrise, OPEN_METEO_MOON_DAYS);
        instants(member(daily, "sunset"), sunset, OPEN_METEO_MOON_DAYS);
        numbers(member(daily, "temperature_2m_max"), high, OPEN_METEO_MOON_DAYS);
        numbers(member(daily, "temperature_2m_min"), low, OPEN_METEO_MOON_DAYS);
        numbers(member(daily, "precipitation_probability_max"), chance, OPEN_METEO_MOON_DAYS);
        numbers(member(daily, "weather_code"), code, OPEN_METEO_MOON_DAYS);
        numbers(member(daily, "moon_phase"), phase, OPEN_METEO_MOON_DAYS);

        /*The moon's phase every day, the past ones too, to find the last new moon in.*/
        for(uint32_t i = 0; i < n; i++) {
            out->moon[i].date  = when[i];
            out->moon[i].phase = phase[i];
        }
        out->moon_count = n;

        /*The forecast proper starts today, after the past days asked for.*/
        for(uint32_t i = OPEN_METEO_MOON_PAST_DAYS; i < n && out->day_count < OPEN_METEO_DAYS; i++) {
            open_meteo_day_t * day = &out->days[out->day_count++];

            day->date        = when[i];
            day->high        = high[i];
            day->low         = low[i];
            day->rain_chance = chance[i];
            day->code        = code_of(code[i]);
            day->sunrise     = sunrise[i];
            day->sunset      = sunset[i];
        }
    }

    if(quarters) {
        time_t   when[OPEN_METEO_QUARTERS];
        float    amount[OPEN_METEO_QUARTERS];
        uint32_t n = instants(member(quarters, "time"), when, OPEN_METEO_QUARTERS);

        numbers(member(quarters, "precipitation"), amount, OPEN_METEO_QUARTERS);

        for(uint32_t i = 0; i < n; i++) {
            out->quarters[i].time          = when[i];
            out->quarters[i].precipitation = amount[i];
        }
        out->quarter_count = n;
    }

    cJSON_Delete(root);
    return out->hour_count > 0 && out->day_count > 0;
}

bool open_meteo_parse_air(const char * json, size_t len, open_meteo_air_t * out)
{
    memset(out, 0, sizeof(*out));

    cJSON *       root    = cJSON_ParseWithLength(json, len);
    const cJSON * current = member(root, "current");
    const cJSON * hourly  = member(root, "hourly");

    if(!current || !hourly) {
        cJSON_Delete(root);
        return false;
    }

    out->aqi  = number(member(current, "us_aqi"));
    out->pm25 = number(member(current, "pm2_5"));
    out->pm10 = number(member(current, "pm10"));

    /*A few kilobytes: the worker threads have the stack for it.*/
    time_t   when[OPEN_METEO_AIR_HOURS];
    float    aqi[OPEN_METEO_AIR_HOURS], pm25[OPEN_METEO_AIR_HOURS], pm10[OPEN_METEO_AIR_HOURS];
    uint32_t n = instants(member(hourly, "time"), when, OPEN_METEO_AIR_HOURS);

    numbers(member(hourly, "us_aqi"), aqi, OPEN_METEO_AIR_HOURS);
    numbers(member(hourly, "pm2_5"), pm25, OPEN_METEO_AIR_HOURS);
    numbers(member(hourly, "pm10"), pm10, OPEN_METEO_AIR_HOURS);

    for(uint32_t i = 0; i < n; i++) {
        out->hours[i].time = when[i];
        out->hours[i].aqi  = aqi[i];
        out->hours[i].pm25 = pm25[i];
        out->hours[i].pm10 = pm10[i];
    }
    out->hour_count = n;

    cJSON_Delete(root);
    return n > 0;
}

bool open_meteo_parse_place(const char * json, size_t len, open_meteo_place_t * out)
{
    memset(out, 0, sizeof(*out));

    cJSON *       root   = cJSON_ParseWithLength(json, len);
    const cJSON * status = member(root, "status");
    const cJSON * lat    = member(root, "lat");
    const cJSON * lon    = member(root, "lon");
    const cJSON * city   = member(root, "city");

    bool found = cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0 && cJSON_IsNumber(lat) &&
                 cJSON_IsNumber(lon);

    if(found) {
        out->latitude  = lat->valuedouble;
        out->longitude = lon->valuedouble;
        if(cJSON_IsString(city)) snprintf(out->name, sizeof(out->name), "%s", city->valuestring);
    }

    cJSON_Delete(root);
    return found;
}

const char * open_meteo_code_text(int code)
{
    switch(code) {
        case 0:  return "Clear";
        case 1:  return "Mostly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: return "Fog";
        case 48: return "Freezing fog";
        case 51:
        case 53:
        case 55: return "Drizzle";
        case 56:
        case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66:
        case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light showers";
        case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85:
        case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96:
        case 99: return "Thunderstorm with hail";
        default: return "";
    }
}

const char * open_meteo_compass(float degrees)
{
    static const char * const points[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

    if(isnan(degrees)) return "";

    int index = (int)floorf(fmodf(degrees + 22.5f, 360.0f) / 45.0f);
    if(index < 0) index += 8;
    return points[index % 8];
}

time_t open_meteo_next_new_moon(const open_meteo_forecast_t * forecast, time_t from)
{
    const open_meteo_moon_t * moon   = forecast->moon;
    int64_t                   last   = -1;
    time_t                    before = 0;   /**< The last new moon found before `from` */

    for(uint32_t i = 0; i < forecast->moon_count; i++) {
        if(isnan(moon[i].phase)) continue;

        if(last >= 0 && moon[i].phase < moon[last].phase) {
            /*The phase wrapped round since the day before: place the new moon
             *between the two middles, in proportion.*/
            double to_new  = 1.0 - moon[last].phase;
            double past    = moon[i].phase;
            double span    = (double)(moon[i].date - moon[last].date);
            time_t new_moon = moon[last].date + HALF_DAY_SECONDS + (time_t)(span * to_new / (to_new + past));

            if(new_moon >= from) return new_moon;
            before = new_moon;
        }

        last = (int64_t)i;
    }

    /*Further off than the forecast reaches: a mean lunation on from the last one.*/
    if(before) {
        time_t new_moon = before;
        while(new_moon < from) new_moon += (time_t)SYNODIC_SECONDS;
        return new_moon;
    }

    /*No new moon in a window longer than a lunation: only with gaps in the
     *data. On from the last day, at the mean pace.*/
    if(last < 0) return 0;
    return moon[last].date + HALF_DAY_SECONDS + (time_t)((1.0 - moon[last].phase) * SYNODIC_SECONDS);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static const cJSON * member(const cJSON * object, const char * key)
{
    return cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
}

static float number(const cJSON * item)
{
    return cJSON_IsNumber(item) ? (float)item->valuedouble : NAN;
}

/** Copy an array of numbers. Nulls, and entries past its end, read as NAN. @return entries in it, up to `max` */
static uint32_t numbers(const cJSON * array, float out[], uint32_t max)
{
    const cJSON * item;
    uint32_t      n = 0;

    for(uint32_t i = 0; i < max; i++) out[i] = NAN;
    if(!cJSON_IsArray(array)) return 0;

    cJSON_ArrayForEach(item, array) {
        if(n == max) break;
        out[n++] = number(item);
    }
    return n;
}

/** Unix times, kept out of floats, which would round them to minutes. */
static uint32_t instants(const cJSON * array, time_t out[], uint32_t max)
{
    const cJSON * item;
    uint32_t      n = 0;

    if(!cJSON_IsArray(array)) return 0;

    cJSON_ArrayForEach(item, array) {
        if(n == max) break;
        out[n++] = cJSON_IsNumber(item) ? (time_t)item->valuedouble : 0;
    }
    return n;
}

static int code_of(float value)
{
    return isnan(value) ? -1 : (int)value;
}