/**
 * @file page_clock.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/clock/page_clock.h"
#include "ui/ui.h"
#include "ui/ui_format.h"
#include "ui/ui_theme.h"
#include "settings/clock_time.h"

/*********************
 *      DEFINES
 *********************/

/** Length of the ambient <-> active transition. */
#define TRANSITION_MS 450

/** Widest the next alarm's name gets on its chip before it ends in dots. */
#define ALARM_NAME_MAX_WIDTH 320

/** Height of the precipitation graph's plot area. */
#define RAIN_PLOT_HEIGHT 140

/** Thickness of the curve drawn along the top of the filled area. */
#define RAIN_LINE_WIDTH 2

/** Breathing room above the plot so a full-scale reading is not clipped. */
#define RAIN_CHART_HEADROOM 5

/** Marks every quarter hour across the two hours, inclusive of both ends. */
#define RAIN_TICK_COUNT 9

/** Every second tick is major, so the half hours stand taller than the
 *  quarters. Only the hours carry a label. */
#define RAIN_TICK_MAJOR_EVERY 2

/** Tick lengths, major and minor. */
#define RAIN_TICK_MAJOR 8
#define RAIN_TICK_MINOR 4

/** Room under the plot for the ticks and their labels. */
#define RAIN_SCALE_HEIGHT 30

/** lv_scale centres a label on its tick, so the last one overhangs the end of
 *  the axis. Both the plot and the scale are inset by this much to give it
 *  somewhere to go -- equally, so they stay aligned with each other. */
#define RAIN_LABEL_MARGIN 16

/** Height of the drawn thermometer beside the indoor temperature. */
#define SENSOR_ICON_HEIGHT 30

/** Side of the drawn condition icon beside the temperature. */
#define WEATHER_ICON_SIZE 44

/** Width of the gutter holding the Light/Heavy scale labels. */
#define RAIN_AXIS_WIDTH 42

/** The tab naming the city the forecast is for: how much of it the section's
 *  right border leaves showing, and the longest name it stretches to. */
#define PLACE_TAB_SHOWN    26
#define PLACE_TAB_MAX_NAME 150

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);
static void       on_show(void);

static void divider_grad_init(void);
static void clock_block_create(lv_obj_t * parent);
static void weather_section_create(lv_obj_t * parent);
static void calendar_section_create(lv_obj_t * parent);
static void air_section_create(lv_obj_t * parent);

static lv_color_t calendar_color(page_clock_calendar_t calendar);
static int32_t    rain_bar_percent(page_clock_rain_level_t level);

static lv_obj_t * divider_create(lv_obj_t * parent, bool vertical);
static lv_obj_t * flow_create(lv_obj_t * parent, lv_flex_flow_t flow);
static lv_obj_t * section_create(lv_obj_t * parent, const char * caption);
static lv_obj_t * stat_create(lv_obj_t * parent, const char * caption, const char * value);
static lv_obj_t * thermometer_create(lv_obj_t * parent, lv_color_t color, int32_t height);
static lv_obj_t * sensor_stat_create(lv_obj_t * parent, const char * glyph, const char * value);

static void geometry_refresh(void);
static void mode_apply(bool value, bool animate);
static void detail_opa_set(void * obj, int32_t value);
static void detail_faded_out(lv_anim_t * a);
static void wake_event(lv_event_t * e);
static void alarm_chip_clicked(lv_event_t * e);
static void weather_section_clicked(lv_event_t * e);
static void air_section_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "Home",
    .icon    = LV_SYMBOL_HOME,
    .create  = create,
    .on_show = on_show,
    .on_hide = NULL,
};

/*Placeholder calendar, until there is a calendar service. Deliberately varied
 *in length so that layout problems with real data show up here rather than
 *on the device.*/

/*One merged list, as the application would pass it: today's schedule plus the
 *next fortnight of all-day entries. The routing table splits it.
 *
 *Kept as times of day and days from today, and formatted when the page is
 *built, so the samples follow the clock and date settings as real entries
 *would. A negative hour is an all-day entry; a negative day is today's.*/
static const struct {
    int8_t                hour;
    uint8_t               minute;
    int8_t                days_ahead;
    const char *          title;
    page_clock_calendar_t calendar;
} event_samples[] = {
    {8,  15, -1, "Leave for the office", PAGE_CLOCK_CAL_PERSONAL},
    {9,  30, -1, "Team standup",         PAGE_CLOCK_CAL_WORK},
    {11, 0,  -1, "Design review",        PAGE_CLOCK_CAL_WORK},
    {13, 0,  -1, "Lunch with Sam",       PAGE_CLOCK_CAL_PERSONAL},
    {-1, 0,  -1, "Recycling collection", PAGE_CLOCK_CAL_PERSONAL},
    {-1, 0,  0,  "Maria's birthday",     PAGE_CLOCK_CAL_OCCASION},
    {-1, 0,  1,  "Name day: Nikos",      PAGE_CLOCK_CAL_OCCASION},
    {-1, 0,  3,  "Wedding anniversary",  PAGE_CLOCK_CAL_OCCASION},
    {-1, 0,  7,  "Independence Day",     PAGE_CLOCK_CAL_HOLIDAY},
    {-1, 0,  12, "Dad's birthday",       PAGE_CLOCK_CAL_OCCASION},
};
#define EVENT_SAMPLE_COUNT (sizeof(event_samples) / sizeof(event_samples[0]))

static page_clock_event_t event_defaults[EVENT_SAMPLE_COUNT];
static char               event_text[EVENT_SAMPLE_COUNT][24];

/** Format the sample events for today, and hand them to the page. */
static void events_seed(void)
{
    struct tm date;
    clock_time_now(&date);

    int days = 0;

    for(uint32_t i = 0; i < EVENT_SAMPLE_COUNT; i++) {
        event_defaults[i].title    = event_samples[i].title;
        event_defaults[i].calendar = event_samples[i].calendar;
        event_defaults[i].time     = NULL;
        event_defaults[i].day      = NULL;

        if(event_samples[i].hour >= 0) {
            ui_format_time(event_text[i], sizeof(event_text[i]), event_samples[i].hour, event_samples[i].minute);
            event_defaults[i].time = event_text[i];
            continue;
        }
        if(event_samples[i].days_ahead < 0) {
            event_defaults[i].time = "All day";
            continue;
        }
        if(event_samples[i].days_ahead == 0) {
            event_defaults[i].day = "Today";
            continue;
        }

        /*The samples run in date order, so walk the calendar forward.*/
        for(; days < event_samples[i].days_ahead; days++) {
            date.tm_wday = (date.tm_wday + 1) % 7;
            if(++date.tm_mday > clock_time_days_in_month(date.tm_year + 1900, date.tm_mon)) {
                date.tm_mday = 1;
                if(++date.tm_mon > 11) {
                    date.tm_mon = 0;
                    date.tm_year++;
                }
            }
        }
        ui_format_date_short(event_text[i], sizeof(event_text[i]), &date);
        event_defaults[i].day = event_text[i];
    }

    page_clock_set_events(event_defaults, EVENT_SAMPLE_COUNT);
}

/*Configuration, not a user-facing setting yet: which column each calendar
 *lands in. Today's schedule on the left, the fortnight ahead on the right.*/
static page_clock_column_t calendar_column[PAGE_CLOCK_CAL_COUNT] = {
    [PAGE_CLOCK_CAL_HOLIDAY]  = PAGE_CLOCK_COLUMN_UPCOMING,
    [PAGE_CLOCK_CAL_OCCASION] = PAGE_CLOCK_COLUMN_UPCOMING,
    [PAGE_CLOCK_CAL_PERSONAL] = PAGE_CLOCK_COLUMN_TODAY,
    [PAGE_CLOCK_CAL_WORK]     = PAGE_CLOCK_COLUMN_TODAY,
};

/*Every divider on this page fades out at both ends, so none of them reads as
 *the edge of a box. Needs LV_GRADIENT_MAX_STOPS >= 3, and
 *lv_obj_set_style_bg_grad keeps the pointer, so these have to outlive the
 *widgets that use them.*/
static lv_grad_dsc_t divider_grad_ver;  /**< For vertical hairlines */
static lv_grad_dsc_t divider_grad_hor;  /**< For horizontal hairlines */

/*State*/
static bool ambient = true;

/*Where the clock block rests in each state, relative to `root`. Recomputed
 *whenever the page is laid out or the clock text changes width.*/
static lv_point_t ambient_pos;
static lv_point_t active_pos;

/*Structure*/
static lv_obj_t * root;
static lv_obj_t * detail;       /**< Everything that is hidden while ambient */
static lv_obj_t * clock_anchor; /**< Invisible cell reserving the clock's active slot */
static lv_obj_t * clock_block;  /**< Free-positioned, animated between the two states */

/*Widgets this page updates later. Everything else is build-once.*/
static lv_obj_t * time_label;
static lv_obj_t * seconds_label;
static lv_obj_t * meridiem_label;
static lv_obj_t * date_label;
static lv_obj_t * alarm_chip;
static lv_obj_t * alarm_name;
static lv_obj_t * alarm_time;
static lv_obj_t * alarm_when;

static lv_obj_t * weather_icon;
static lv_obj_t * weather_temp;
static lv_obj_t * weather_condition;
static lv_obj_t * weather_real_feel;
static lv_obj_t * weather_humidity;
static lv_obj_t * weather_place;
static lv_obj_t * weather_place_tab;
static lv_obj_t * weather_status;

static lv_obj_t *          rain_chart;
static lv_chart_series_t * rain_series;
static lv_obj_t *          rain_summary;

static struct {
    lv_obj_t * root;
    lv_obj_t * accent;
    lv_obj_t * when;
    lv_obj_t * title;
} event_slot[PAGE_CLOCK_COLUMN_COUNT][PAGE_CLOCK_EVENTS_PER_COLUMN];

static lv_obj_t * air_arc;
static lv_obj_t * air_value;
static lv_obj_t * air_band;
static lv_obj_t * air_pm25;
static lv_obj_t * air_co2;
static lv_obj_t * air_temp;
static lv_obj_t * air_humidity;
static lv_obj_t * air_status;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const ui_page_t * page_clock_desc(void)
{
    return &desc;
}

void page_clock_set_ambient(bool value)
{
    if(value == ambient) return;
    mode_apply(value, true);
}

bool page_clock_is_ambient(void)
{
    return ambient;
}

void page_clock_set_time(const char * time_text, const char * seconds, const char * meridiem)
{
    lv_label_set_text(time_label, time_text);

    if(seconds) {
        lv_label_set_text(seconds_label, seconds);
        lv_obj_set_hidden(seconds_label, false);
    }
    else {
        lv_obj_set_hidden(seconds_label, true);
    }

    if(meridiem) {
        lv_label_set_text(meridiem_label, meridiem);
        lv_obj_set_hidden(meridiem_label, false);
    }
    else {
        lv_obj_set_hidden(meridiem_label, true);
    }

    /*The block changes width with the text, so the centring has to follow.*/
    geometry_refresh();
}

void page_clock_set_date(const char * date_text)
{
    lv_label_set_text(date_label, date_text);
    geometry_refresh();
}

void page_clock_set_next_alarm(const char * name, const char * time, const char * when,
                               bool enabled)
{
    if(!name) {
        lv_obj_set_hidden(alarm_chip, true);
    }
    else {
        lv_color_t colour = enabled ? UI_COLOR_ACCENT : UI_COLOR_TEXT_DIM;

        lv_obj_set_hidden(alarm_chip, false);
        lv_label_set_text(alarm_name, name);
        lv_label_set_text(alarm_time, time ? time : "");
        lv_label_set_text(alarm_when, when ? when : "");

        lv_obj_set_style_text_color(alarm_name, colour, LV_PART_MAIN);
        lv_obj_set_style_text_color(alarm_time, colour, LV_PART_MAIN);
    }

    geometry_refresh();
}

void page_clock_set_weather_now(ui_weather_t icon, const char * temp, const char * condition,
                                const char * real_feel, const char * humidity)
{
    ui_weather_icon_set(weather_icon, icon);
    lv_label_set_text(weather_temp, temp);
    lv_label_set_text(weather_condition, condition);
    lv_label_set_text(weather_real_feel, real_feel);
    lv_label_set_text(weather_humidity, humidity);
}

void page_clock_set_weather_place(const char * name)
{
    bool shown = name && name[0];

    lv_obj_set_hidden(weather_place_tab, !shown);
    if(!shown) return;

    lv_label_set_text(weather_place, name);

    /*The tab is as long as the name is wide: the turn is drawn, so the layout
     *still has the label lying down.*/
    lv_obj_update_layout(weather_place);
    lv_obj_set_height(weather_place_tab, lv_obj_get_width(weather_place) + UI_GAP);
}

void page_clock_set_rain(const page_clock_rain_level_t levels[], uint32_t count,
                         const char * summary)
{
    if(count > PAGE_CLOCK_RAIN_SEGMENTS) count = PAGE_CLOCK_RAIN_SEGMENTS;

    int32_t points[PAGE_CLOCK_RAIN_SEGMENTS];

    for(uint32_t i = 0; i < PAGE_CLOCK_RAIN_SEGMENTS; i++) {
        points[i] = rain_bar_percent((i < count) ? levels[i] : PAGE_CLOCK_RAIN_NONE);
    }

    /*The chart keeps the values and redraws itself; nothing else holds a copy.*/
    lv_chart_set_series_values(rain_chart, rain_series, points, PAGE_CLOCK_RAIN_SEGMENTS);

    if(summary) lv_label_set_text(rain_summary, summary);
}

void page_clock_set_calendar_column(page_clock_calendar_t calendar, page_clock_column_t column)
{
    if(calendar >= PAGE_CLOCK_CAL_COUNT || column >= PAGE_CLOCK_COLUMN_COUNT) return;
    calendar_column[calendar] = column;
}

page_clock_column_t page_clock_get_calendar_column(page_clock_calendar_t calendar)
{
    if(calendar >= PAGE_CLOCK_CAL_COUNT) return PAGE_CLOCK_COLUMN_TODAY;
    return calendar_column[calendar];
}

void page_clock_set_events(const page_clock_event_t events[], uint32_t count)
{
    uint32_t filled[PAGE_CLOCK_COLUMN_COUNT] = {0};

    for(uint32_t i = 0; i < count; i++) {
        if(events[i].calendar >= PAGE_CLOCK_CAL_COUNT) continue;

        page_clock_column_t column = calendar_column[events[i].calendar];
        uint32_t            row    = filled[column];

        /*Columns fill independently, so a busy day cannot push the fortnight
         *ahead off the page, or the other way round.*/
        if(row >= PAGE_CLOCK_EVENTS_PER_COLUMN) continue;
        filled[column]++;

        /*The right column is made of all-day entries, so it is labelled with
         *the day rather than a time. Fall back if the caller only filled one.*/
        const char * when = (column == PAGE_CLOCK_COLUMN_UPCOMING)
                            ? (events[i].day ? events[i].day : events[i].time)
                            : (events[i].time ? events[i].time : events[i].day);

        lv_obj_set_hidden(event_slot[column][row].root, false);
        lv_label_set_text(event_slot[column][row].when, when ? when : "");
        lv_label_set_text(event_slot[column][row].title, events[i].title);
        lv_obj_set_style_bg_color(event_slot[column][row].accent,
                                  calendar_color(events[i].calendar), LV_PART_MAIN);
    }

    for(uint32_t c = 0; c < PAGE_CLOCK_COLUMN_COUNT; c++) {
        for(uint32_t r = filled[c]; r < PAGE_CLOCK_EVENTS_PER_COLUMN; r++) {
            lv_obj_set_hidden(event_slot[c][r].root, true);
        }
    }
}

void page_clock_set_air_summary(int32_t aqi, const char * pm25, const char * co2)
{
    bool       known = aqi >= 0;
    lv_color_t color = known ? ui_aqi_color(aqi) : UI_COLOR_TEXT_DIM;

    lv_arc_set_value(air_arc, known ? aqi : 0);
    lv_obj_set_style_arc_color(air_arc, color, LV_PART_INDICATOR);

    if(known) lv_label_set_text_fmt(air_value, "%d", (int)aqi);
    else      lv_label_set_text(air_value, "--");
    lv_obj_set_style_text_color(air_value, color, LV_PART_MAIN);
    lv_label_set_text(air_band, known ? ui_aqi_band(aqi) : "");
    lv_label_set_text(air_pm25, pm25);
    lv_label_set_text(air_co2, co2);
}

void page_clock_set_indoor(const char * temperature, const char * humidity)
{
    lv_label_set_text(air_temp, temperature);
    lv_label_set_text(air_humidity, humidity);
}

void page_clock_set_weather_state(ui_status_state_t state, const char * message)
{
    ui_status_set(weather_status, state, message);
}

void page_clock_set_air_state(ui_status_state_t state, const char * message)
{
    ui_status_set(air_status, state, message);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create(lv_obj_t * parent)
{
    /*The two sides split their height differently -- the clock wants very
     *little and the forecast wants a lot -- so each gets its own grid rather
     *than sharing rows across the page.*/
    static const int32_t outer_cols[] = {LV_GRID_FR(7), 1, LV_GRID_FR(4), LV_GRID_TEMPLATE_LAST};
    static const int32_t outer_rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    static const int32_t side_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    /*Left: three tenths to the time and date, seven to the events.*/
    static const int32_t left_rows[]  = {LV_GRID_FR(3), 1, LV_GRID_FR(7), LV_GRID_TEMPLATE_LAST};
    /*Right: three fifths to the forecast, two to the indoor sensors.*/
    static const int32_t right_rows[] = {LV_GRID_FR(3), 1, LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};

    divider_grad_init();

    root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(root, false);

    /*A press anywhere on the empty ambient face brings the detail back.*/
    lv_obj_set_clickable(root, true);
    lv_obj_add_event_cb(root, wake_event, LV_EVENT_PRESSED, NULL);

    detail = lv_obj_create(root);
    lv_obj_set_size(detail, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(detail, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(detail, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(detail, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(detail, UI_GAP * 2, LV_PART_MAIN);
    lv_obj_set_scrollable(detail, false);
    lv_obj_set_grid_dsc_array(detail, outer_cols, outer_rows);

    lv_obj_t * left = flow_create(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(left, LV_PCT(100), LV_PCT(100));
    lv_obj_set_grid_cell(left, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_pad_row(left, UI_GAP, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(left, side_cols, left_rows);

    /*Full-height hairline splitting the clock/calendar side from the
     *weather/air side.*/
    lv_obj_t * split = divider_create(detail, true);
    lv_obj_set_grid_cell(split, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    lv_obj_t * right = flow_create(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(right, LV_PCT(100), LV_PCT(100));
    lv_obj_set_grid_cell(right, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_pad_row(right, UI_GAP, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(right, side_cols, right_rows);

    /*Reserves the clock's slot in the grid without drawing anything; the real
     *clock block is a free-positioned sibling so it can animate across it.*/
    clock_anchor = lv_obj_create(left);
    lv_obj_set_grid_cell(clock_anchor, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_bg_opa(clock_anchor, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(clock_anchor, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(clock_anchor, false);
    lv_obj_set_clickable(clock_anchor, false);

    /*One rule per side rather than a single full-width one, so the two never
     *cross and the page keeps reading as regions instead of a table.*/
    lv_obj_t * rule_left = divider_create(left, false);
    lv_obj_set_grid_cell(rule_left, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);

    lv_obj_t * rule_right = divider_create(right, false);
    lv_obj_set_grid_cell(rule_right, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);

    weather_section_create(right);
    calendar_section_create(left);
    air_section_create(right);

    clock_block_create(root);

    /*The feeds fill in the time, the next alarm, the weather and the air as
     *soon as they start. The calendar has no service yet, so it shows samples.*/
    events_seed();

    /*Start ambient, without animating into it.*/
    mode_apply(true, false);

    return root;
}

static void on_show(void)
{
    /*The page is only laid out once it is visible, so this is the first point
     *at which the clock's two resting positions can be measured.*/
    geometry_refresh();
    mode_apply(ambient, false);

    /* TODO: start the 1 Hz tick that calls page_clock_set_time(), and ask the
     * weather / calendar / sensor services for a refresh. */
}

static void divider_grad_init(void)
{
    static const lv_opa_t opa[3]   = {LV_OPA_TRANSP, LV_OPA_COVER, LV_OPA_TRANSP};
    static const uint8_t  fracs[3] = {0, 128, 255};
    lv_color_t            colors[3];

    colors[0] = UI_COLOR_BORDER;
    colors[1] = UI_COLOR_BORDER;
    colors[2] = UI_COLOR_BORDER;

    lv_grad_init_stops(&divider_grad_ver, colors, opa, fracs, 3);
    divider_grad_ver.dir = LV_GRAD_DIR_VER;

    lv_grad_init_stops(&divider_grad_hor, colors, opa, fracs, 3);
    divider_grad_hor.dir = LV_GRAD_DIR_HOR;
}

/**
 * A one-pixel hairline that fades to nothing at both ends. Caller places it in
 * the grid; this only sets the thickness on the axis it does not span.
 */
static lv_obj_t * divider_create(lv_obj_t * parent, bool vertical)
{
    lv_obj_t * obj = lv_obj_create(parent);

    if(vertical) lv_obj_set_width(obj, 1);
    else         lv_obj_set_height(obj, 1);

    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(obj, vertical ? &divider_grad_ver : &divider_grad_hor, LV_PART_MAIN);
    lv_obj_set_clickable(obj, false);

    return obj;
}

/**
 * The colour that stands in for a calendar's name.
 */
static lv_color_t calendar_color(page_clock_calendar_t calendar)
{
    switch(calendar) {
        case PAGE_CLOCK_CAL_HOLIDAY:  return UI_COLOR_CAL_HOLIDAY;
        case PAGE_CLOCK_CAL_OCCASION: return UI_COLOR_CAL_OCCASION;
        case PAGE_CLOCK_CAL_WORK:     return UI_COLOR_CAL_WORK;
        case PAGE_CLOCK_CAL_PERSONAL:
        default:                      return UI_COLOR_CAL_PERSONAL;
    }
}

/**
 * Where a forecast level sits on the chart, as a percentage of full scale.
 * Dry minutes rest on the axis.
 */
static int32_t rain_bar_percent(page_clock_rain_level_t level)
{
    switch(level) {
        case PAGE_CLOCK_RAIN_LIGHT:    return 30;
        case PAGE_CLOCK_RAIN_MODERATE: return 55;
        case PAGE_CLOCK_RAIN_HEAVY:    return 78;
        case PAGE_CLOCK_RAIN_EXTREME:  return 100;
        case PAGE_CLOCK_RAIN_NONE:
        default:                       return 0;
    }
}

/**
 * Transparent, non-scrolling, non-clickable container. The page is built from
 * these rather than from cards -- the ambient state has to be able to dissolve
 * into the active one without a grid of outlines appearing.
 */
static lv_obj_t * flow_create(lv_obj_t * parent, lv_flex_flow_t flow)
{
    lv_obj_t * obj = lv_obj_create(parent);

    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(obj, 6, LV_PART_MAIN);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_flex_flow(obj, flow);

    return obj;
}

/**
 * A titled region of the page: a dim caption with the content stacked below,
 * the whole stack centred in whatever cell the caller places it in.
 */
static lv_obj_t * section_create(lv_obj_t * parent, const char * caption)
{
    lv_obj_t * obj = flow_create(parent, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(obj, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if(caption) {
        lv_obj_t * label = ui_label_create(obj, caption, UI_FONT_XS, UI_COLOR_TEXT_DIM);
        lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    }

    return obj;
}

/**
 * A region whose caption is pinned to the top of its cell, with the content
 * filling everything below it. The plain section_create() centres its whole
 * stack instead, which is wrong when the content wants the height.
 *
 * @param caption   heading text
 * @param body      receives the container to put the content in
 * @return          the section, for the caller to place in its grid cell
 */
static lv_obj_t * header_section_create(lv_obj_t * parent, const char * caption, lv_obj_t ** body)
{
    lv_obj_t * section = flow_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(section, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(section, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t * label = ui_label_create(section, caption, UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);

    *body = flow_create(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(*body, LV_PCT(100));
    lv_obj_set_flex_grow(*body, 1);
    lv_obj_set_flex_align(*body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    return section;
}

/**
 * A caption over a value, for the small readouts beside the air quality arc.
 */
static lv_obj_t * stat_create(lv_obj_t * parent, const char * caption, const char * value)
{
    lv_obj_t * obj = flow_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(obj, 1);

    ui_label_create(obj, caption, UI_FONT_XS, UI_COLOR_TEXT_DIM);
    return ui_label_create(obj, value, UI_FONT_SM, UI_COLOR_TEXT);
}

/**
 * A thermometer, built from two small objects because the built-in font has no
 * glyph for one: a rounded stem sitting on a round bulb.
 */
static lv_obj_t * thermometer_create(lv_obj_t * parent, lv_color_t color, int32_t height)
{
    lv_obj_t * icon = flow_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(icon, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(icon, 0, LV_PART_MAIN);

    int32_t bulb = (height * 5) / 12;
    int32_t stem = height - bulb;

    const int32_t part_w[2] = {bulb / 2, bulb};
    const int32_t part_h[2] = {stem, bulb};

    for(uint32_t i = 0; i < 2; i++) {
        lv_obj_t * part = lv_obj_create(icon);
        lv_obj_set_size(part, part_w[i], part_h[i]);
        lv_obj_set_style_radius(part, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(part, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(part, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(part, 0, LV_PART_MAIN);
        lv_obj_set_scrollable(part, false);
        lv_obj_set_clickable(part, false);
    }

    return icon;
}

/**
 * A readout from an on-board sensor: a large icon beside a large reading, with
 * no caption -- the icon alone says which sensor it is.
 * @param glyph   symbol to use, or NULL to draw a thermometer instead
 */
static lv_obj_t * sensor_stat_create(lv_obj_t * parent, const char * glyph, const char * value)
{
    /*Content width, not grown: the row spreads the readouts apart itself.*/
    lv_obj_t * stat = flow_create(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stat, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(stat, UI_GAP, LV_PART_MAIN);

    if(glyph) ui_label_create(stat, glyph, UI_FONT_LG, UI_COLOR_ACCENT);
    else      thermometer_create(stat, UI_COLOR_ACCENT, SENSOR_ICON_HEIGHT);

    return ui_label_create(stat, value, UI_FONT_LG, UI_COLOR_TEXT);
}

static void clock_block_create(lv_obj_t * parent)
{
    clock_block = flow_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(clock_block, 4, LV_PART_MAIN);
    lv_obj_set_flex_align(clock_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /*Time, with the seconds and meridiem sitting on its baseline.*/
    lv_obj_t * row = flow_create(clock_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    time_label     = ui_label_create(row, "07:24", UI_FONT_CLOCK, UI_COLOR_TEXT);
    seconds_label  = ui_label_create(row, "31",    UI_FONT_LG,    UI_COLOR_TEXT_DIM);
    meridiem_label = ui_label_create(row, "AM",    UI_FONT_MD,    UI_COLOR_TEXT_DIM);

    date_label = ui_label_create(clock_block, "Saturday, 13 September", UI_FONT_MD, UI_COLOR_TEXT_DIM);

    /*Next-alarm chip: the one pill on the page, because it is a status rather
     *than a heading.*/
    alarm_chip = flow_create(clock_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(alarm_chip, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alarm_chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(alarm_chip, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(alarm_chip, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(alarm_chip, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(alarm_chip, 8, LV_PART_MAIN);
    lv_obj_set_style_margin_top(alarm_chip, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_flex_align(alarm_chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(alarm_chip, true);
    lv_obj_add_event_cb(alarm_chip, alarm_chip_clicked, LV_EVENT_CLICKED, NULL);

    ui_label_create(alarm_chip, LV_SYMBOL_BELL, UI_FONT_SM, UI_COLOR_ACCENT);
    alarm_name = ui_label_create(alarm_chip, "", UI_FONT_SM, UI_COLOR_ACCENT);
    /*A name can run to PAGE_ALARMS_NAME_CHARS: past this it ends in dots
     *rather than pushing the time off the chip.*/
    lv_obj_set_style_max_width(alarm_name, ALARM_NAME_MAX_WIDTH, LV_PART_MAIN);
    ui_label_single_line(alarm_name, UI_FONT_SM);
    alarm_time = ui_label_create(alarm_chip, "", UI_FONT_SM, UI_COLOR_ACCENT);
    alarm_when = ui_label_create(alarm_chip, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
}

static void weather_section_create(lv_obj_t * parent)
{
    /*No caption: the readings speak for themselves and the room is better
     *spent on the forecast.*/
    lv_obj_t * section = section_create(parent, NULL);
    lv_obj_set_grid_cell(section, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    /*The whole section is a shortcut into the full forecast.*/
    lv_obj_set_clickable(section, true);
    lv_obj_add_event_cb(section, weather_section_clicked, LV_EVENT_CLICKED, NULL);

    /*Where the forecast is for, on a tab at the section's right border, out of
     *the stack below. Half the tab is past the border, so only its rounded
     *left side shows, as if it were slid under the edge. It is drawn in the
     *text colour with the name in the background's -- dark on light, light on
     *dark -- so the corner reads as a marker rather than another reading.*/
    weather_place_tab = lv_obj_create(section);
    lv_obj_set_floating(weather_place_tab, true);
    lv_obj_set_size(weather_place_tab, PLACE_TAB_SHOWN * 2, PLACE_TAB_SHOWN * 2);
    lv_obj_set_style_bg_color(weather_place_tab, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(weather_place_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(weather_place_tab, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(weather_place_tab, UI_RADIUS - 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(weather_place_tab, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(weather_place_tab, false);
    lv_obj_set_clickable(weather_place_tab, false);
    lv_obj_align(weather_place_tab, LV_ALIGN_TOP_RIGHT, PLACE_TAB_SHOWN, 0);
    lv_obj_set_hidden(weather_place_tab, true);

    /*A quarter turn anticlockwise about its centre, so the name reads bottom
     *to top, centred on the half of the tab that shows.*/
    weather_place = ui_label_create(weather_place_tab, "", UI_FONT_XS, UI_COLOR_BG);
    lv_obj_set_style_max_width(weather_place, PLACE_TAB_MAX_NAME, LV_PART_MAIN);
    ui_label_single_line(weather_place, UI_FONT_XS);
    lv_obj_set_style_transform_pivot_x(weather_place, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(weather_place, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(weather_place, 2700, LV_PART_MAIN);
    lv_obj_align(weather_place, LV_ALIGN_CENTER, -PLACE_TAB_SHOWN / 2, 0);

    /*Conditions first: temperature with the icon, then RealFeel and humidity
     *as a pair of small readouts underneath.*/
    lv_obj_t * head = flow_create(section, LV_FLEX_FLOW_ROW);
    /*Full width, not content width: the condition label grows into whatever
     *the icon and temperature leave over, and flex_grow needs real space to
     *grow into.*/
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, UI_GAP, LV_PART_MAIN);

    weather_icon = ui_weather_icon_create(head, WEATHER_ICON_SIZE, UI_WEATHER_CLEAR);
    weather_temp = ui_label_create(head, "--", UI_FONT_XL, UI_COLOR_TEXT);

    weather_condition = ui_label_create(head, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_flex_grow(weather_condition, 1);
    ui_label_single_line(weather_condition, UI_FONT_SM);

    lv_obj_t * readouts = flow_create(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(readouts, LV_PCT(100));

    weather_real_feel = stat_create(readouts, "REALFEEL", "--");
    weather_humidity  = stat_create(readouts, "HUMIDITY", "--");

    lv_obj_t * caption = ui_label_create(section, "MINUTECAST", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_margin_top(caption, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(caption, 1, LV_PART_MAIN);

    rain_summary = ui_label_create(section, "", UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_width(rain_summary, LV_PCT(100));
    ui_label_single_line(rain_summary, UI_FONT_SM);

    /*Plot area with the intensity scale in a gutter down its left edge. No gap
     *between the two, so the ruler below lines up with the plot exactly.*/
    lv_obj_t * chart_row = flow_create(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(chart_row, LV_PCT(100), RAIN_PLOT_HEIGHT);
    lv_obj_set_style_pad_column(chart_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(chart_row, RAIN_LABEL_MARGIN, LV_PART_MAIN);

    lv_obj_t * axis = flow_create(chart_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(axis, RAIN_AXIS_WIDTH, LV_PCT(100));
    lv_obj_set_flex_align(axis, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    ui_label_create(axis, "Heavy", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    ui_label_create(axis, "Light", UI_FONT_XS, UI_COLOR_TEXT_DIM);

    /*Plain lv_chart: it owns the data, the scaling, the grid and the drawing.
     *Nothing is hooked onto it -- the ruler below is a separate object.*/
    rain_chart = lv_chart_create(chart_row);
    lv_obj_set_height(rain_chart, LV_PCT(100));
    lv_obj_set_flex_grow(rain_chart, 1);
    lv_chart_set_type(rain_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(rain_chart, PAGE_CLOCK_RAIN_SEGMENTS);
    lv_chart_set_axis_range(rain_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    /*A vertical line every quarter hour, sitting over each ruler tick.*/
    lv_chart_set_div_line_count(rain_chart, 0, RAIN_TICK_COUNT);

    lv_obj_set_style_bg_opa(rain_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(rain_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rain_chart, 0, LV_PART_MAIN);
    /*Headroom so a full-scale reading is not sliced off by the top edge.*/
    lv_obj_set_style_pad_top(rain_chart, RAIN_CHART_HEADROOM, LV_PART_MAIN);
    lv_obj_set_scrollable(rain_chart, false);
    lv_obj_set_clickable(rain_chart, false);

    /*Grid lines: present, but never competing with the data.*/
    lv_obj_set_style_line_color(rain_chart, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_line_width(rain_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(rain_chart, LV_OPA_40, LV_PART_MAIN);

    lv_obj_set_style_line_width(rain_chart, RAIN_LINE_WIDTH, LV_PART_ITEMS);
    lv_obj_set_style_size(rain_chart, 0, 0, LV_PART_INDICATOR);

    rain_series = lv_chart_add_series(rain_chart, UI_COLOR_RAIN, LV_CHART_AXIS_PRIMARY_Y);

    /*The X axis. lv_chart draws no ticks or labels of its own; the convention
     *is to stack a horizontal lv_scale under it with the same tick count and
     *the same width, so the two line up. Indented past the gutter to match.
     *
     *One label per major tick, so the half hours get blanks: lv_scale offers
     *exactly two tick lengths, and a taller half hour was worth more than a
     *taller hour, which the labels already single out.*/
    static const char * scale_labels[] = {"NOW", "", "+1h", "", "+2h", NULL};

    /*Same shape as the chart row above -- a gutter-width spacer, then the
     *scale growing into exactly the space the chart occupies. Structural
     *alignment beats trying to match the chart with padding.*/
    lv_obj_t * scale_row = flow_create(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(scale_row, LV_PCT(100), RAIN_SCALE_HEIGHT);
    lv_obj_set_style_pad_column(scale_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(scale_row, RAIN_LABEL_MARGIN, LV_PART_MAIN);

    lv_obj_t * spacer = flow_create(scale_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(spacer, RAIN_AXIS_WIDTH, LV_PCT(100));

    lv_obj_t * scale = lv_scale_create(scale_row);
    lv_obj_set_height(scale, LV_PCT(100));
    lv_obj_set_flex_grow(scale, 1);
    /*So a tap on it reaches the section, which opens the weather page.*/
    lv_obj_set_clickable(scale, false);
    lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_total_tick_count(scale, RAIN_TICK_COUNT);
    lv_scale_set_major_tick_every(scale, RAIN_TICK_MAJOR_EVERY);
    lv_scale_set_text_src(scale, scale_labels);

    lv_obj_set_style_pad_all(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, LV_PART_MAIN);
    /*The chart already draws a line along the axis, so the scale needs none.*/
    lv_obj_set_style_line_width(scale, 0, LV_PART_MAIN);

    lv_obj_set_style_length(scale, RAIN_TICK_MAJOR, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale, UI_COLOR_BORDER, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 1, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(scale, UI_FONT_XS, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(scale, UI_COLOR_TEXT_DIM, LV_PART_INDICATOR);

    lv_obj_set_style_length(scale, RAIN_TICK_MINOR, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, UI_COLOR_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 1, LV_PART_ITEMS);

    weather_status = ui_status_create(section, UI_COLOR_BG, NULL, NULL);
}

static void calendar_section_create(lv_obj_t * parent)
{
    lv_obj_t * body;
    lv_obj_t * section = header_section_create(parent, "CALENDAR EVENTS", &body);
    lv_obj_set_grid_cell(section, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

    /*Fills the body, so the entries spread down the whole cell and the divider
     *between the two calendars runs its full height.*/
    lv_obj_t * columns = flow_create(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(columns, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_column(columns, UI_GAP, LV_PART_MAIN);

    for(uint32_t c = 0; c < PAGE_CLOCK_COLUMN_COUNT; c++) {
        /*Hairline between today and the fortnight ahead.*/
        if(c > 0) {
            lv_obj_t * rule = divider_create(columns, true);
            lv_obj_set_height(rule, LV_PCT(100));
        }

        /*Entries stack from the top at their natural height; once there are
         *more than fit, the column scrolls rather than squeezing them.*/
        lv_obj_t * col = flow_create(columns, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_height(col, LV_PCT(100));
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_style_pad_row(col, 2, LV_PART_MAIN);
        lv_obj_set_scrollable(col, true);
        lv_obj_set_scroll_dir(col, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_AUTO);

        /*No column headings. The left column is today, the right is the
         *fortnight ahead; the timestamps say which is which.*/
        for(uint32_t r = 0; r < PAGE_CLOCK_EVENTS_PER_COLUMN; r++) {

            lv_obj_t * row = flow_create(col, LV_FLEX_FLOW_ROW);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_style_pad_ver(row, 5, LV_PART_MAIN);
            lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            /*The one thing identifying the calendar, so give it enough width
             *to read as a colour rather than a hairline.*/
            lv_obj_t * accent = lv_obj_create(row);
            lv_obj_set_size(accent, 4, 24);
            lv_obj_set_style_bg_color(accent, UI_COLOR_CAL_PERSONAL, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(accent, 2, LV_PART_MAIN);
            lv_obj_set_clickable(accent, false);

            lv_obj_t * when_lbl  = ui_label_create(row, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
            lv_obj_t * title_lbl = ui_label_create(row, "", UI_FONT_SM, UI_COLOR_TEXT);
            lv_obj_set_flex_grow(title_lbl, 1);
            ui_label_single_line(title_lbl, UI_FONT_SM);

            event_slot[c][r].root   = row;
            event_slot[c][r].accent = accent;
            event_slot[c][r].when   = when_lbl;
            event_slot[c][r].title  = title_lbl;
        }
    }
}

static void air_section_create(lv_obj_t * parent)
{
    lv_obj_t * body;
    lv_obj_t * section = header_section_create(parent, "AIR QUALITY", &body);
    lv_obj_set_grid_cell(section, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_style_pad_row(body, UI_GAP * 2, LV_PART_MAIN);

    /*The whole section is a shortcut into the air quality page.*/
    lv_obj_set_clickable(section, true);
    lv_obj_add_event_cb(section, air_section_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * head = flow_create(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, UI_GAP, LV_PART_MAIN);

    air_arc = lv_arc_create(head);
    lv_obj_set_size(air_arc, 84, 84);
    lv_arc_set_rotation(air_arc, 135);
    lv_arc_set_bg_angles(air_arc, 0, 270);
    lv_arc_set_range(air_arc, 0, 300);
    lv_obj_remove_style(air_arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(air_arc, false);
    lv_obj_set_style_arc_width(air_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(air_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(air_arc, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_border_width(air_arc, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(air_arc, LV_OPA_TRANSP, LV_PART_MAIN);

    air_value = ui_label_create(air_arc, "--", UI_FONT_LG, UI_COLOR_TEXT_DIM);
    lv_obj_center(air_value);

    lv_obj_t * summary = flow_create(head, LV_FLEX_FLOW_COLUMN);
    air_band = ui_label_create(summary, "", UI_FONT_SM, UI_COLOR_TEXT);
    air_pm25 = ui_label_create(summary, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    air_co2  = ui_label_create(summary, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);

    /*Indoor climate, from the on-board sensor rather than the forecast. Pushed
     *out to the edges rather than bunched in the middle.*/
    lv_obj_t * indoor = flow_create(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(indoor, LV_PCT(100));
    lv_obj_set_style_pad_hor(indoor, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_align(indoor, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    air_temp     = sensor_stat_create(indoor, NULL, "--");
    air_humidity = sensor_stat_create(indoor, UI_SYMBOL_HUMIDITY, "--");

    /*Under the caption, which stays.*/
    air_status = ui_status_create(body, UI_COLOR_BG, NULL, NULL);
}

/**
 * Measure where the clock block rests in each state.
 *
 * Ambient centres it on the physical display rather than on the page, so the
 * navigation rail -- which stays in the layout and is only faded out -- does
 * not push the idle clock off-centre.
 */
static void geometry_refresh(void)
{
    if(!root || !clock_block) return;

    lv_obj_update_layout(root);

    int32_t block_w = lv_obj_get_width(clock_block);
    int32_t block_h = lv_obj_get_height(clock_block);

    /*The anchor is nested a couple of levels down now, so go through absolute
     *coordinates rather than lv_obj_get_x(), which is parent-relative.*/
    lv_area_t root_area;
    lv_area_t anchor_area;
    lv_obj_get_coords(root, &root_area);
    lv_obj_get_coords(clock_anchor, &anchor_area);

    active_pos.x = (anchor_area.x1 - root_area.x1) +
                   (lv_area_get_width(&anchor_area) - block_w) / 2;
    active_pos.y = (anchor_area.y1 - root_area.y1) +
                   (lv_area_get_height(&anchor_area) - block_h) / 2;

    lv_display_t * disp = lv_obj_get_display(root);
    ambient_pos.x = (lv_display_get_horizontal_resolution(disp) - block_w) / 2 - root_area.x1;
    ambient_pos.y = (lv_display_get_vertical_resolution(disp) - block_h) / 2 - root_area.y1;

    /*Keep the resting position current, but never fight a running transition.*/
    if(lv_anim_get(clock_block, NULL) == NULL) {
        const lv_point_t * p = ambient ? &ambient_pos : &active_pos;
        lv_obj_set_pos(clock_block, p->x, p->y);
    }
}

static void detail_opa_set(void * obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, LV_PART_MAIN);
}

static void detail_faded_out(lv_anim_t * a)
{
    LV_UNUSED(a);
    /*Only hide once invisible, so the fade is not cut short. Hiding matters:
     *a fully transparent object still takes touches.*/
    if(ambient) lv_obj_set_hidden(detail, true);
}

static void mode_apply(bool value, bool animate)
{
    ambient = value;

    const lv_point_t * target = ambient ? &ambient_pos : &active_pos;
    lv_opa_t           opa    = ambient ? LV_OPA_TRANSP : LV_OPA_COVER;

    ui_set_chrome_hidden(ambient);

    /*Unhide before fading in; hiding on the way out waits for the animation.*/
    if(!ambient) lv_obj_set_hidden(detail, false);

    lv_anim_delete(clock_block, NULL);
    lv_anim_delete(detail, NULL);

    if(!animate) {
        lv_obj_set_pos(clock_block, target->x, target->y);
        lv_obj_set_style_opa(detail, opa, LV_PART_MAIN);
        lv_obj_set_hidden(detail, ambient);
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_duration(&a, TRANSITION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

    lv_anim_set_var(&a, clock_block);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_values(&a, lv_obj_get_x(clock_block), target->x);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, lv_obj_get_y(clock_block), target->y);
    lv_anim_start(&a);

    lv_anim_set_var(&a, detail);
    lv_anim_set_exec_cb(&a, detail_opa_set);
    lv_anim_set_values(&a, lv_obj_get_style_opa(detail, LV_PART_MAIN), opa);
    lv_anim_set_completed_cb(&a, detail_faded_out);
    lv_anim_start(&a);
}

static void wake_event(lv_event_t * e)
{
    LV_UNUSED(e);
    page_clock_set_ambient(false);
}

static void alarm_chip_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    /*While ambient the chip is just part of the idle face, and making it
     *clickable would otherwise swallow the touch that should wake the page.
     *So it only navigates once the page is awake.*/
    if(ambient) page_clock_set_ambient(false);
    else        ui_navigate(UI_PAGE_ALARMS);
}

static void weather_section_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(!ambient) ui_navigate(UI_PAGE_WEATHER);
}

static void air_section_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(!ambient) ui_navigate(UI_PAGE_AIR_QUALITY);
}
