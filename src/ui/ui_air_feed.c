/**
 * @file ui_air_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_air_feed.h"
#include "ui/ui_format.h"
#include "ui/ui_status.h"
#include "ui/ui_theme.h"
#include "ui/ui_weather_feed.h"
#include "ui/pages/air_quality/page_air_quality.h"
#include "ui/pages/clock/page_clock.h"
#include "net/mqtt_client.h"
#include "sensors/sensor_history.h"
#include "sensors/sensor_hw.h"
#include "sensors/sensor_sampler.h"
#include "settings/clock_time.h"
#include "settings/settings.h"

#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*********************
 *      DEFINES
 *********************/

/** How often the sampler is asked for a new reading. */
#define POLL_MS 1000

/** The chart's newest point moves with every reading; it is redrawn this often, and whenever a bucket finishes. */
#define CHART_EVERY_MS (30UL * 1000)

/** How often the history is written to the card. */
#define SAVE_EVERY_MS (10UL * 60 * 1000)

/** No reading with any value in it by then: the sensors are not answering. */
#define SENSOR_TIMEOUT_MS (10UL * 1000)

#define DISCOVERY_PREFIX "homeassistant"
#define TOPIC_LEN        160

#define NOT_ANSWERING "The air sensors are not answering"

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    SOURCE_INDOOR,    /**< The clock's own sensors */
    SOURCE_OUTDOOR,   /**< The forecast */
} source_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void              poll(lv_timer_t * timer);
static void              reading_take(sensor_reading_t * reading);
static void              parts_report(uint32_t parts);
static ui_status_state_t indoor_state(void);

static void sensors_publish(void);
static void tiles_publish(void);
static void chart_publish(void);
static void time_labels_publish(void);
static void analysis_publish(void);
static void readings_state_publish(void);
static void forecast_publish(void);
static void clock_publish(void);

static float                         source_value(sensor_metric_t metric);
static void                          source_series(sensor_metric_t metric, float out[]);
static void                          outdoor_series(sensor_metric_t metric, float out[]);
static float                         hour_value(const open_meteo_air_hour_t * hour, sensor_metric_t metric);
static const open_meteo_air_hour_t * air_hour_at(const open_meteo_air_t * forecast, time_t at);

static float        display_value(sensor_metric_t metric, float value, bool indoor);
static void         value_text(char * buf, size_t size, sensor_metric_t metric, float value);
static const char * unit_text(sensor_metric_t metric);
static lv_color_t   value_color(sensor_metric_t metric, float value);
static const char * advice_text(float aqi, float co2, bool indoor);

static void mqtt_accumulate(const sensor_reading_t * reading);
static void mqtt_state_publish(void);
static void mqtt_announce(void * user);

static void range_selected(page_aq_range_t selected);
static void sensor_selected(uint32_t index);
static void forecast_refresh_requested(void);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Each tile's metric, and how its values go onto the chart. */
static const struct {
    sensor_metric_t metric;
    int32_t         scale;      /**< Chart values are readings times this, so tenths survive as integers */
    float           min_span;   /**< The least the chart's range spans, so sensor noise stays small */
} tiles[PAGE_AQ_METRIC_COUNT] = {
    [PAGE_AQ_METRIC_PM1]      = {SENSOR_PM1,         10, 5.0f},
    [PAGE_AQ_METRIC_PM25]     = {SENSOR_PM25,        10, 5.0f},
    [PAGE_AQ_METRIC_CO2]      = {SENSOR_CO2,         1,  100.0f},
    [PAGE_AQ_METRIC_VOC]      = {SENSOR_VOC,         1,  40.0f},
    [PAGE_AQ_METRIC_TEMP]     = {SENSOR_TEMPERATURE, 10, 2.0f},
    [PAGE_AQ_METRIC_PM4]      = {SENSOR_PM4,         10, 5.0f},
    [PAGE_AQ_METRIC_PM10]     = {SENSOR_PM10,        10, 5.0f},
    [PAGE_AQ_METRIC_NOX]      = {SENSOR_NOX,         1,  10.0f},
    [PAGE_AQ_METRIC_HCHO]     = {SENSOR_HCHO,        10, 10.0f},
    [PAGE_AQ_METRIC_HUMIDITY] = {SENSOR_HUMIDITY,    10, 10.0f},
};

static bool             initialized;
static source_t         source;
static page_aq_range_t  range = PAGE_AQ_RANGE_24H;
static char             outdoor_name[PAGE_AQ_SENSOR_NAME_LEN + 1];

static uint32_t         sample_count;   /**< The sampler's count at the reading last taken */
static sensor_reading_t latest;         /**< With the temperature offset applied */
static bool             have_values;    /**< Some reading has held a value */
static uint32_t         parts_ok = SENSOR_HW_AIR | SENSOR_HW_CLIMATE;

static uint32_t started_at;
static uint32_t chart_at;
static uint32_t save_at;
static uint32_t publish_at;

static double   publish_sum[SENSOR_METRIC_COUNT];
static uint32_t publish_count[SENSOR_METRIC_COUNT];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_air_feed_init(void)
{
    sensor_reading_clear(&latest);

    if(!sensor_history_load(SENSOR_HISTORY_PATH)) LV_LOG_USER("air: no sensor history yet in " SENSOR_HISTORY_PATH);
    if(!sensor_sampler_start()) ui_notice_show(UI_NOTICE_ERROR, "The air sensors could not be started");

    page_air_quality_set_range_cb(range_selected);
    page_air_quality_set_sensor_cb(sensor_selected);
    page_air_quality_set_refresh_cb(forecast_refresh_requested);
    mqtt_client_set_connected_cb(mqtt_announce, NULL);

    started_at  = lv_tick_get();
    chart_at    = started_at;
    save_at     = started_at;
    publish_at  = started_at;
    initialized = true;

    ui_air_feed_republish();
    if(mqtt_client_is_connected()) mqtt_announce(NULL);

    lv_timer_create(poll, POLL_MS, NULL);
}

void ui_air_feed_republish(void)
{
    if(!initialized) return;

    /*A rebuilt page has no sensors listed: list them again.*/
    outdoor_name[0] = '\0';
    sensors_publish();
    range = page_air_quality_get_range();

    tiles_publish();
    chart_publish();
    analysis_publish();
    readings_state_publish();
    forecast_publish();
    clock_publish();
}

void ui_air_feed_outdoor_changed(void)
{
    if(!initialized) return;

    source_t was = source;
    sensors_publish();

    if(source == SOURCE_OUTDOOR || source != was) {
        tiles_publish();
        chart_publish();
        analysis_publish();
        readings_state_publish();
    }

    forecast_publish();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void poll(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    sensor_reading_t reading;
    uint32_t         parts = 0;
    uint32_t         count = sensor_sampler_get(&reading, &parts);

    if(count != 0 && count != sample_count) {
        sample_count = count;
        parts_report(parts);
        reading_take(&reading);
    }
    else if(!have_values && lv_tick_elaps(started_at) >= SENSOR_TIMEOUT_MS) {
        readings_state_publish();
        clock_publish();
    }

    if(lv_tick_elaps(publish_at) >= settings_get()->sensor_interval * 1000U) mqtt_state_publish();

    if(lv_tick_elaps(save_at) >= SAVE_EVERY_MS) {
        save_at = lv_tick_get();
        if(!sensor_history_save(SENSOR_HISTORY_PATH)) LV_LOG_WARN("air: could not write " SENSOR_HISTORY_PATH);
    }
}

static void reading_take(sensor_reading_t * reading)
{
    float * temperature = &reading->values[SENSOR_TEMPERATURE];
    if(!isnan(*temperature)) *temperature += settings_get()->sensor_temp_offset / 10.0f;

    latest = *reading;
    for(int m = 0; m < SENSOR_METRIC_COUNT; m++) {
        if(!isnan(reading->values[m])) have_values = true;
    }

    bool moved = sensor_history_add(time(NULL), reading);
    mqtt_accumulate(reading);

    if(source == SOURCE_INDOOR) {
        tiles_publish();
        analysis_publish();
        if(moved || lv_tick_elaps(chart_at) >= CHART_EVERY_MS) chart_publish();
        readings_state_publish();
    }

    clock_publish();
}

/** Say when a sensor stops answering. Its values are NAN meanwhile, so the tiles read "--". */
static void parts_report(uint32_t parts)
{
    uint32_t lost = parts_ok & ~parts;

    if(lost & SENSOR_HW_AIR) {
        LV_LOG_WARN("air: the SEN69C stopped answering");
        ui_notice_show(UI_NOTICE_ERROR, "The SEN69C air sensor is not answering");
    }
    else if(lost & SENSOR_HW_CLIMATE) {
        LV_LOG_WARN("air: the SHT45 stopped answering");
        ui_notice_show(UI_NOTICE_ERROR, "The SHT45 temperature sensor is not answering");
    }

    parts_ok = parts;
}

static ui_status_state_t indoor_state(void)
{
    if(have_values) return UI_STATUS_READY;
    return lv_tick_elaps(started_at) >= SENSOR_TIMEOUT_MS ? UI_STATUS_FAILED : UI_STATUS_LOADING;
}

/** The sensor selector: the clock's own, then the forecast's, named after the place. */
static void sensors_publish(void)
{
    const char * place = ui_weather_feed_place();
    const char * name  = place ? place : "Outdoor";

    if(strcmp(name, outdoor_name) == 0) return;
    lv_strlcpy(outdoor_name, name, sizeof(outdoor_name));

    const page_aq_sensor_t list[] = {
        {"Indoor",     false},
        {outdoor_name, true},
    };
    page_air_quality_set_sensors(list, 2);

    /*A new list selects its first entry.*/
    source = SOURCE_INDOOR;
}

static void tiles_publish(void)
{
    bool indoor = source == SOURCE_INDOOR;

    for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
        sensor_metric_t metric = tiles[i].metric;
        float           raw    = source_value(metric);
        char            text[16];

        value_text(text, sizeof(text), metric, display_value(metric, raw, indoor));
        page_air_quality_set_metric((page_aq_metric_t)i, text, unit_text(metric), value_color(metric, raw));
    }
}

static void chart_publish(void)
{
    bool indoor = source == SOURCE_INDOOR;

    chart_at = lv_tick_get();

    for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
        sensor_metric_t metric = tiles[i].metric;
        float           scale  = (float)tiles[i].scale;
        float           values[SENSOR_HISTORY_POINTS];
        int32_t         points[SENSOR_HISTORY_POINTS];
        float           low  = INFINITY;
        float           high = -INFINITY;

        source_series(metric, values);

        for(uint32_t p = 0; p < SENSOR_HISTORY_POINTS; p++) {
            float value = display_value(metric, values[p], indoor);

            if(isnan(value)) {
                points[p] = LV_CHART_POINT_NONE;
                continue;
            }

            points[p] = (int32_t)lroundf(value * scale);
            if(value < low) low = value;
            if(value > high) high = value;
        }

        /*Some room above and below the trace, and never less than the least
         *span, or a steady reading's noise would fill the chart.*/
        if(low > high) {
            low  = 0.0f;
            high = tiles[i].min_span;
        }
        else if(high - low < tiles[i].min_span) {
            float middle = (low + high) / 2.0f;
            low  = middle - tiles[i].min_span / 2.0f;
            high = middle + tiles[i].min_span / 2.0f;
        }
        else {
            float room = (high - low) * 0.1f;
            low -= room;
            high += room;
        }

        page_air_quality_set_history((page_aq_metric_t)i, points, SENSOR_HISTORY_POINTS,
                                     (int32_t)floorf(low * scale), (int32_t)ceilf(high * scale), tiles[i].scale);
    }

    time_labels_publish();
}

/**
 * The times under the chart: five across the hour or the day, eight days
 * across the week, the last point being now. Its window is the history's.
 */
static void time_labels_publish(void)
{
    static const uint32_t counts[PAGE_AQ_RANGE_COUNT] = {5, 5, 8};

    uint32_t     count  = counts[range < PAGE_AQ_RANGE_COUNT ? range : PAGE_AQ_RANGE_24H];
    double       window = sensor_history_window((sensor_range_t)range);
    time_t       now    = time(NULL);
    char         texts[PAGE_AQ_TIME_LABELS_MAX][16];
    const char * labels[PAGE_AQ_TIME_LABELS_MAX];

    for(uint32_t i = 0; i < count; i++) {
        time_t    at = now - (time_t)(window * (count - 1 - i) / (count - 1));
        struct tm local;
        clock_time_local(at, &local);

        if(i == count - 1)                 lv_strlcpy(texts[i], "Now", sizeof(texts[i]));
        else if(range == PAGE_AQ_RANGE_7D) lv_strlcpy(texts[i], ui_format_weekday(local.tm_wday, true), sizeof(texts[i]));
        else                               ui_format_time(texts[i], sizeof(texts[i]), local.tm_hour, local.tm_min);
        labels[i] = texts[i];
    }

    page_air_quality_set_time_labels(labels, count);
}

/** The index now, and its low, average, high and trend over the chart's window. */
static void analysis_publish(void)
{
    float    series[SENSOR_HISTORY_POINTS];
    float    values[SENSOR_HISTORY_POINTS];
    uint32_t count   = 0;
    float    current = source_value(SENSOR_AQI);
    bool     indoor  = source == SOURCE_INDOOR;

    source_series(SENSOR_AQI, series);
    for(uint32_t p = 0; p < SENSOR_HISTORY_POINTS; p++) {
        if(!isnan(series[p])) values[count++] = series[p];
    }

    char         min_text[8] = "--", avg_text[8] = "--", max_text[8] = "--";
    const char * trend       = "";

    if(count > 0) {
        float  low = values[0], high = values[0];
        double sum = 0.0;

        for(uint32_t i = 0; i < count; i++) {
            if(values[i] < low) low = values[i];
            if(values[i] > high) high = values[i];
            sum += values[i];
        }

        lv_snprintf(min_text, sizeof(min_text), "%d", (int)lroundf(low));
        lv_snprintf(avg_text, sizeof(avg_text), "%d", (int)lround(sum / count));
        lv_snprintf(max_text, sizeof(max_text), "%d", (int)lroundf(high));
    }

    /*The window's last quarter against its first.*/
    if(count >= 8) {
        uint32_t quarter = count / 4;
        double   before  = 0.0, after = 0.0;

        for(uint32_t i = 0; i < quarter; i++) {
            before += values[i];
            after += values[count - 1 - i];
        }
        before /= quarter;
        after /= quarter;

        double change = after - before;
        double noise  = before * 0.1 > 3.0 ? before * 0.1 : 3.0;
        trend         = change > noise ? "Rising" : change < -noise ? "Falling" : "Steady";
    }

    page_air_quality_set_index(isnan(current) ? -1 : (int32_t)lroundf(current));
    page_air_quality_set_analysis(min_text, avg_text, max_text, trend,
                                  advice_text(current, indoor ? latest.values[SENSOR_CO2] : NAN, indoor));
}

static void readings_state_publish(void)
{
    if(source == SOURCE_OUTDOOR) {
        ui_weather_feed_air_t outdoor;
        ui_weather_feed_get_air(&outdoor);
        page_air_quality_set_readings_state(outdoor.state, outdoor.error);
        return;
    }

    page_air_quality_set_readings_state(indoor_state(), NOT_ANSWERING);
}

/** Now, every three hours for the next twelve, and tomorrow's worst hour. */
static void forecast_publish(void)
{
    /*The page keeps the pointers.*/
    static char when[PAGE_AQ_FORECAST_SLOTS][16];

    ui_weather_feed_air_t outdoor;
    ui_weather_feed_get_air(&outdoor);

    page_air_quality_set_forecast_state(outdoor.state, outdoor.error);
    page_air_quality_set_forecast_refreshing(outdoor.refreshing);
    page_air_quality_set_forecast_updated(outdoor.updated, outdoor.stale);

    if(!outdoor.data) return;

    const open_meteo_air_t * forecast = outdoor.data;
    page_aq_forecast_t       slots[PAGE_AQ_FORECAST_SLOTS];
    uint32_t                 count = 0;
    time_t                   now   = time(NULL);

    if(!isnan(forecast->aqi)) {
        lv_strlcpy(when[count], "Now", sizeof(when[count]));
        slots[count].when = when[count];
        slots[count].aqi  = (int32_t)lroundf(forecast->aqi);
        count++;
    }

    for(int step = 1; step <= 4; step++) {
        const open_meteo_air_hour_t * hour = air_hour_at(forecast, now + step * 3 * 3600);
        if(!hour || isnan(hour->aqi)) continue;

        struct tm local;
        clock_time_local(hour->time, &local);
        ui_format_hour(when[count], sizeof(when[count]), local.tm_hour);
        slots[count].when = when[count];
        slots[count].aqi  = (int32_t)lroundf(hour->aqi);
        count++;
    }

    struct tm tomorrow;
    clock_time_local(now + 86400, &tomorrow);

    float worst = NAN;
    for(uint32_t k = 0; k < forecast->hour_count; k++) {
        struct tm local;
        clock_time_local(forecast->hours[k].time, &local);

        float aqi = forecast->hours[k].aqi;
        if(local.tm_yday != tomorrow.tm_yday || local.tm_year != tomorrow.tm_year || isnan(aqi)) continue;
        if(isnan(worst) || aqi > worst) worst = aqi;
    }

    if(!isnan(worst)) {
        lv_strlcpy(when[count], "Tomorrow", sizeof(when[count]));
        slots[count].when = when[count];
        slots[count].aqi  = (int32_t)lroundf(worst);
        count++;
    }

    page_air_quality_set_forecast(slots, count);
}

/** The clock page's air section: always the clock's own sensors. */
static void clock_publish(void)
{
    char value[16], pm25[32], co2[32], temperature[24], humidity[16];

    page_clock_set_air_state(indoor_state(), NOT_ANSWERING);

    value_text(value, sizeof(value), SENSOR_PM25, latest.values[SENSOR_PM25]);
    lv_snprintf(pm25, sizeof(pm25), "PM2.5  %s ug/m3", value);
    value_text(value, sizeof(value), SENSOR_CO2, latest.values[SENSOR_CO2]);
    lv_snprintf(co2, sizeof(co2), "CO2  %s ppm", value);

    float aqi = latest.values[SENSOR_AQI];
    page_clock_set_air_summary(isnan(aqi) ? -1 : (int32_t)lroundf(aqi), pm25, co2);

    float degrees = display_value(SENSOR_TEMPERATURE, latest.values[SENSOR_TEMPERATURE], true);
    value_text(value, sizeof(value), SENSOR_TEMPERATURE, degrees);
    if(isnan(degrees)) lv_strlcpy(temperature, value, sizeof(temperature));
    else               lv_snprintf(temperature, sizeof(temperature), "%s%s", value, unit_text(SENSOR_TEMPERATURE));

    value_text(value, sizeof(value), SENSOR_HUMIDITY, latest.values[SENSOR_HUMIDITY]);
    if(isnan(latest.values[SENSOR_HUMIDITY])) lv_strlcpy(humidity, value, sizeof(humidity));
    else                                      lv_snprintf(humidity, sizeof(humidity), "%s %%", value);

    page_clock_set_indoor(temperature, humidity);
}

/** A metric now, from the source selected. The forecast's temperature is already in the units shown. */
static float source_value(sensor_metric_t metric)
{
    if(source == SOURCE_INDOOR) return latest.values[metric];

    ui_weather_feed_air_t outdoor;
    float                 temperature = NAN, humidity = NAN;

    ui_weather_feed_get_air(&outdoor);
    ui_weather_feed_outdoor_climate(&temperature, &humidity);

    switch(metric) {
        case SENSOR_PM25:        return outdoor.data ? outdoor.data->pm25 : NAN;
        case SENSOR_PM10:        return outdoor.data ? outdoor.data->pm10 : NAN;
        case SENSOR_AQI:         return outdoor.data ? outdoor.data->aqi : NAN;
        case SENSOR_TEMPERATURE: return temperature;
        case SENSOR_HUMIDITY:    return humidity;
        default:                 return NAN;
    }
}

static void source_series(sensor_metric_t metric, float out[])
{
    /*page_aq_range_t and sensor_range_t list the hour, day and week alike.*/
    if(source == SOURCE_INDOOR) sensor_history_get((sensor_range_t)range, metric, out);
    else                        outdoor_series(metric, out);
}

/** The forecast's hourly values over the chart's window, interpolated onto its points. */
static void outdoor_series(sensor_metric_t metric, float out[])
{
    ui_weather_feed_air_t outdoor;
    ui_weather_feed_get_air(&outdoor);

    for(uint32_t p = 0; p < SENSOR_HISTORY_POINTS; p++) out[p] = NAN;
    if(!outdoor.data) return;

    const open_meteo_air_t * forecast = outdoor.data;
    double                   window   = sensor_history_window((sensor_range_t)range);
    time_t                   now      = time(NULL);
    uint32_t                 k        = 0;

    for(uint32_t p = 0; p < SENSOR_HISTORY_POINTS; p++) {
        /*Each point at the end of its share of the window, the last at the present.*/
        time_t at = now - (time_t)(window * (SENSOR_HISTORY_POINTS - 1 - p) / SENSOR_HISTORY_POINTS);

        while(k + 1 < forecast->hour_count && forecast->hours[k + 1].time <= at) k++;
        if(k + 1 >= forecast->hour_count || forecast->hours[k].time > at) continue;

        const open_meteo_air_hour_t * a  = &forecast->hours[k];
        const open_meteo_air_hour_t * b  = &forecast->hours[k + 1];
        float                         va = hour_value(a, metric);
        float                         vb = hour_value(b, metric);
        if(isnan(va) || isnan(vb) || b->time <= a->time) continue;

        out[p] = va + (vb - va) * (float)(at - a->time) / (float)(b->time - a->time);
    }
}

static float hour_value(const open_meteo_air_hour_t * hour, sensor_metric_t metric)
{
    switch(metric) {
        case SENSOR_PM25: return hour->pm25;
        case SENSOR_PM10: return hour->pm10;
        case SENSOR_AQI:  return hour->aqi;
        default:          return NAN;
    }
}

static const open_meteo_air_hour_t * air_hour_at(const open_meteo_air_t * forecast, time_t at)
{
    for(uint32_t k = 0; k < forecast->hour_count; k++) {
        if(at >= forecast->hours[k].time && at < forecast->hours[k].time + 3600) return &forecast->hours[k];
    }
    return NULL;
}

/** A value in the units shown: an indoor temperature in F when the settings ask. */
static float display_value(sensor_metric_t metric, float value, bool indoor)
{
    if(metric == SENSOR_TEMPERATURE && indoor && settings_get()->fahrenheit && !isnan(value)) {
        return value * 9.0f / 5.0f + 32.0f;
    }
    return value;
}

static void value_text(char * buf, size_t size, sensor_metric_t metric, float value)
{
    if(isnan(value)) {
        lv_strlcpy(buf, "--", size);
        return;
    }

    switch(metric) {
        case SENSOR_TEMPERATURE:
            snprintf(buf, size, "%.1f", value);
            break;
        case SENSOR_PM1:
        case SENSOR_PM25:
        case SENSOR_PM4:
        case SENSOR_PM10:
        case SENSOR_HCHO:
            snprintf(buf, size, value < 10.0f ? "%.1f" : "%.0f", value);
            break;
        default:
            snprintf(buf, size, "%.0f", value);
            break;
    }
}

static const char * unit_text(sensor_metric_t metric)
{
    switch(metric) {
        case SENSOR_PM1:
        case SENSOR_PM25:
        case SENSOR_PM4:
        case SENSOR_PM10:        return "ug/m3";
        case SENSOR_CO2:         return "ppm";
        case SENSOR_VOC:
        case SENSOR_NOX:         return "index";
        case SENSOR_HCHO:        return "ppb";
        case SENSOR_TEMPERATURE: return settings_get()->fahrenheit ? UI_DEG "F" : UI_DEG "C";
        case SENSOR_HUMIDITY:    return "%";
        default:                 return "";
    }
}

/** Plain text while a value is fine; the warning colours once it is worth a look. */
static lv_color_t value_color(sensor_metric_t metric, float value)
{
    float warn, bad;

    if(isnan(value)) return UI_COLOR_TEXT_DIM;

    switch(metric) {
        /*The EPA's Good and Moderate bands for PM2.5 and PM10.*/
        case SENSOR_PM1:
        case SENSOR_PM25: warn = 9.1f;    bad = 35.5f;  break;
        case SENSOR_PM4:
        case SENSOR_PM10: warn = 55.0f;   bad = 155.0f; break;
        case SENSOR_CO2:  warn = 1000.0f; bad = 1500.0f; break;
        /*Sensirion's indices sit at 100 and 1 in a room's usual air.*/
        case SENSOR_VOC:  warn = 150.0f;  bad = 250.0f; break;
        case SENSOR_NOX:  warn = 20.0f;   bad = 150.0f; break;
        /*The WHO's guideline is 0.1 mg/m3, about 80 ppb.*/
        case SENSOR_HCHO: warn = 40.0f;   bad = 80.0f;  break;
        case SENSOR_HUMIDITY:
            return (value < 30.0f || value > 60.0f) ? UI_COLOR_WARN : UI_COLOR_TEXT;
        default:
            return UI_COLOR_TEXT;
    }

    return value >= bad ? UI_COLOR_BAD : value >= warn ? UI_COLOR_WARN : UI_COLOR_TEXT;
}

static const char * advice_text(float aqi, float co2, bool indoor)
{
    if(!isnan(co2) && co2 >= 1500.0f) return "CO2 is high: open a window for a while.";
    if(isnan(aqi)) return "";

    if(aqi <= 50.0f) {
        return !isnan(co2) && co2 >= 1000.0f ? "Particles are low, but some fresh air would help."
                                             : "Air quality is good.";
    }
    if(aqi <= 100.0f) return "Acceptable. Unusually sensitive people may notice.";
    if(aqi <= 150.0f) return indoor ? "Sensitive groups may notice: run the purifier." : "Sensitive groups should take it easy outdoors.";
    if(aqi <= 200.0f) return indoor ? "Unhealthy: run the purifier and keep the windows shut." : "Unhealthy: limit time outdoors.";
    return indoor ? "Very unhealthy: purify the air and keep the windows shut." : "Very unhealthy: stay indoors.";
}

static void mqtt_accumulate(const sensor_reading_t * reading)
{
    for(int m = 0; m < SENSOR_METRIC_COUNT; m++) {
        if(isnan(reading->values[m])) continue;
        publish_sum[m] += reading->values[m];
        publish_count[m]++;
    }
}

/**
 * The readings since the last publish, averaged, as one message:
 * {"pm1_0": 4.1, "pm2_5": 5.3, ..., "temperature": 21.6, "humidity": 46.2, "aqi": 29}
 */
static void mqtt_state_publish(void)
{
    const settings_t * s    = settings_get();
    cJSON *            root = mqtt_client_is_connected() && s->sensor_topic[0] ? cJSON_CreateObject() : NULL;
    bool               any  = false;

    publish_at = lv_tick_get();

    for(int m = 0; m < SENSOR_METRIC_COUNT; m++) {
        if(root && publish_count[m]) {
            const sensor_metric_info_t * info  = sensor_metric_info((sensor_metric_t)m);
            double                       scale = pow(10.0, info->decimals);
            double                       mean  = publish_sum[m] / publish_count[m];

            cJSON_AddNumberToObject(root, info->key, round(mean * scale) / scale);
            any = true;
        }
        publish_sum[m]   = 0.0;
        publish_count[m] = 0;
    }

    if(any) {
        char * text = cJSON_PrintUnformatted(root);
        if(text) {
            mqtt_client_publish(s->sensor_topic, text, false);
            cJSON_free(text);
        }
    }

    cJSON_Delete(root);
}

/** Announce every metric to Home Assistant's MQTT discovery, retained. */
static void mqtt_announce(void * user)
{
    LV_UNUSED(user);

    const settings_t * s = settings_get();
    if(!s->sensor_discovery || !s->sensor_topic[0]) return;

    for(int m = 0; m < SENSOR_METRIC_COUNT; m++) {
        const sensor_metric_info_t * info = sensor_metric_info((sensor_metric_t)m);
        char                         topic[TOPIC_LEN], id[80], value_template[64];

        lv_snprintf(topic, sizeof(topic), DISCOVERY_PREFIX "/sensor/%s/%s/config", s->mqtt_client_id, info->key);
        lv_snprintf(id, sizeof(id), "%s_%s", s->mqtt_client_id, info->key);
        lv_snprintf(value_template, sizeof(value_template), "{{ value_json.%s }}", info->key);

        cJSON * config = cJSON_CreateObject();
        cJSON_AddStringToObject(config, "name", info->name);
        cJSON_AddStringToObject(config, "unique_id", id);
        cJSON_AddStringToObject(config, "state_topic", s->sensor_topic);
        cJSON_AddStringToObject(config, "value_template", value_template);
        if(info->unit) cJSON_AddStringToObject(config, "unit_of_measurement", info->unit);
        if(info->device_class) cJSON_AddStringToObject(config, "device_class", info->device_class);
        cJSON_AddStringToObject(config, "state_class", "measurement");
        /*Unavailable once three messages in a row have gone missing.*/
        cJSON_AddNumberToObject(config, "expire_after", s->sensor_interval * 3);

        cJSON * device      = cJSON_AddObjectToObject(config, "device");
        cJSON * identifiers = cJSON_AddArrayToObject(device, "identifiers");
        cJSON_AddItemToArray(identifiers, cJSON_CreateString(s->mqtt_client_id));
        cJSON_AddStringToObject(device, "name", "Smart Alarm Clock");
        cJSON_AddStringToObject(device, "model", "ESP32-P4 with SEN69C and SHT45");

        char * text = cJSON_PrintUnformatted(config);
        if(text) {
            mqtt_client_publish(topic, text, true);
            cJSON_free(text);
        }
        cJSON_Delete(config);
    }
}

static void range_selected(page_aq_range_t selected)
{
    range = selected;
    chart_publish();
    analysis_publish();
}

static void sensor_selected(uint32_t index)
{
    source = index == 1 ? SOURCE_OUTDOOR : SOURCE_INDOOR;

    tiles_publish();
    chart_publish();
    analysis_publish();
    readings_state_publish();
}

static void forecast_refresh_requested(void)
{
    ui_weather_feed_refresh_air();
}
