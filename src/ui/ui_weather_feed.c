/**
 * @file ui_weather_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_weather_feed.h"
#include "ui/ui_air_feed.h"
#include "ui/ui_format.h"
#include "ui/ui_theme.h"
#include "ui/pages/clock/page_clock.h"
#include "ui/pages/weather/page_weather.h"
#include "net/http_worker.h"
#include "settings/clock_time.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define TICK_MS            1000
#define FORECAST_EVERY_MS  (30UL * 60 * 1000)
#define AIR_EVERY_MS       (60UL * 60 * 1000)

/** The rain band and the next hours move on with the clock between fetches. */
#define REDRAW_EVERY_MS    (2UL * 60 * 1000)

/** After a failure: a minute, doubling each time, up to a quarter of an hour. */
#define RETRY_FIRST_MS     (60UL * 1000)
#define RETRY_MAX_MS       (15UL * 60 * 1000)

#define FORECAST_MAX_BYTES (256 * 1024)
#define PLACE_MAX_BYTES    (4 * 1024)
#define URL_LEN            768

/** Rates in mm/h at which each intensity of the rain band begins. */
#define RAIN_LIGHT_MM_H    0.1f
#define RAIN_MODERATE_MM_H 2.5f
#define RAIN_HEAVY_MM_H    7.6f
#define RAIN_EXTREME_MM_H  50.0f

/**********************
 *      TYPEDEFS
 **********************/

/** One thing fetched: where the clock is, the forecast, or the air quality. */
typedef struct {
    bool     busy;
    bool     failed;       /**< The last attempt failed */
    bool     asked;        /**< A tap asked for it: jump the queue, and say if it fails */
    uint32_t since;        /**< lv_tick the wait counts from */
    uint32_t wait;         /**< How long until the next attempt */
    uint32_t retry;        /**< The wait after the next failure */
    int32_t  updated_at;   /**< Minute of the day of the last success; -1 before one */
    char     error[96];
} fetch_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void         tick(lv_timer_t * timer);
static bool         place_ready(void);
static const char * place_problem(void);
static bool         due(const fetch_t * fetch);
static void         schedule(fetch_t * fetch, uint32_t wait);
static void         fetch_succeeded(fetch_t * fetch, uint32_t every);
static void         fetch_failed(fetch_t * fetch, const char * error, const char * notice);
static const char * failure_text(const http_job_t * job, const char * unreachable);
static void         updated_text(const fetch_t * fetch, char * buf, size_t size);

static void place_start(void);
static void place_work(http_job_t * job);
static void place_done(http_job_t * job);
static void forecast_start(void);
static void forecast_work(http_job_t * job);
static void forecast_done(http_job_t * job);
static void air_start(void);
static void air_work(http_job_t * job);
static void air_done(http_job_t * job);

static void                    weather_publish(void);
static void                    rain_publish(void);
static void                    moon_publish(time_t now);
static bool                    same_day(const struct tm * a, const struct tm * b);
static float                   lit_share(float phase);
static page_clock_rain_level_t rain_level(float mm_per_hour);
static ui_weather_t            condition_of(int code, bool day);
static void                    time_text(char * buf, size_t size, time_t when);
static void                    degrees_text(char * buf, size_t size, float value);
static int32_t                 whole(float value);

static void refresh_requested(void);

/**********************
 *  STATIC VARIABLES
 **********************/

static fetch_t place_fetch;
static fetch_t forecast_fetch;
static fetch_t air_fetch;

static bool                    have_place;   /**< `place` holds the IP's location */
static open_meteo_place_t      place;
static open_meteo_forecast_t * forecast;
static open_meteo_air_t *      air;

/** Goes up when the settings change, so answers to older requests are dropped. */
static uint32_t generation;

static uint32_t redrawn;
static bool     started;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_weather_feed_init(void)
{
    fetch_t blank = {.retry = RETRY_FIRST_MS, .updated_at = -1};

    place_fetch    = blank;
    forecast_fetch = blank;
    air_fetch      = blank;

    page_weather_set_refresh_cb(refresh_requested);
    started = true;

    ui_weather_feed_republish();

    lv_timer_create(tick, TICK_MS, NULL);
    tick(NULL);
}

void ui_weather_feed_republish(void)
{
    if(!started) return;

    weather_publish();
    ui_air_feed_outdoor_changed();
}

void ui_weather_feed_settings_changed(const settings_t * before, const settings_t * after)
{
    bool moved = before->location_auto != after->location_auto ||
                 strcmp(before->location_name, after->location_name) != 0 ||
                 before->latitude != after->latitude || before->longitude != after->longitude;
    bool units = before->fahrenheit != after->fahrenheit;

    if(!started || (!moved && !units)) return;

    /*Whatever is on its way was asked for with the old settings.*/
    generation++;
    place_fetch.busy    = false;
    forecast_fetch.busy = false;
    air_fetch.busy      = false;

    free(forecast);
    forecast              = NULL;
    forecast_fetch.failed = false;
    schedule(&forecast_fetch, 0);

    if(moved) {
        have_place         = false;
        place_fetch.failed = false;
        schedule(&place_fetch, 0);

        free(air);
        air              = NULL;
        air_fetch.failed = false;
        schedule(&air_fetch, 0);
    }

    ui_weather_feed_republish();
    tick(NULL);
}

void ui_weather_feed_refresh_air(void)
{
    air_fetch.asked = true;
    schedule(&air_fetch, 0);
    if(!have_place) schedule(&place_fetch, 0);
    tick(NULL);
}

void ui_weather_feed_get_air(ui_weather_feed_air_t * out)
{
    static char  updated[32];
    const char * problem = place_problem();

    updated_text(&air_fetch, updated, sizeof(updated));

    out->data       = air;
    out->state      = air ? UI_STATUS_READY : (problem || air_fetch.failed) ? UI_STATUS_FAILED : UI_STATUS_LOADING;
    out->error      = problem ? problem : air_fetch.error;
    out->stale      = air && air_fetch.failed;
    out->refreshing = air && air_fetch.busy;
    out->updated    = updated;
}

const char * ui_weather_feed_place(void)
{
    return place_ready() && place.name[0] ? place.name : NULL;
}

bool ui_weather_feed_outdoor_climate(float * temperature, float * humidity)
{
    if(!forecast) return false;

    *temperature = forecast->now.temperature;
    *humidity    = forecast->now.humidity;
    return true;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void tick(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    http_worker_poll();

    if(!place_ready()) {
        if(settings_get()->location_auto && !place_fetch.busy && due(&place_fetch)) place_start();
        return;
    }

    if(!forecast_fetch.busy && due(&forecast_fetch)) forecast_start();
    if(!air_fetch.busy && due(&air_fetch)) air_start();

    if(forecast && lv_tick_elaps(redrawn) >= REDRAW_EVERY_MS) weather_publish();
}

/** @return   true once it is known where the forecasts are for; `place` then says */
static bool place_ready(void)
{
    const settings_t * s = settings_get();

    if(s->location_auto) return have_place;
    if(s->latitude == 0.0 && s->longitude == 0.0) return false;

    lv_strlcpy(place.name, s->location_name, sizeof(place.name));
    place.latitude  = s->latitude;
    place.longitude = s->longitude;
    return true;
}

/** @return   why it is not known where the forecasts are for; NULL if it is, or while finding out */
static const char * place_problem(void)
{
    const settings_t * s = settings_get();

    if(!s->location_auto) {
        return (s->latitude == 0.0 && s->longitude == 0.0) ? "No location is set for the forecast" : NULL;
    }
    return !have_place && place_fetch.failed ? place_fetch.error : NULL;
}

static bool due(const fetch_t * fetch)
{
    return lv_tick_elaps(fetch->since) >= fetch->wait;
}

static void schedule(fetch_t * fetch, uint32_t wait)
{
    fetch->since = lv_tick_get();
    fetch->wait  = wait;
}

static void fetch_succeeded(fetch_t * fetch, uint32_t every)
{
    struct tm now;
    clock_time_now(&now);

    fetch->busy       = false;
    fetch->failed     = false;
    fetch->asked      = false;
    fetch->error[0]   = '\0';
    fetch->retry      = RETRY_FIRST_MS;
    fetch->updated_at = now.tm_hour * 60 + now.tm_min;
    schedule(fetch, every);
}

/**
 * @param notice   what to tell the user while older data is still up, if this
 *                 failure is news -- the first of a run, or one a tap asked
 *                 for; NULL for nothing
 */
static void fetch_failed(fetch_t * fetch, const char * error, const char * notice)
{
    if(notice && (fetch->asked || !fetch->failed)) ui_notice_show(UI_NOTICE_ERROR, notice);

    fetch->busy   = false;
    fetch->failed = true;
    fetch->asked  = false;
    lv_strlcpy(fetch->error, error, sizeof(fetch->error));

    schedule(fetch, fetch->retry);
    fetch->retry = LV_MIN(fetch->retry * 2, RETRY_MAX_MS);
}

static const char * failure_text(const http_job_t * job, const char * unreachable)
{
    if(job->response.status == 0) return unreachable;
    if(!job->ok) return "The forecast service turned the request down";
    return "The forecast service's answer could not be read";
}

static void updated_text(const fetch_t * fetch, char * buf, size_t size)
{
    char time[12];

    if(fetch->updated_at < 0) {
        buf[0] = '\0';
        return;
    }

    ui_format_time(time, sizeof(time), fetch->updated_at / 60, fetch->updated_at % 60);
    lv_snprintf(buf, size, "Updated %s", time);
}

static void place_start(void)
{
    http_job_t * job = http_job_create(OPEN_METEO_IP_LOCATION_URL, PLACE_MAX_BYTES);
    if(!job) return;

    job->tag  = generation;
    job->work = place_work;
    job->done = place_done;

    place_fetch.busy = true;
    http_worker_submit(job, place_fetch.asked);
}

/** Worker thread: nothing from LVGL. */
static void place_work(http_job_t * job)
{
    open_meteo_place_t * found = job->ok ? malloc(sizeof(*found)) : NULL;

    if(found && !open_meteo_parse_place(job->response.body, job->response.len, found)) {
        free(found);
        found = NULL;
    }
    job->user = found;
}

static void place_done(http_job_t * job)
{
    open_meteo_place_t * found = job->user;

    if(job->tag == generation) {
        if(found) {
            place      = *found;
            have_place = true;
            fetch_succeeded(&place_fetch, 0);
            LV_LOG_USER("weather: forecasts for %s", place.name);

            /*The tick starts the forecasts.*/
            schedule(&forecast_fetch, 0);
            schedule(&air_fetch, 0);
        }
        else {
            LV_LOG_WARN("weather: ip-api.com gave no location (HTTP %d)", job->response.status);
            fetch_failed(&place_fetch, "Couldn't find out where the clock is", NULL);
        }

        ui_weather_feed_republish();
    }

    free(found);
}

static void forecast_start(void)
{
    char url[URL_LEN];

    if(!open_meteo_forecast_url(place.latitude, place.longitude, settings_get()->fahrenheit, url, sizeof(url))) return;

    http_job_t * job = http_job_create(url, FORECAST_MAX_BYTES);
    if(!job) return;

    job->tag  = generation;
    job->work = forecast_work;
    job->done = forecast_done;

    forecast_fetch.busy = true;
    http_worker_submit(job, forecast_fetch.asked);
    weather_publish();
}

static void forecast_work(http_job_t * job)
{
    open_meteo_forecast_t * parsed = job->ok ? malloc(sizeof(*parsed)) : NULL;

    if(parsed && !open_meteo_parse_forecast(job->response.body, job->response.len, parsed)) {
        free(parsed);
        parsed = NULL;
    }
    job->user = parsed;
}

static void forecast_done(http_job_t * job)
{
    open_meteo_forecast_t * parsed = job->user;

    if(job->tag != generation) {
        free(parsed);
        return;
    }

    if(parsed) {
        free(forecast);
        forecast = parsed;
        fetch_succeeded(&forecast_fetch, FORECAST_EVERY_MS);
    }
    else {
        LV_LOG_WARN("weather: no forecast from Open-Meteo (HTTP %d)", job->response.status);
        fetch_failed(&forecast_fetch, failure_text(job, "Couldn't reach the forecast service"),
                     forecast ? "Couldn't refresh the weather forecast" : NULL);
    }

    ui_weather_feed_republish();
}

static void air_start(void)
{
    char url[URL_LEN];

    if(!open_meteo_air_url(place.latitude, place.longitude, url, sizeof(url))) return;

    http_job_t * job = http_job_create(url, FORECAST_MAX_BYTES);
    if(!job) return;

    job->tag  = generation;
    job->work = air_work;
    job->done = air_done;

    air_fetch.busy = true;
    http_worker_submit(job, air_fetch.asked);
    ui_air_feed_outdoor_changed();
}

static void air_work(http_job_t * job)
{
    open_meteo_air_t * parsed = job->ok ? malloc(sizeof(*parsed)) : NULL;

    if(parsed && !open_meteo_parse_air(job->response.body, job->response.len, parsed)) {
        free(parsed);
        parsed = NULL;
    }
    job->user = parsed;
}

static void air_done(http_job_t * job)
{
    open_meteo_air_t * parsed = job->user;

    if(job->tag != generation) {
        free(parsed);
        return;
    }

    if(parsed) {
        free(air);
        air = parsed;
        fetch_succeeded(&air_fetch, AIR_EVERY_MS);
    }
    else {
        LV_LOG_WARN("weather: no air quality forecast from Open-Meteo (HTTP %d)", job->response.status);
        fetch_failed(&air_fetch, failure_text(job, "Couldn't reach the air quality service"),
                     air ? "Couldn't refresh the air quality forecast" : NULL);
    }

    ui_air_feed_outdoor_changed();
}

static void weather_publish(void)
{
    const char *      problem = place_problem();
    ui_status_state_t state   = forecast ? UI_STATUS_READY
                                : (problem || forecast_fetch.failed) ? UI_STATUS_FAILED : UI_STATUS_LOADING;
    const char *      error   = problem ? problem : forecast_fetch.error;

    redrawn = lv_tick_get();

    page_weather_set_state(state, error);
    page_weather_set_refreshing(forecast && forecast_fetch.busy);
    page_clock_set_weather_state(state, error);

    if(!forecast) return;

    const open_meteo_forecast_t * f          = forecast;
    time_t                        now        = time(NULL);
    bool                          fahrenheit = settings_get()->fahrenheit;

    /*The hour under way.*/
    uint32_t current = 0;
    while(current + 1 < f->hour_count && f->hours[current + 1].time <= now) current++;

    char sunrise[12], sunset[12], wind[32], updated[32];

    page_weather_now_t conditions = {
        .condition   = condition_of(f->now.code, f->now.is_day),
        .summary     = open_meteo_code_text(f->now.code),
        .temp        = whole(f->now.temperature),
        .feels_like  = whole(f->now.feels_like),
        .high        = whole(f->days[0].high),
        .low         = whole(f->days[0].low),
        .humidity    = whole(f->now.humidity),
        .rain_chance = whole(f->hours[current].rain_chance),
        .wind        = wind,
        .sunrise     = sunrise,
        .sunset      = sunset,
    };

    lv_snprintf(wind, sizeof(wind), "%d %s %s", (int)whole(f->now.wind_speed), fahrenheit ? "mph" : "km/h",
                open_meteo_compass(f->now.wind_direction));
    time_text(sunrise, sizeof(sunrise), f->days[0].sunrise);
    time_text(sunset, sizeof(sunset), f->days[0].sunset);
    page_weather_set_now(&conditions);

    /*Every two hours, from the next.*/
    page_weather_hour_t hours[PAGE_WEATHER_HOURS];
    char                hour_text[PAGE_WEATHER_HOURS][12];
    uint32_t            hour_count = 0;

    for(uint32_t i = current + 1; i < f->hour_count && hour_count < PAGE_WEATHER_HOURS; i += 2) {
        struct tm local;
        clock_time_local(f->hours[i].time, &local);
        ui_format_hour(hour_text[hour_count], sizeof(hour_text[hour_count]), local.tm_hour);

        hours[hour_count].when        = hour_text[hour_count];
        hours[hour_count].condition   = condition_of(f->hours[i].code, f->hours[i].is_day);
        hours[hour_count].temp        = whole(f->hours[i].temperature);
        hours[hour_count].rain_chance = whole(f->hours[i].rain_chance);
        hour_count++;
    }
    page_weather_set_hourly(hours, hour_count);

    page_weather_day_t days[PAGE_WEATHER_DAYS];
    uint32_t           day_count = LV_MIN(f->day_count, PAGE_WEATHER_DAYS);

    for(uint32_t i = 0; i < day_count; i++) {
        /*Midday, so the weekday comes out the same in the clock's zone as in the forecast's.*/
        struct tm local;
        clock_time_local(f->days[i].date + 12 * 3600, &local);

        days[i].day         = i == 0 ? "Today" : ui_format_weekday(local.tm_wday, true);
        days[i].condition   = condition_of(f->days[i].code, true);
        days[i].low         = whole(f->days[i].low);
        days[i].high        = whole(f->days[i].high);
        days[i].rain_chance = whole(f->days[i].rain_chance);
    }
    page_weather_set_daily(days, day_count);
    moon_publish(now);

    updated_text(&forecast_fetch, updated, sizeof(updated));
    page_weather_set_location(place.name, updated, forecast_fetch.failed);

    char temperature[12], feels[12], humidity[12];
    degrees_text(temperature, sizeof(temperature), f->now.temperature);
    degrees_text(feels, sizeof(feels), f->now.feels_like);
    if(isnan(f->now.humidity)) lv_strlcpy(humidity, "--", sizeof(humidity));
    else                       lv_snprintf(humidity, sizeof(humidity), "%d %%", (int)whole(f->now.humidity));

    page_clock_set_weather_now(conditions.condition, temperature, conditions.summary, feels, humidity);
    rain_publish();
}

/** The clock page's two-hour rain band, from the quarter-hourly precipitation. */
static void rain_publish(void)
{
    /*The page keeps the pointer.*/
    static char summary[48];

    const open_meteo_forecast_t * f     = forecast;
    time_t                        now   = time(NULL);
    uint32_t                      known = 0;
    page_clock_rain_level_t       levels[PAGE_CLOCK_RAIN_SEGMENTS];

    for(uint32_t i = 0; i < PAGE_CLOCK_RAIN_SEGMENTS; i++) {
        /*The middle of the bar's two minutes.*/
        time_t at = now + (time_t)(i * 120 + 60);

        levels[i] = PAGE_CLOCK_RAIN_NONE;
        for(uint32_t q = 0; q < f->quarter_count; q++) {
            if(at < f->quarters[q].time || at >= f->quarters[q].time + 15 * 60) continue;

            /*What falls in the quarter hour, as a rate.*/
            float amount = f->quarters[q].precipitation;
            if(!isnan(amount)) levels[i] = rain_level(amount * 4.0f);
            known = i + 1;
            break;
        }
    }

    if(known == 0) {
        lv_strlcpy(summary, "No rain forecast to show", sizeof(summary));
    }
    else {
        bool     wet    = levels[0] != PAGE_CLOCK_RAIN_NONE;
        uint32_t change = 0;
        while(change < known && (levels[change] != PAGE_CLOCK_RAIN_NONE) == wet) change++;

        if(change < known) {
            lv_snprintf(summary, sizeof(summary), wet ? "Rain stops in %u min" : "Rain starts in %u min",
                        (unsigned)(change * 2));
        }
        else if(known == PAGE_CLOCK_RAIN_SEGMENTS) {
            lv_strlcpy(summary, wet ? "Rain for the next 2 hours" : "No rain for the next 2 hours", sizeof(summary));
        }
        else {
            lv_snprintf(summary, sizeof(summary), wet ? "Rain for the next %u min" : "No rain for the next %u min",
                        (unsigned)(known * 2));
        }
    }

    page_clock_set_rain(levels, PAGE_CLOCK_RAIN_SEGMENTS, summary);
}

/** Today's moon phase and the next new moon, from the forecast's daily moon phases. */
static void moon_publish(time_t now)
{
    const open_meteo_forecast_t * f        = forecast;
    uint32_t                      day      = 0;
    char                          date[24] = "--";
    struct tm                     today, tomorrow, when;

    /*Today, where the forecast is for.*/
    while(day + 1 < f->moon_count && f->moon[day + 1].date <= now) day++;

    float  phase    = day < f->moon_count ? f->moon[day].phase : NAN;
    bool   known    = !isnan(phase);
    /*From the start of today, so a new moon earlier today is still today's.*/
    time_t new_moon = f->moon_count ? open_meteo_next_new_moon(f, f->moon[day].date) : 0;

    clock_time_local(now, &today);
    clock_time_local(now + 86400, &tomorrow);

    if(new_moon) {
        clock_time_local(new_moon, &when);
        if(same_day(&when, &today))         lv_strlcpy(date, "Today", sizeof(date));
        else if(same_day(&when, &tomorrow)) lv_strlcpy(date, "Tomorrow", sizeof(date));
        else                                ui_format_date_short(date, sizeof(date), &when);
    }

    /*Growing or shrinking in the days ahead: tomorrow's lit share against
     *today's -- so a full moon already points down -- or, without tomorrow,
     *which side of full the moon is.*/
    float next = day + 1 < f->moon_count ? f->moon[day + 1].phase : NAN;

    page_weather_moon_t info = {
        .age          = known ? phase : 0.0f,
        .southern     = place.latitude < 0.0,
        .illumination = known ? (int32_t)lroundf(lit_share(phase) * 100.0f) : -1,
        .waxing       = isnan(next) ? phase < 0.5f : lit_share(next) > lit_share(phase),
        .new_moon     = date,
    };
    page_weather_set_moon(&info);
}

static bool same_day(const struct tm * a, const struct tm * b)
{
    return a->tm_year == b->tm_year && a->tm_yday == b->tm_yday;
}

/** The lit share of the moon's disc, 0..1, from how far round from new it is. */
static float lit_share(float phase)
{
    return (1.0f - cosf(2.0f * 3.1415927f * phase)) / 2.0f;
}

static page_clock_rain_level_t rain_level(float mm_per_hour)
{
    if(mm_per_hour < RAIN_LIGHT_MM_H)    return PAGE_CLOCK_RAIN_NONE;
    if(mm_per_hour < RAIN_MODERATE_MM_H) return PAGE_CLOCK_RAIN_LIGHT;
    if(mm_per_hour < RAIN_HEAVY_MM_H)    return PAGE_CLOCK_RAIN_MODERATE;
    if(mm_per_hour < RAIN_EXTREME_MM_H)  return PAGE_CLOCK_RAIN_HEAVY;
    return PAGE_CLOCK_RAIN_EXTREME;
}

/** A WMO weather code, as one of the drawn icons. */
static ui_weather_t condition_of(int code, bool day)
{
    switch(code) {
        case 0:
        case 1:  return day ? UI_WEATHER_CLEAR : UI_WEATHER_CLEAR_NIGHT;
        case 2:  return day ? UI_WEATHER_PARTLY_CLOUDY : UI_WEATHER_PARTLY_CLOUDY_NIGHT;
        case 45:
        case 48: return UI_WEATHER_FOG;
        case 80:
        case 81:
        case 82: return day ? UI_WEATHER_SHOWERS : UI_WEATHER_RAIN;
        case 95:
        case 96:
        case 99: return UI_WEATHER_THUNDERSTORM;
        default: break;
    }

    if((code >= 71 && code <= 77) || code == 85 || code == 86) return UI_WEATHER_SNOW;
    if(code >= 51 && code <= 67) return UI_WEATHER_RAIN;
    return UI_WEATHER_CLOUDY;
}

static void time_text(char * buf, size_t size, time_t when)
{
    struct tm local;

    if(when == 0) {
        lv_strlcpy(buf, "--", size);
        return;
    }

    clock_time_local(when, &local);
    ui_format_time(buf, size, local.tm_hour, local.tm_min);
}

static void degrees_text(char * buf, size_t size, float value)
{
    if(isnan(value)) lv_strlcpy(buf, "--", size);
    else             lv_snprintf(buf, size, "%d" UI_DEG, (int)whole(value));
}

static int32_t whole(float value)
{
    return isnan(value) ? 0 : (int32_t)lroundf(value);
}

static void refresh_requested(void)
{
    forecast_fetch.asked = true;
    schedule(&forecast_fetch, 0);
    if(!have_place) schedule(&place_fetch, 0);
    tick(NULL);
}
