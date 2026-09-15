/**
 * @file page_air_quality.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/air_quality/page_air_quality.h"
#include "ui/ui_theme.h"

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
static lv_color_t metric_color(page_aq_metric_t metric);

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

/** Tile captions, placeholder contents and default trace range, in
 *  page_aq_metric_t order. Trace colours are in metric_color(). */
static const struct {
    const char * caption;
    const char * value;
    const char * unit;
    int32_t      min;
    int32_t      max;
} metric_defaults[PAGE_AQ_METRIC_COUNT] = {
    [PAGE_AQ_METRIC_PM1]      = {"PM1.0",    "5",   "ug/m3",    0,   40},
    [PAGE_AQ_METRIC_PM25]     = {"PM2.5",    "8",   "ug/m3",    0,   60},
    [PAGE_AQ_METRIC_CO2]      = {"CO2",      "640", "ppm",      400, 2000},
    [PAGE_AQ_METRIC_VOC]      = {"VOC",      "112", "index",    0,   500},
    [PAGE_AQ_METRIC_TEMP]     = {"TEMP",     "21",  UI_DEG "C", 10,  35},
    [PAGE_AQ_METRIC_PM4]      = {"PM4.0",    "11",  "ug/m3",    0,   80},
    [PAGE_AQ_METRIC_PM10]     = {"PM10",     "14",  "ug/m3",    0,   100},
    [PAGE_AQ_METRIC_NOX]      = {"NOx",      "1",   "index",    0,   500},
    [PAGE_AQ_METRIC_HCHO]     = {"HCHO",     "18",  "ppb",      0,   100},
    [PAGE_AQ_METRIC_HUMIDITY] = {"HUMIDITY", "46",  "%",        0,   100},
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
} metrics[PAGE_AQ_METRIC_COUNT];

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
    lv_color_t color = ui_aqi_color(aqi);

    lv_label_set_text_fmt(index_value, "%d", (int)aqi);
    lv_obj_set_style_text_color(index_value, color, LV_PART_MAIN);
    lv_label_set_text(index_band, ui_aqi_band(aqi));
}

void page_air_quality_set_metric(page_aq_metric_t metric, const char * value, const char * unit,
                                 lv_color_t status)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return;

    lv_label_set_text(metrics[metric].value, value);
    lv_label_set_text(metrics[metric].unit, unit);
    lv_obj_set_style_text_color(metrics[metric].value, status, LV_PART_MAIN);
}

void page_air_quality_set_history(page_aq_metric_t metric, const int32_t values[], uint32_t count,
                                  int32_t min, int32_t max)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return;
    if(count > PAGE_AQ_HISTORY_POINTS) count = PAGE_AQ_HISTORY_POINTS;

    metrics[metric].min = min;
    metrics[metric].max = max;

    int32_t scaled[PAGE_AQ_HISTORY_POINTS];
    for(uint32_t i = 0; i < count; i++) {
        scaled[i] = plot_value(metric, values[i]);
    }

    lv_chart_set_series_values(chart, metrics[metric].series, scaled, count);
}

void page_air_quality_push_sample(page_aq_metric_t metric, int32_t value)
{
    if(metric >= PAGE_AQ_METRIC_COUNT) return;

    lv_chart_set_next_value(chart, metrics[metric].series, plot_value(metric, value));
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
        metrics[i].value = ui_label_create(reading, metric_defaults[i].value, UI_FONT_LG, UI_COLOR_TEXT);
        metrics[i].unit  = ui_label_create(reading, metric_defaults[i].unit, UI_FONT_XS, UI_COLOR_TEXT_DIM);
        /*Roughly lines the unit up with the value's baseline.*/
        lv_obj_set_style_pad_bottom(metrics[i].unit, 5, LV_PART_MAIN);

        metrics[i].series = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
        metrics[i].min    = metric_defaults[i].min;
        metrics[i].max    = metric_defaults[i].max;

        /*Placeholder trace so the card reads correctly before real samples
         *land. Two superimposed sines, phase-shifted per metric so the traces
         *do not sit on top of each other.*/
        int32_t phase = (int32_t)i * 37;
        for(uint32_t p = 0; p < PAGE_AQ_HISTORY_POINTS; p++) {
            int32_t angle = (int32_t)p * 360 / PAGE_AQ_HISTORY_POINTS;
            int32_t value = PLOT_SCALE / 2
                            + (lv_trigo_sin((int16_t)(angle * 2 + phase)) * 250) / LV_TRIGO_SIN_MAX
                            + (lv_trigo_sin((int16_t)(angle * 5 + 40 + phase * 3)) * 110) / LV_TRIGO_SIN_MAX;
            lv_chart_set_next_value(chart, metrics[i].series, value);
        }

        /*PM2.5 is the one worth watching by default.*/
        page_air_quality_set_plotted((page_aq_metric_t)i, i == PAGE_AQ_METRIC_PM25);
    }
}

static void sensor_button_create(lv_obj_t * parent)
{
    /*Placeholder list until the sensor service registers the real ones.
     *Deliberately interleaved: the popup does the grouping.*/
    static const page_aq_sensor_t placeholder[] = {
        {"Bedroom",     false},
        {"Living Room", false},
        {"Garden",      true},
        {"Kitchen",     false},
        {"Balcony",     true},
    };

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

    page_air_quality_set_sensors(placeholder, sizeof(placeholder) / sizeof(placeholder[0]));
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

    chart = lv_chart_create(card);
    lv_obj_set_width(chart, LV_PCT(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, PAGE_AQ_HISTORY_POINTS);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, PLOT_SCALE);
    lv_chart_set_div_line_count(chart, 5, 8);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);  /*No point markers*/
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    /*Stands in for the traces when every tile is switched off.*/
    chart_hint = ui_label_create(chart, "Tap a reading above to plot it", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_center(chart_hint);

    range_select(range_current);
}

static void analysis_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    ui_card_title(card, "ANALYSIS");

    index_value = ui_label_create(card, "42", UI_FONT_XL, UI_COLOR_GOOD);
    /*Band names run long ("Unhealthy for sensitive groups"), so this one is
     *deliberately allowed to wrap rather than being ellipsised.*/
    index_band = ui_label_create(card, "Good", UI_FONT_SM, UI_COLOR_TEXT);
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

    stat_trend = ui_label_create(card, "Steady", UI_FONT_SM, UI_COLOR_ACCENT);

    advice_label = ui_label_create(card, "Air quality is satisfactory.", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_label_set_long_mode(advice_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(advice_label, LV_PCT(100));
}

static void forecast_card_create(lv_obj_t * parent)
{
    /*Spread across the AQI bands so the colour coding is visible up front.*/
    static const struct {
        const char * when;
        int32_t      aqi;
    } placeholder[PAGE_AQ_FORECAST_SLOTS] = {
        {"Now", 42}, {"+3h", 48}, {"+6h", 67}, {"+9h", 104}, {"+12h", 88}, {"Tomorrow", 51},
    };

    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 2, 1);

    ui_card_title(card, "FORECAST");

    lv_obj_t * strip = transparent_box_create(card);
    lv_obj_set_width(strip, LV_PCT(100));
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for(uint32_t i = 0; i < PAGE_AQ_FORECAST_SLOTS; i++) {
        lv_obj_t * col = transparent_box_create(strip);
        lv_obj_set_style_pad_row(col, 4, LV_PART_MAIN);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t * when = ui_label_create(col, placeholder[i].when, UI_FONT_XS, UI_COLOR_TEXT_DIM);

        lv_obj_t * pill = lv_obj_create(col);
        lv_obj_set_size(pill, 62, 38);
        lv_obj_set_style_bg_color(pill, ui_aqi_color(placeholder[i].aqi), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(pill, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pill, 0, LV_PART_MAIN);
        lv_obj_set_scrollable(pill, false);

        lv_obj_t * value = lv_label_create(pill);
        lv_label_set_text_fmt(value, "%d", (int)placeholder[i].aqi);
        lv_obj_set_style_text_font(value, UI_FONT_MD, LV_PART_MAIN);
        lv_obj_set_style_text_color(value, lv_color_black(), LV_PART_MAIN);
        lv_obj_center(value);

        forecast[i].root  = col;
        forecast[i].when  = when;
        forecast[i].pill  = pill;
        forecast[i].value = value;
    }
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
    int32_t min = metrics[metric].min;
    int32_t max = metrics[metric].max;

    if(max <= min) return 0;

    int32_t scaled = (int32_t)(((int64_t)value - min) * PLOT_SCALE / ((int64_t)max - min));
    return LV_CLAMP(0, scaled, PLOT_SCALE);
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
