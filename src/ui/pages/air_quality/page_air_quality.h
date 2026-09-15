/**
 * @file page_air_quality.h
 *
 * Air quality page: live readings, history, analysis and forecast.
 *
 * Layout:
 *
 *   +---+--------+--------+--------+--------+--------+
 *   | S | PM1.0  | PM2.5  |  CO2   |  VOC   |  TEMP  |
 *   | > | PM4.0  |  PM10  |  NOx   |  HCHO  | HUMID  |
 *   +---+--------+--------+--------+--------+--------+
 *   |  history chart            |  analysis          |
 *   |  [1h] [24h] [7d]          |  min/avg/max       |
 *   +---------------------------+--------------------+
 *   |  forecast strip                                |
 *   +------------------------------------------------+
 *
 * S is the sensor button: a narrow strip naming the current sensor sideways.
 * Tapping it pops up the sensor list beside it, grouped under Indoor and
 * Outdoor headings.
 *
 * Every metric tile is also a toggle: tapping it adds that metric's trace to
 * the history chart or removes it. The tile's outline and dot take the
 * trace's colour while it is plotted, so the tiles double as the legend.
 */

#ifndef PAGE_AIR_QUALITY_H
#define PAGE_AIR_QUALITY_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_page.h"
#include "ui/ui_status.h"

/*********************
 *      DEFINES
 *********************/

/** Points held in the history chart. One per pixel column is plenty. */
#define PAGE_AQ_HISTORY_POINTS 96

#define PAGE_AQ_FORECAST_SLOTS 6

/** Most sensors the selector will list. */
#define PAGE_AQ_SENSOR_MAX 8

/** Longest sensor name kept, in bytes; longer names are truncated. */
#define PAGE_AQ_SENSOR_NAME_LEN 24

/**********************
 *      TYPEDEFS
 **********************/

/** The metrics shown as tiles across the top, in display order: the first
 *  five fill the top row left to right, the next five the second row. */
typedef enum {
    PAGE_AQ_METRIC_PM1,
    PAGE_AQ_METRIC_PM25,
    PAGE_AQ_METRIC_CO2,
    PAGE_AQ_METRIC_VOC,
    PAGE_AQ_METRIC_TEMP,
    PAGE_AQ_METRIC_PM4,
    PAGE_AQ_METRIC_PM10,
    PAGE_AQ_METRIC_NOX,
    PAGE_AQ_METRIC_HCHO,
    PAGE_AQ_METRIC_HUMIDITY,
    PAGE_AQ_METRIC_COUNT,
} page_aq_metric_t;

/** History window the chart is showing. */
typedef enum {
    PAGE_AQ_RANGE_1H,
    PAGE_AQ_RANGE_24H,
    PAGE_AQ_RANGE_7D,
    PAGE_AQ_RANGE_COUNT,
} page_aq_range_t;

/** One entry in the sensor selector. */
typedef struct {
    const char * name;      /**< e.g. "Bedroom" */
    bool         outdoor;   /**< Which group it is listed under in the popup */
} page_aq_sensor_t;

/** One slot in the forecast strip. */
typedef struct {
    const char * when;   /**< e.g. "12:00" or "Tue" */
    int32_t      aqi;    /**< Predicted index; drives the colour */
} page_aq_forecast_t;

/** Called when the user picks a different history window. */
typedef void (*page_aq_range_cb_t)(page_aq_range_t range);

/** Called when the user picks a different sensor. */
typedef void (*page_aq_sensor_cb_t)(uint32_t index);

/** Called when the user adds a metric to the chart or removes it. */
typedef void (*page_aq_plot_cb_t)(page_aq_metric_t metric, bool plotted);

/** Called when the forecast's refresh button, or Try again, is tapped. */
typedef void (*page_aq_refresh_cb_t)(void);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_air_quality_desc(void);

/**
 * Set the headline index shown next to the chart.
 * @param aqi   index value, 0..500; negative when unknown
 */
void page_air_quality_set_index(int32_t aqi);

/**
 * Update one metric tile.
 * @param metric   which tile
 * @param value    formatted value, e.g. "8", or "--" if this sensor lacks it
 * @param unit     unit text, e.g. "ug/m3" (no micro sign in the built-in font)
 * @param status   colour band for the value, e.g. UI_COLOR_GOOD
 */
void page_air_quality_set_metric(page_aq_metric_t metric, const char * value, const char * unit,
                                 lv_color_t status);

/**
 * Replace one metric's history trace.
 *
 * The metrics have unrelated units, so the chart has no shared y-scale: each
 * trace is stretched over the chart's full height between its own min and
 * max. The trace shapes are comparable; their heights are not.
 *
 * @param metric   which trace
 * @param values   array of samples, oldest first; LV_CHART_POINT_NONE for a gap
 * @param count    number of samples, clamped to PAGE_AQ_HISTORY_POINTS
 * @param min      value drawn at the bottom of the chart
 * @param max      value drawn at the top of the chart
 */
void page_air_quality_set_history(page_aq_metric_t metric, const int32_t values[], uint32_t count,
                                  int32_t min, int32_t max);

/**
 * Append one sample to a trace, scrolling it left. Cheaper than re-sending
 * the whole series when a new reading arrives. Uses the min/max from the last
 * page_air_quality_set_history() call for that metric.
 * @param metric   which trace
 * @param value    the new sample
 */
void page_air_quality_push_sample(page_aq_metric_t metric, int32_t value);

/**
 * Add a metric's trace to the chart or remove it, as if its tile was tapped.
 * Does not fire the plot callback.
 * @param metric    which trace
 * @param plotted   true to show it
 */
void page_air_quality_set_plotted(page_aq_metric_t metric, bool plotted);

/**
 * @param metric   which trace
 * @return         true if that metric is currently on the chart
 */
bool page_air_quality_is_plotted(page_aq_metric_t metric);

/**
 * Fill the analysis column.
 * @param min      e.g. "4"
 * @param avg      e.g. "9"
 * @param max      e.g. "22"
 * @param trend    e.g. "Rising" or "Steady"
 * @param advice   one-line health guidance
 */
void page_air_quality_set_analysis(const char * min, const char * avg, const char * max,
                                   const char * trend, const char * advice);

/**
 * Fill the forecast strip.
 * @param slots   array of slots; strings must outlive the call
 * @param count   number of entries, clamped to PAGE_AQ_FORECAST_SLOTS
 */
void page_air_quality_set_forecast(const page_aq_forecast_t slots[], uint32_t count);

/**
 * Replace the sensors offered by the selector. Selects the first one, and
 * closes the sensor popup if it is open. Does not fire the sensor callback.
 * The popup lists indoor sensors first, then outdoor, each group in the order
 * given here.
 * @param sensors   array of sensors; names are copied, up to
 *                  PAGE_AQ_SENSOR_NAME_LEN bytes
 * @param count     number of entries, clamped to PAGE_AQ_SENSOR_MAX
 */
void page_air_quality_set_sensors(const page_aq_sensor_t sensors[], uint32_t count);

/**
 * @return   index of the sensor currently selected
 */
uint32_t page_air_quality_get_sensor(void);

/**
 * Register the callback fired when the user taps 1h / 24h / 7d.
 * @param cb   callback, or NULL to clear
 */
void page_air_quality_set_range_cb(page_aq_range_cb_t cb);

/**
 * Register the callback fired when the user picks another sensor. The
 * service answers by refreshing the tiles, history and analysis for it.
 * @param cb   callback, or NULL to clear
 */
void page_air_quality_set_sensor_cb(page_aq_sensor_cb_t cb);

/**
 * Register the callback fired when the user toggles a metric on the chart.
 * @param cb   callback, or NULL to clear
 */
void page_air_quality_set_plot_cb(page_aq_plot_cb_t cb);

/**
 * Say whether there are readings for the selected sensor. Until there are,
 * the chart and the analysis are covered -- a spinner, or why not -- and the
 * tiles read "--".
 * @param state     loading, ready or failed
 * @param message   why, when failed
 */
void page_air_quality_set_readings_state(ui_status_state_t state, const char * message);

/**
 * The same for the forecast strip. Failed offers Try again, which fires the
 * refresh callback.
 */
void page_air_quality_set_forecast_state(ui_status_state_t state, const char * message);

/**
 * Say how fresh the forecast is, beside its refresh button.
 * @param updated   e.g. "Updated 10:15 AM"; NULL for nothing
 * @param stale     the last refresh failed: drawn as a warning
 */
void page_air_quality_set_forecast_updated(const char * updated, bool stale);

/**
 * @param refreshing   true while the forecast is fetched: its refresh button turns into a spinner
 */
void page_air_quality_set_forecast_refreshing(bool refreshing);

/**
 * @param cb   called when the forecast's refresh button or Try again is tapped; NULL to clear
 */
void page_air_quality_set_refresh_cb(page_aq_refresh_cb_t cb);

/**
 * @return   the history window the chart is showing
 */
page_aq_range_t page_air_quality_get_range(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_AIR_QUALITY_H*/
