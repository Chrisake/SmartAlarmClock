/**
 * @file page_air_quality.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/air_quality/page_air_quality.h"
#include "ui/ui_theme.h"

#include <math.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** Chart y-axis span every trace is stretched over. The metrics share no
 *  unit, so each is mapped from its own min..max onto 0..PLOT_SCALE. */
#define PLOT_SCALE 1000

/** Width of the sideways sensor button, left of the tiles. */
#define SENSOR_BUTTON_WIDTH 48

/** Width of the sensor popup. */
#define SENSOR_POPUP_WIDTH 240

/** Tiles per row. */
#define TILE_COLUMNS 5

/** The value axes beside the chart: their width, and their ticks -- one on
 *  each horizontal division line, the top and bottom edges included. */
#define AXIS_WIDTH    44
#define AXIS_TICKS    5
#define AXIS_TEXT_LEN 16

/** Room above and below the plot for the axes' top and bottom labels, which
 *  are centred on the edges. */
#define AXIS_LABEL_ROOM 8

/** The time scale under the chart: its ticks and their labels. */
#define TIME_SCALE_HEIGHT 24

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);

static void tiles_card_create(lv_obj_t * parent);
static void sensor_button_create(lv_obj_t * parent);
static void chart_card_create(lv_obj_t * parent);
static void analysis_card_create(lv_obj_t * parent);
static void forecast_card_create(lv_obj_t * parent);

static lv_obj_t * transparent_box_create(lv_obj_t * parent);

static void range_button_clicked(lv_event_t * e);
static void range_select(page_aq_range_t range);

static void tile_toggled(lv_event_t * e);
static void plot_apply(page_aq_metric_t metric, bool plotted);

static void sensor_button_clicked(lv_event_t * e);
static void sensor_button_resized(lv_event_t * e);
static void sensor_name_update(void);
static void sensor_popup_open(void);
static void sensor_popup_close(void);
static void sensor_popup_dismissed(lv_event_t * e);
static void sensor_popup_group_create(lv_obj_t * panel, const char * heading, bool outdoor);
static void sensor_option_clicked(lv_event_t * e);

static int32_t    plot_value(page_aq_metric_t metric, int32_t value);
static lv_obj_t * axis_create(lv_obj_t * parent, lv_scale_mode_t mode);
static void       axis_style(lv_obj_t * scale);
static void       axes_update(void);
static double     axis_step(double span);
static void       axis_value_text(char * buf, size_t size, double value, double step);
static lv_color_t metric_color(page_aq_metric_t metric);
static void       refresh_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "AQI",
    .icon    = UI_SYMBOL_AIR,
    .create  = create,
    .on_show = NULL,
    /*The popup lives on the top layer, above every page, so it has to be
     *closed explicitly -- e.g. when the idle timeout returns to the clock.*/
    .on_hide = sensor_popup_close,
};

/** Tile captions, units and default trace range, in page_aq_metric_t order.
 *  Trace colours are in metric_color(). */
static const struct {
    const char * caption;
    const char * unit;
    int32_t      min;
    int32_t      max;
} metric_defaults[PAGE_AQ_METRIC_COUNT] = {
    [PAGE_AQ_METRIC_PM1]      = {"PM1.0",    "ug/m3",    0,   40},
    [PAGE_AQ_METRIC_PM25]     = {"PM2.5",    "ug/m3",    0,   60},
    [PAGE_AQ_METRIC_CO2]      = {"CO2",      "ppm",      400, 2000},
    [PAGE_AQ_METRIC_VOC]      = {"VOC",      "index",    0,   500},
    [PAGE_AQ_METRIC_TEMP]     = {"TEMP",     UI_DEG "C", 10,  35},
    [PAGE_AQ_METRIC_PM4]      = {"PM4.0",    "ug/m3",    0,   80},
    [PAGE_AQ_METRIC_PM10]     = {"PM10",     "ug/m3",    0,   100},
    [PAGE_AQ_METRIC_NOX]      = {"NOx",      "index",    0,   500},
    [PAGE_AQ_METRIC_HCHO]     = {"HCHO",     "ppb",      0,   100},
    [PAGE_AQ_METRIC_HUMIDITY] = {"HUMIDITY", "%",        0,   100},
};

static const char * range_labels[PAGE_AQ_RANGE_COUNT] = {"1h", "24h", "7d"};

static struct {
    lv_obj_t *          tile;
    lv_obj_t *          dot;
    lv_obj_t *          value;
    lv_obj_t *          unit;
    lv_chart_series_t * series;
    int32_t             min;
    int32_t             max;
    int32_t             scale;      /**< The values are readings times this */
    int32_t             plot_min;   /**< Drawn at the bottom: `min`, or its axis's shared range */
    int32_t             plot_max;
    int32_t             raw[PAGE_AQ_HISTORY_POINTS];   /**< As given, to draw again on a new range */
    uint32_t            count;
} metrics[PAGE_AQ_METRIC_COUNT];

/** The value axes, left and right: a unit's scale beside the chart, the unit
 *  over it, and room under it that keeps the times under the chart. */
static struct {
    lv_obj_t *   scale;
    lv_obj_t *   unit;
    lv_obj_t *   spacer;
    char         texts[AXIS_TICKS][AXIS_TEXT_LEN];
    const char * text_list[AXIS_TICKS + 1];
} axes[2];

static lv_obj_t *   time_scale;
static char         time_texts[PAGE_AQ_TIME_LABELS_MAX][AXIS_TEXT_LEN];
static const char * time_list[PAGE_AQ_TIME_LABELS_MAX + 1];

static struct {
    char name[PAGE_AQ_SENSOR_NAME_LEN + 1];
    bool outdoor;
} sensor_list[PAGE_AQ_SENSOR_MAX];

static uint32_t            sensor_count;
static uint32_t            sensor_current;
static lv_obj_t *          sensor_button;
static lv_obj_t *          sensor_strip;   /**< Name and chevron, turned on its side */
static lv_obj_t *          sensor_name;
static lv_obj_t *          sensor_popup;   /**< Backdrop on the top layer; NULL when closed */
static page_aq_sensor_cb_t sensor_cb;

static lv_obj_t *          chart;
static lv_obj_t *          chart_hint;
static lv_obj_t *          range_buttons[PAGE_AQ_RANGE_COUNT];
static page_aq_range_t     range_current = PAGE_AQ_RANGE_24H;
static page_aq_range_cb_t  range_cb;
static page_aq_plot_cb_t   plot_cb;

static lv_obj_t *           readings_status[2];   /**< Over the chart, and over the analysis */
static lv_obj_t *           forecast_status;
static lv_obj_t *           forecast_updated;
static lv_obj_t *           forecast_refresh;
static page_aq_refresh_cb_t refresh_cb;

static lv_obj_t * index_value;
static lv_obj_t * index_band;
static lv_obj_t * stat_min;
static lv_obj_t * stat_avg;
static lv_obj_t * stat_max;
static lv_obj_t * stat_trend;
static lv_obj_t * advice_label;

static struct {
    lv_obj_t * root;
    lv_obj_t * when;
    lv_obj_t * pill;
    lv_obj_t * value;
} forecast[PAGE_AQ_FORECAST_SLOTS];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const ui_page_t * page_air_quality_desc(void)
{
    return &desc;
}

void page_air_quality_set_index(int32_t aqi)
{
    if(aqi < 0) {
        lv_label_set_text(index_value, "--");
        lv_obj_set_style_text_color(index_value, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_label_set_text(index_band, "");
        return;
    }

    lv_color_t color = ui_aqi_color(aqi);

    lv_label_set_text_fmt(index_value, "%d", (int)aqi);
    lv_obj_set_style_text_color(index_value, color, LV_PART_MAIN);
    lv_label_set_text(index_band, ui_aqi_band(aqi));
}

void page_air_quality_set_metric(page_aq_metric_t metric, const char * value, const char * unit,
                                 lv_color_t status)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return;

    /*A new unit -- degrees Celsius to Fahrenheit -- can move the axes.*/
    bool new_unit = strcmp(lv_label_get_text(metrics[metric].unit), unit) != 0;

    lv_label_set_text(metrics[metric].value, value);
    lv_label_set_text(metrics[metric].unit, unit);
    lv_obj_set_style_text_color(metrics[metric].value, status, LV_PART_MAIN);

    if(new_unit) axes_update();
}

void page_air_quality_set_history(page_aq_metric_t metric, const int32_t values[], uint32_t count,
                                  int32_t min, int32_t max, int32_t scale)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return;
    if(count > PAGE_AQ_HISTORY_POINTS) count = PAGE_AQ_HISTORY_POINTS;

    metrics[metric].min   = min;
    metrics[metric].max   = max;
    metrics[metric].scale = scale > 0 ? scale : 1;
    metrics[metric].count = count;
    lv_memcpy(metrics[metric].raw, values, count * sizeof(values[0]));

    /*Its range may move an axis, and so every trace sharing it.*/
    axes_update();
}

void page_air_quality_push_sample(page_aq_metric_t metric, int32_t value)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return;

    /*Kept for drawing again on a new range, scrolled as the chart scrolls.*/
    if(metrics[metric].count == PAGE_AQ_HISTORY_POINTS) {
        lv_memmove(metrics[metric].raw, metrics[metric].raw + 1, (PAGE_AQ_HISTORY_POINTS - 1) * sizeof(int32_t));
        metrics[metric].count--;
    }
    metrics[metric].raw[metrics[metric].count++] = value;

    lv_chart_set_next_value(chart, metrics[metric].series, plot_value(metric, value));
}

void page_air_quality_set_time_labels(const char * const labels[], uint32_t count)
{
    if(count > PAGE_AQ_TIME_LABELS_MAX) count = PAGE_AQ_TIME_LABELS_MAX;
    if(count < 2) {
        lv_scale_set_label_show(time_scale, false);
        return;
    }

    for(uint32_t i = 0; i < count; i++) {
        lv_strlcpy(time_texts[i], labels[i] ? labels[i] : "", sizeof(time_texts[i]));
        time_list[i] = time_texts[i];
    }
    time_list[count] = NULL;

    lv_scale_set_total_tick_count(time_scale, count);
    lv_scale_set_major_tick_every(time_scale, 1);
    lv_scale_set_range(time_scale, 0, (int32_t)count - 1);
    lv_scale_set_text_src(time_scale, time_list);
    lv_scale_set_label_show(time_scale, true);
    lv_obj_invalidate(time_scale);

    /*A vertical division line over each time.*/
    lv_chart_set_div_line_count(chart, AXIS_TICKS, count);
}

void page_air_quality_set_plotted(page_aq_metric_t metric, bool plotted)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return;

    if(plotted) lv_obj_add_state(metrics[metric].tile, LV_STATE_CHECKED);
    else        lv_obj_remove_state(metrics[metric].tile, LV_STATE_CHECKED);

    plot_apply(metric, plotted);
}

bool page_air_quality_is_plotted(page_aq_metric_t metric)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return false;

    return lv_obj_has_state(metrics[metric].tile, LV_STATE_CHECKED);
}

void page_air_quality_set_analysis(const char * min, const char * avg, const char * max,
                                   const char * trend, const char * advice)
{
    lv_label_set_text(stat_min, min);
    lv_label_set_text(stat_avg, avg);
    lv_label_set_text(stat_max, max);
    lv_label_set_text(stat_trend, trend);
    lv_label_set_text(advice_label, advice);
}

void page_air_quality_set_forecast(const page_aq_forecast_t slots[], uint32_t count)
{
    if(count > PAGE_AQ_FORECAST_SLOTS) count = PAGE_AQ_FORECAST_SLOTS;

    for(uint32_t i = 0; i < PAGE_AQ_FORECAST_SLOTS; i++) {
        if(i < count) {
            lv_obj_set_hidden(forecast[i].root, false);
            lv_label_set_text(forecast[i].when, slots[i].when);
            lv_label_set_text_fmt(forecast[i].value, "%d", (int)slots[i].aqi);
            lv_obj_set_style_bg_color(forecast[i].pill, ui_aqi_color(slots[i].aqi), LV_PART_MAIN);
        }
        else {
            lv_obj_set_hidden(forecast[i].root, true);
        }
    }
}

void page_air_quality_set_sensors(const page_aq_sensor_t sensors[], uint32_t count)
{
    if(count > PAGE_AQ_SENSOR_MAX) count = PAGE_AQ_SENSOR_MAX;

    /*Its options would point at entries about to change.*/
    sensor_popup_close();

    for(uint32_t i = 0; i < count; i++) {
        lv_strlcpy(sensor_list[i].name, sensors[i].name, sizeof(sensor_list[i].name));
        sensor_list[i].outdoor = sensors[i].outdoor;
    }

    sensor_count   = count;
    sensor_current = 0;
    sensor_name_update();
}

uint32_t page_air_quality_get_sensor(void)
{
    return sensor_current;
}

void page_air_quality_set_range_cb(page_aq_range_cb_t cb)
{
    range_cb = cb;
}

void page_air_quality_set_sensor_cb(page_aq_sensor_cb_t cb)
{
    sensor_cb = cb;
}

void page_air_quality_set_plot_cb(page_aq_plot_cb_t cb)
{
    plot_cb = cb;
}

void page_air_quality_set_readings_state(ui_status_state_t state, const char * message)
{
    /*The analysis column is narrow: the chart says why.*/
    ui_status_set(readings_status[0], state, message);
    ui_status_set(readings_status[1], state, NULL);
}

void page_air_quality_set_forecast_state(ui_status_state_t state, const char * message)
{
    ui_status_set(forecast_status, state, message);
}

void page_air_quality_set_forecast_updated(const char * updated, bool stale)
{
    lv_label_set_text(forecast_updated, updated ? updated : "");
    lv_obj_set_style_text_color(forecast_updated, stale ? UI_COLOR_WARN : UI_COLOR_TEXT_DIM, LV_PART_MAIN);
}

void page_air_quality_set_forecast_refreshing(bool refreshing)
{
    ui_refresh_set_busy(forecast_refresh, refreshing);
}

void page_air_quality_set_refresh_cb(page_aq_refresh_cb_t cb)
{
    refresh_cb = cb;
}

page_aq_range_t page_air_quality_get_range(void)
{
    return range_current;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create(lv_obj_t * parent)
{
    /*Top row holds two rows of tiles: 2 x 69 px tiles, plus the card's
     *padding and the gap between them.*/
    static const int32_t cols[] = {LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {156, LV_GRID_FR(1), 112, LV_GRID_TEMPLATE_LAST};

    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(root, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(root, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_grid_dsc_array(root, cols, rows);

    /*After ui_rebuild() these still point at the old page's objects, deleted.
     *The tiles are built one at a time, and each new one looks over all of
     *them -- plot_apply() and axes_update() -- so the ones not built yet must
     *read as missing, not as freed memory. The feeds tell the new page its
     *readings and history again.*/
    lv_memzero(metrics, sizeof(metrics));
    lv_memzero(axes, sizeof(axes));

    /*The chart first: the tiles add their series to it.*/
    chart_card_create(root);
    tiles_card_create(root);
    analysis_card_create(root);
    forecast_card_create(root);

    return root;
}

static void tiles_card_create(lv_obj_t * parent)
{
    static const int32_t cols[] = {SENSOR_BUTTON_WIDTH,
                                   LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_pad_all(card, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(card, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(card, cols, rows);

    sensor_button_create(card);

    for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
        lv_color_t color = metric_color((page_aq_metric_t)i);

        lv_obj_t * tile = ui_tile_create(card);
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, 1 + i % TILE_COLUMNS, 1,
                             LV_GRID_ALIGN_STRETCH, i / TILE_COLUMNS, 1);
        lv_obj_set_style_pad_all(tile, 6, LV_PART_MAIN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        /*The tile is a toggle for its chart trace. The border is always there
         *and only changes colour, so checking a tile never nudges its contents.*/
        lv_obj_set_checkable(tile, true);
        lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, color, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(tile, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile, tile_toggled, LV_EVENT_VALUE_CHANGED, (void *)(lv_uintptr_t)i);

        /*Caption, with the legend dot at the far end.*/
        lv_obj_t * head = transparent_box_create(tile);
        lv_obj_set_width(head, LV_PCT(100));
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        ui_label_create(head, metric_defaults[i].caption, UI_FONT_XS, UI_COLOR_TEXT_DIM);

        lv_obj_t * dot = lv_obj_create(head);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(dot, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN);

        /*Value with its unit trailing it on the same line.*/
        lv_obj_t * reading = transparent_box_create(tile);
        lv_obj_set_flex_flow(reading, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(reading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(reading, 4, LV_PART_MAIN);

        metrics[i].tile  = tile;
        metrics[i].dot   = dot;
        metrics[i].value = ui_label_create(reading, "--", UI_FONT_LG, UI_COLOR_TEXT_DIM);
        metrics[i].unit  = ui_label_create(reading, metric_defaults[i].unit, UI_FONT_XS, UI_COLOR_TEXT_DIM);
        /*Roughly lines the unit up with the value's baseline.*/
        lv_obj_set_style_pad_bottom(metrics[i].unit, 5, LV_PART_MAIN);

        metrics[i].series   = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
        metrics[i].min      = metric_defaults[i].min;
        metrics[i].max      = metric_defaults[i].max;
        metrics[i].scale    = 1;
        metrics[i].plot_min = metrics[i].min;
        metrics[i].plot_max = metrics[i].max;
        lv_chart_set_all_values(chart, metrics[i].series, LV_CHART_POINT_NONE);

        /*PM2.5 is the one worth watching by default.*/
        page_air_quality_set_plotted((page_aq_metric_t)i, i == PAGE_AQ_METRIC_PM25);
    }
}

static void sensor_button_create(lv_obj_t * parent)
{
    sensor_button = lv_button_create(parent);
    lv_obj_set_grid_cell(sensor_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
    lv_obj_set_style_radius(sensor_button, UI_RADIUS - 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sensor_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(sensor_button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sensor_button, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sensor_button, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(sensor_button, sensor_button_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(sensor_button, sensor_button_resized, LV_EVENT_SIZE_CHANGED, NULL);

    /*Name then chevron, laid out as an ordinary row and then turned a quarter
     *turn anticlockwise about its centre: the name reads bottom to top and
     *the chevron ends up at the top, pointing right, where the popup opens.*/
    sensor_strip = transparent_box_create(sensor_button);
    lv_obj_set_height(sensor_strip, lv_font_get_line_height(UI_FONT_SM));
    lv_obj_set_flex_flow(sensor_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sensor_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sensor_strip, 6, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(sensor_strip, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(sensor_strip, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(sensor_strip, 2700, LV_PART_MAIN);
    lv_obj_center(sensor_strip);

    sensor_name = ui_label_create(sensor_strip, "", UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(sensor_name, 1);
    lv_obj_set_style_text_align(sensor_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ui_label_single_line(sensor_name, UI_FONT_SM);

    ui_label_create(sensor_strip, LV_SYMBOL_DOWN, UI_FONT_XS, UI_COLOR_TEXT_DIM);

    sensor_name_update();
}

static void chart_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    /*Header: caption on the left, range selector on the right.*/
    lv_obj_t * header = transparent_box_create(card);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_style_pad_column(header, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * caption = ui_card_title(header, "HISTORY");
    lv_obj_set_flex_grow(caption, 1);

    for(uint32_t i = 0; i < PAGE_AQ_RANGE_COUNT; i++) {
        lv_obj_t * btn = lv_button_create(header);
        lv_obj_set_size(btn, 52, 32);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, range_labels[i]);
        lv_obj_set_style_text_font(label, UI_FONT_XS, LV_PART_MAIN);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, range_button_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
        range_buttons[i] = btn;
    }

    /*Each axis's unit over it, then [value axis] chart [value axis], then the
     *times under the chart. The units go above: under, the first and last
     *times, centred on the chart's edges, would run into them.*/
    lv_obj_t * units_row = transparent_box_create(card);
    lv_obj_set_width(units_row, LV_PCT(100));
    lv_obj_set_flex_flow(units_row, LV_FLEX_FLOW_ROW);

    axes[0].unit = ui_label_create(units_row, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_hidden(axes[0].unit, true);

    /*Keeps the right unit on the right when the left one is hidden.*/
    lv_obj_t * units_fill = transparent_box_create(units_row);
    lv_obj_set_height(units_fill, 1);
    lv_obj_set_flex_grow(units_fill, 1);

    axes[1].unit = ui_label_create(units_row, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_hidden(axes[1].unit, true);

    lv_obj_t * plot_row = transparent_box_create(card);
    lv_obj_set_width(plot_row, LV_PCT(100));
    lv_obj_set_flex_grow(plot_row, 1);
    lv_obj_set_flex_flow(plot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_ver(plot_row, AXIS_LABEL_ROOM, LV_PART_MAIN);
    lv_obj_set_style_pad_column(plot_row, 6, LV_PART_MAIN);

    axes[0].scale = axis_create(plot_row, LV_SCALE_MODE_VERTICAL_LEFT);

    chart = lv_chart_create(plot_row);
    lv_obj_set_size(chart, 0, LV_PCT(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, PAGE_AQ_HISTORY_POINTS);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, PLOT_SCALE);
    lv_chart_set_div_line_count(chart, AXIS_TICKS, 5);
    /*No padding, so the division lines meet the axes' ticks.*/
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);  /*No point markers*/
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    /*Stands in for the traces when every tile is switched off.*/
    chart_hint = ui_label_create(chart, "Tap a reading above to plot it", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_center(chart_hint);

    axes[1].scale = axis_create(plot_row, LV_SCALE_MODE_VERTICAL_RIGHT);

    lv_obj_t * time_row = transparent_box_create(card);
    lv_obj_set_width(time_row, LV_PCT(100));
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(time_row, 6, LV_PART_MAIN);

    axes[0].spacer = transparent_box_create(time_row);
    lv_obj_set_size(axes[0].spacer, AXIS_WIDTH, 1);
    lv_obj_set_hidden(axes[0].spacer, true);

    time_scale = lv_scale_create(time_row);
    lv_obj_set_size(time_scale, 0, TIME_SCALE_HEIGHT);
    lv_obj_set_flex_grow(time_scale, 1);
    lv_scale_set_mode(time_scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_label_show(time_scale, false);
    axis_style(time_scale);

    axes[1].spacer = transparent_box_create(time_row);
    lv_obj_set_size(axes[1].spacer, AXIS_WIDTH, 1);
    lv_obj_set_hidden(axes[1].spacer, true);

    range_select(range_current);

    readings_status[0] = ui_status_create(card, UI_COLOR_CARD, NULL, NULL);
}

static void analysis_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    ui_card_title(card, "ANALYSIS");

    index_value = ui_label_create(card, "--", UI_FONT_XL, UI_COLOR_TEXT_DIM);
    /*Band names run long ("Unhealthy for sensitive groups"), so this one is
     *deliberately allowed to wrap rather than being ellipsised.*/
    index_band = ui_label_create(card, "", UI_FONT_SM, UI_COLOR_TEXT);
    lv_label_set_long_mode(index_band, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(index_band, LV_PCT(100));

    /*min / avg / max in one row.*/
    lv_obj_t * stats = transparent_box_create(card);
    lv_obj_set_width(stats, LV_PCT(100));
    lv_obj_set_style_pad_top(stats, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const char * stat_captions[3] = {"MIN", "AVG", "MAX"};
    lv_obj_t * stat_values[3];

    for(uint32_t i = 0; i < 3; i++) {
        lv_obj_t * col = transparent_box_create(stats);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        ui_label_create(col, stat_captions[i], UI_FONT_XS, UI_COLOR_TEXT_DIM);
        stat_values[i] = ui_label_create(col, "--", UI_FONT_MD, UI_COLOR_TEXT);
    }

    stat_min = stat_values[0];
    stat_avg = stat_values[1];
    stat_max = stat_values[2];

    stat_trend = ui_label_create(card, "", UI_FONT_SM, UI_COLOR_ACCENT);

    advice_label = ui_label_create(card, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_label_set_long_mode(advice_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(advice_label, LV_PCT(100));

    readings_status[1] = ui_status_create(card, UI_COLOR_CARD, NULL, NULL);
}

static void forecast_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 2, 1);

    /*The caption, then how fresh the forecast is and a way to fetch it again.
     *No taller than the caption alone, or the strip loses its room.*/
    lv_obj_t * header = transparent_box_create(card);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_style_pad_column(header, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * caption = ui_card_title(header, "FORECAST");
    lv_obj_set_flex_grow(caption, 1);

    forecast_updated = ui_label_create(header, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    forecast_refresh = ui_refresh_create(header, refresh_clicked, NULL);

    lv_obj_t * strip = transparent_box_create(card);
    lv_obj_set_width(strip, LV_PCT(100));
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for(uint32_t i = 0; i < PAGE_AQ_FORECAST_SLOTS; i++) {
        lv_obj_t * col = transparent_box_create(strip);
        lv_obj_set_style_pad_row(col, 4, LV_PART_MAIN);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t * when = ui_label_create(col, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);

        lv_obj_t * pill = lv_obj_create(col);
        lv_obj_set_size(pill, 62, 38);
        lv_obj_set_style_bg_color(pill, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(pill, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pill, 0, LV_PART_MAIN);
        lv_obj_set_scrollable(pill, false);

        lv_obj_t * value = lv_label_create(pill);
        lv_label_set_text(value, "");
        lv_obj_set_style_text_font(value, UI_FONT_MD, LV_PART_MAIN);
        lv_obj_set_style_text_color(value, lv_color_black(), LV_PART_MAIN);
        lv_obj_center(value);

        forecast[i].root  = col;
        forecast[i].when  = when;
        forecast[i].pill  = pill;
        forecast[i].value = value;
        lv_obj_set_hidden(col, true);
    }

    forecast_status = ui_status_create(card, UI_COLOR_CARD, refresh_clicked, NULL);
}

/**
 * An invisible, content-sized layout box. Not clickable, so a press on it
 * falls through to whatever it sits in -- which is what lets a whole tile
 * act as one button.
 */
static lv_obj_t * transparent_box_create(lv_obj_t * parent)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
    return box;
}

static void range_button_clicked(lv_event_t * e)
{
    page_aq_range_t range = (page_aq_range_t)(lv_uintptr_t)lv_event_get_user_data(e);

    range_select(range);

    /* TODO: the sensor service answers this by calling
     * page_air_quality_set_history() with the samples for the new window. */
    if(range_cb) range_cb(range);
}

static void range_select(page_aq_range_t range)
{
    range_current = range;

    for(uint32_t i = 0; i < PAGE_AQ_RANGE_COUNT; i++) {
        if((page_aq_range_t)i == range) lv_obj_add_state(range_buttons[i], LV_STATE_CHECKED);
        else                            lv_obj_remove_state(range_buttons[i], LV_STATE_CHECKED);
    }
}

static void tile_toggled(lv_event_t * e)
{
    page_aq_metric_t metric  = (page_aq_metric_t)(lv_uintptr_t)lv_event_get_user_data(e);
    bool             plotted = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);

    plot_apply(metric, plotted);

    if(plot_cb) plot_cb(metric, plotted);
}

/**
 * Bring the chart and the tile's legend dot in line with whether a metric is
 * plotted. The tile's own checked state is the source of truth.
 */
static void plot_apply(page_aq_metric_t metric, bool plotted)
{
    lv_chart_hide_series(chart, metrics[metric].series, !plotted);

    /*Filled in the trace colour when plotted, a hollow ring when not.*/
    lv_obj_set_style_bg_opa(metrics[metric].dot, plotted ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(metrics[metric].dot, plotted ? 0 : 2, LV_PART_MAIN);

    bool any = false;
    for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
        /*Tiles are still being built on the first pass through here.*/
        if(metrics[i].tile && lv_obj_has_state(metrics[i].tile, LV_STATE_CHECKED)) {
            any = true;
            break;
        }
    }
    lv_obj_set_hidden(chart_hint, any);

    axes_update();
}

static void sensor_button_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    if(sensor_count > 0) sensor_popup_open();
}

static void sensor_button_resized(lv_event_t * e)
{
    LV_UNUSED(e);

    /*The strip is laid out before it is turned, so its length has to come
     *from the button's height, less a margin at each end.*/
    lv_obj_set_width(sensor_strip, lv_obj_get_content_height(sensor_button) - 2 * UI_GAP);
}

static void sensor_name_update(void)
{
    lv_label_set_text(sensor_name, sensor_count > 0 ? sensor_list[sensor_current].name : "No sensors");
}

static void sensor_popup_open(void)
{
    if(sensor_popup) return;

    /*Dims the page and catches a tap anywhere outside the panel, which
     *closes the popup without changing the selection.*/
    sensor_popup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(sensor_popup);
    lv_obj_set_size(sensor_popup, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(sensor_popup, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sensor_popup, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_event_cb(sensor_popup, sensor_popup_dismissed, LV_EVENT_CLICKED, NULL);

    lv_obj_t * panel = ui_card_create(sensor_popup);
    lv_obj_set_size(panel, SENSOR_POPUP_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, lv_display_get_vertical_resolution(NULL) - 2 * UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(panel, true);  /*Only matters with a very long list*/

    sensor_popup_group_create(panel, "INDOOR", false);
    sensor_popup_group_create(panel, "OUTDOOR", true);

    lv_obj_align_to(panel, sensor_button, LV_ALIGN_OUT_RIGHT_TOP, UI_GAP / 2, 0);
}

static void sensor_popup_close(void)
{
    if(!sensor_popup) return;

    /*Usually called from an event on one of the popup's own children, which
     *must not be deleted from under itself.*/
    lv_obj_delete_async(sensor_popup);
    sensor_popup = NULL;
}

static void sensor_popup_dismissed(lv_event_t * e)
{
    LV_UNUSED(e);

    sensor_popup_close();
}

/**
 * Add one group to the popup: a heading, which is only a label and so cannot
 * be picked, then a button per sensor in that group. Adds nothing at all when
 * no sensor belongs to it.
 */
static void sensor_popup_group_create(lv_obj_t * panel, const char * heading, bool outdoor)
{
    bool first = true;

    for(uint32_t i = 0; i < sensor_count; i++) {
        if(sensor_list[i].outdoor != outdoor) continue;

        if(first) {
            lv_obj_t * title = ui_card_title(panel, heading);
            /*Separates this group from the one above it, if any.*/
            if(lv_obj_get_index(title) > 0) lv_obj_set_style_pad_top(title, UI_GAP, LV_PART_MAIN);
            first = false;
        }

        lv_obj_t * option = lv_button_create(panel);
        lv_obj_set_size(option, LV_PCT(100), UI_TOUCH_MIN);
        lv_obj_set_style_radius(option, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(option, UI_GAP, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(option, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(option, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(option, UI_COLOR_TEXT, LV_PART_MAIN);
        if(i == sensor_current) lv_obj_add_state(option, LV_STATE_CHECKED);

        lv_obj_t * label = lv_label_create(option);
        lv_label_set_text(label, sensor_list[i].name);
        lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
        lv_obj_set_width(label, LV_PCT(100));
        ui_label_single_line(label, UI_FONT_SM);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_add_event_cb(option, sensor_option_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
    }
}

static void sensor_option_clicked(lv_event_t * e)
{
    uint32_t index = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);

    sensor_popup_close();

    if(index == sensor_current) return;

    sensor_current = index;
    sensor_name_update();

    /* TODO: the sensor service answers this by refreshing every tile with
     * page_air_quality_set_metric() and every trace with
     * page_air_quality_set_history() for the newly selected sensor. */
    if(sensor_cb) sensor_cb(index);
}

/**
 * Map a raw reading onto the chart's shared 0..PLOT_SCALE axis using the
 * metric's own range, clamped so an outlier cannot draw outside the chart.
 */
static int32_t plot_value(page_aq_metric_t metric, int32_t value)
{
    int32_t min = metrics[metric].plot_min;
    int32_t max = metrics[metric].plot_max;

    if(value == LV_CHART_POINT_NONE) return LV_CHART_POINT_NONE;
    if(max <= min) return 0;

    int32_t scaled = (int32_t)(((int64_t)value - min) * PLOT_SCALE / ((int64_t)max - min));
    return LV_CLAMP(0, scaled, PLOT_SCALE);
}

/** A value axis: a vertical scale beside the chart, hidden until it has a unit. */
static lv_obj_t * axis_create(lv_obj_t * parent, lv_scale_mode_t mode)
{
    lv_obj_t * scale = lv_scale_create(parent);
    lv_obj_set_size(scale, AXIS_WIDTH, LV_PCT(100));
    lv_scale_set_mode(scale, mode);
    lv_scale_set_total_tick_count(scale, AXIS_TICKS);
    lv_scale_set_major_tick_every(scale, 1);
    lv_scale_set_range(scale, 0, AXIS_TICKS - 1);
    lv_scale_set_label_show(scale, true);
    axis_style(scale);
    lv_obj_set_hidden(scale, true);
    return scale;
}

/** Short ticks in the division lines' colour, small dim labels, no spine. */
static void axis_style(lv_obj_t * scale)
{
    lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_length(scale, 4, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 1, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale, UI_COLOR_BORDER, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(scale, UI_FONT_XS, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(scale, UI_COLOR_TEXT_DIM, LV_PART_INDICATOR);
}

/**
 * Give the axes to the units of the plotted traces: the most used on the
 * left, the next on the right, the first plotted winning a tie. Every trace
 * in an axis's unit is drawn on the span of all their ranges, so the axis
 * reads true for each; the rest keep their own. Then every trace is drawn
 * again on its range.
 */
static void axes_update(void)
{
    const char * units[PAGE_AQ_METRIC_COUNT];
    uint32_t     uses[PAGE_AQ_METRIC_COUNT];
    uint32_t     unit_count = 0;
    int32_t      axis_of[PAGE_AQ_METRIC_COUNT];

    for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
        axis_of[i] = -1;

        /*The tiles are still being built on the first calls.*/
        if(!metrics[i].tile || !metrics[i].unit || !lv_obj_has_state(metrics[i].tile, LV_STATE_CHECKED)) continue;

        const char * unit = lv_label_get_text(metrics[i].unit);
        uint32_t     u    = 0;
        while(u < unit_count && strcmp(units[u], unit) != 0) u++;
        if(u == unit_count) {
            units[unit_count] = unit;
            uses[unit_count]  = 0;
            unit_count++;
        }
        uses[u]++;
    }

    int32_t best[2] = {-1, -1};
    for(uint32_t u = 0; u < unit_count; u++) {
        if(best[0] < 0 || uses[u] > uses[best[0]]) {
            best[1] = best[0];
            best[0] = (int32_t)u;
        }
        else if(best[1] < 0 || uses[u] > uses[best[1]]) {
            best[1] = (int32_t)u;
        }
    }

    for(int a = 0; a < 2; a++) {
        bool shown = best[a] >= 0;
        lv_obj_set_hidden(axes[a].scale, !shown);
        lv_obj_set_hidden(axes[a].unit, !shown);
        lv_obj_set_hidden(axes[a].spacer, !shown);
        if(!shown) continue;

        const char * unit = units[best[a]];
        double       low  = HUGE_VAL;
        double       high = -HUGE_VAL;

        for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
            if(!metrics[i].tile || !lv_obj_has_state(metrics[i].tile, LV_STATE_CHECKED)) continue;
            if(strcmp(lv_label_get_text(metrics[i].unit), unit) != 0) continue;

            axis_of[i] = a;
            low        = LV_MIN(low, (double)metrics[i].min / metrics[i].scale);
            high       = LV_MAX(high, (double)metrics[i].max / metrics[i].scale);
        }

        /*Widen the span to even steps, so the labels read 0 5 10 15 20
         *rather than 1 5 9 13 16.*/
        double step = axis_step(high - low);
        double top  = high;
        low         = floor(low / step + 1e-9) * step;
        while((high = low + step * (AXIS_TICKS - 1)) < top - 1e-9) {
            step = axis_step(step * (AXIS_TICKS - 1) + step / 2);
            low  = floor(low / step + 1e-9) * step;
        }

        for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
            if(axis_of[i] != a) continue;
            metrics[i].plot_min = (int32_t)lround(low * metrics[i].scale);
            metrics[i].plot_max = (int32_t)lround(high * metrics[i].scale);
        }

        lv_label_set_text(axes[a].unit, unit);

        for(int t = 0; t < AXIS_TICKS; t++) {
            axis_value_text(axes[a].texts[t], sizeof(axes[a].texts[t]), low + step * t, step);
            axes[a].text_list[t] = axes[a].texts[t];
        }
        axes[a].text_list[AXIS_TICKS] = NULL;
        lv_scale_set_text_src(axes[a].scale, axes[a].text_list);
        lv_obj_invalidate(axes[a].scale);
    }

    for(uint32_t i = 0; i < PAGE_AQ_METRIC_COUNT; i++) {
        if(axis_of[i] < 0) {
            metrics[i].plot_min = metrics[i].min;
            metrics[i].plot_max = metrics[i].max;
        }

        if(!metrics[i].series || metrics[i].count == 0) continue;

        int32_t scaled[PAGE_AQ_HISTORY_POINTS];
        for(uint32_t p = 0; p < metrics[i].count; p++) scaled[p] = plot_value((page_aq_metric_t)i, metrics[i].raw[p]);
        lv_chart_set_series_values(chart, metrics[i].series, scaled, metrics[i].count);
    }
}

/**
 * The step between an axis's labels for a span: 1, 2 or 5 times a power of
 * ten -- 25 and 250 too -- no finer than a tenth, and at least span / (ticks - 1).
 */
static double axis_step(double span)
{
    static const double nice[] = {1.0, 2.0, 2.5, 5.0, 10.0};

    double rough     = LV_MAX(span, 0.1 * (AXIS_TICKS - 1)) / (AXIS_TICKS - 1);
    double magnitude = pow(10.0, floor(log10(rough)));

    for(size_t i = 0; i < sizeof(nice) / sizeof(nice[0]); i++) {
        /*2.5 only where it is still a whole number.*/
        if(nice[i] == 2.5 && magnitude < 10.0) continue;
        if(nice[i] * magnitude >= rough - 1e-9) return nice[i] * magnitude;
    }
    return 10.0 * magnitude;
}

/** A value on an axis: whole numbers, or tenths for steps under one. */
static void axis_value_text(char * buf, size_t size, double value, double step)
{
    if(step >= 1.0) {
        lv_snprintf(buf, size, "%d", (int)lround(value));
        return;
    }

    long tenths = lround(value * 10.0);
    lv_snprintf(buf, size, "%s%ld.%ld", tenths < 0 ? "-" : "", labs(tenths) / 10, labs(tenths) % 10);
}

/**
 * Trace colour for a metric. A switch rather than a table because the theme
 * tokens are lv_color_hex() calls, which cannot initialise a static array.
 */
static lv_color_t metric_color(page_aq_metric_t metric)
{
    switch(metric) {
        case PAGE_AQ_METRIC_PM1:      return UI_COLOR_AQ_PM1;
        case PAGE_AQ_METRIC_PM25:     return UI_COLOR_AQ_PM25;
        case PAGE_AQ_METRIC_CO2:      return UI_COLOR_AQ_CO2;
        case PAGE_AQ_METRIC_VOC:      return UI_COLOR_AQ_VOC;
        case PAGE_AQ_METRIC_TEMP:     return UI_COLOR_AQ_TEMP;
        case PAGE_AQ_METRIC_PM4:      return UI_COLOR_AQ_PM4;
        case PAGE_AQ_METRIC_PM10:     return UI_COLOR_AQ_PM10;
        case PAGE_AQ_METRIC_NOX:      return UI_COLOR_AQ_NOX;
        case PAGE_AQ_METRIC_HCHO:     return UI_COLOR_AQ_HCHO;
        case PAGE_AQ_METRIC_HUMIDITY: return UI_COLOR_AQ_HUMIDITY;
        default:                      return UI_COLOR_TEXT_DIM;
    }
}

static void refresh_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(refresh_cb) refresh_cb();
}
