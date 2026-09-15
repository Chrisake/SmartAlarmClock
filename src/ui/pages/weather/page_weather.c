/**
 * @file page_weather.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/weather/page_weather.h"
#include "ui/ui_format.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

/** Height of the current-conditions card. */
#define NOW_HEIGHT 140

/** Height of the 24-hour card. The week takes whatever is left. */
#define HOURLY_HEIGHT 148

#define NOW_ICON_SIZE  96
#define HOUR_ICON_SIZE 48
#define DAY_ICON_SIZE  40

/** Width of the column holding the condition, high/low and location. */
#define NOW_SUMMARY_WIDTH 220

/** Thickness of a day's temperature range bar. */
#define RANGE_BAR_WIDTH 8

/** Chances of rain below this are shown dimmed, so the wet slots stand out. */
#define RAIN_NOTABLE_PERCENT 30

/**********************
 *      TYPEDEFS
 **********************/

/** The readouts on the right of the current-conditions card, in grid order. */
typedef enum {
    STAT_FEELS_LIKE,
    STAT_HUMIDITY,
    STAT_RAIN,
    STAT_WIND,
    STAT_SUNRISE,
    STAT_SUNSET,
    STAT_COUNT,
} now_stat_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);

static void now_card_create(lv_obj_t * parent);
static void hourly_card_create(lv_obj_t * parent);
static void daily_card_create(lv_obj_t * parent);

static lv_obj_t * box_create(lv_obj_t * parent);
static void       temp_label_set(lv_obj_t * label, int32_t temp);
static void       rain_label_set(lv_obj_t * label, int32_t chance);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "Weather",
    .icon    = UI_SYMBOL_WEATHER,
    .create  = create,
    .on_show = NULL,
    .on_hide = NULL,
};

/*Placeholder forecast, so the layout reads correctly before the weather
 *service reports. Varied on purpose: every icon but snow, dry and wet slots,
 *and a week whose highs and lows move enough to show off the range bars.*/
static const page_weather_now_t now_defaults = {
    .condition   = UI_WEATHER_PARTLY_CLOUDY,
    .summary     = "Partly cloudy",
    .temp        = 24,
    .feels_like  = 25,
    .high        = 27,
    .low         = 18,
    .humidity    = 58,
    .rain_chance = 20,
    .wind        = "14 km/h N",
    .sunrise     = NULL,   /*Times are formatted when the page is built, in create()*/
    .sunset      = NULL,
};

/*The sample strip, every four hours from 2 PM, as hours of the day so they
 *follow the clock setting.*/
static const struct {
    uint8_t      hour;
    ui_weather_t condition;
    int32_t      temp;
    int32_t      rain_chance;
} hour_defaults[PAGE_WEATHER_HOURS] = {
    {14, UI_WEATHER_PARTLY_CLOUDY,       26, 10},
    {18, UI_WEATHER_SHOWERS,             23, 60},
    {22, UI_WEATHER_PARTLY_CLOUDY_NIGHT, 20, 30},
    {2,  UI_WEATHER_CLEAR_NIGHT,         18, 0},
    {6,  UI_WEATHER_FOG,                 17, 10},
    {10, UI_WEATHER_CLEAR,               22, 0},
};

static const page_weather_day_t day_defaults[PAGE_WEATHER_DAYS] = {
    {"Today", UI_WEATHER_PARTLY_CLOUDY, 18, 27, 20},
    {"Tue",   UI_WEATHER_RAIN,          17, 22, 80},
    {"Wed",   UI_WEATHER_THUNDERSTORM,  16, 21, 90},
    {"Thu",   UI_WEATHER_CLOUDY,        16, 23, 30},
    {"Fri",   UI_WEATHER_CLEAR,         17, 26, 0},
    {"Sat",   UI_WEATHER_CLEAR,         19, 29, 0},
    {"Sun",   UI_WEATHER_SHOWERS,       18, 25, 40},
};

static const char * const stat_captions[STAT_COUNT] = {
    [STAT_FEELS_LIKE] = "FEELS LIKE",
    [STAT_HUMIDITY]   = "HUMIDITY",
    [STAT_RAIN]       = "RAIN",
    [STAT_WIND]       = "WIND",
    [STAT_SUNRISE]    = "SUNRISE",
    [STAT_SUNSET]     = "SUNSET",
};

static lv_obj_t * now_icon;
static lv_obj_t * now_temp;
static lv_obj_t * now_summary;
static lv_obj_t * now_range;
static lv_obj_t * now_location;
static lv_obj_t * now_stats[STAT_COUNT];

static struct {
    lv_obj_t * root;
    lv_obj_t * when;
    lv_obj_t * icon;
    lv_obj_t * temp;
    lv_obj_t * rain;
} hour_slots[PAGE_WEATHER_HOURS];

static struct {
    lv_obj_t * root;
    lv_obj_t * day;
    lv_obj_t * icon;
    lv_obj_t * rain;
    lv_obj_t * high;
    lv_obj_t * bar;
    lv_obj_t * low;
} day_slots[PAGE_WEATHER_DAYS];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const ui_page_t * page_weather_desc(void)
{
    return &desc;
}

void page_weather_set_location(const char * location, const char * updated)
{
    if(!location) location = "";

    if(updated && updated[0]) {
        lv_label_set_text_fmt(now_location, "%s  " UI_BULLET "  %s", location, updated);
    }
    else {
        lv_label_set_text(now_location, location);
    }
}

void page_weather_set_now(const page_weather_now_t * now)
{
    if(!now) return;

    ui_weather_icon_set(now_icon, now->condition);
    temp_label_set(now_temp, now->temp);
    lv_label_set_text(now_summary, now->summary ? now->summary : "");
    lv_label_set_text_fmt(now_range, "H %d" UI_DEG "   L %d" UI_DEG, (int)now->high, (int)now->low);

    temp_label_set(now_stats[STAT_FEELS_LIKE], now->feels_like);
    lv_label_set_text_fmt(now_stats[STAT_HUMIDITY], "%d %%", (int)now->humidity);
    lv_label_set_text_fmt(now_stats[STAT_RAIN], "%d %%", (int)now->rain_chance);
    lv_label_set_text(now_stats[STAT_WIND], now->wind ? now->wind : "--");
    lv_label_set_text(now_stats[STAT_SUNRISE], now->sunrise ? now->sunrise : "--");
    lv_label_set_text(now_stats[STAT_SUNSET], now->sunset ? now->sunset : "--");
}

void page_weather_set_hourly(const page_weather_hour_t hours[], uint32_t count)
{
    if(count > PAGE_WEATHER_HOURS) count = PAGE_WEATHER_HOURS;

    for(uint32_t i = 0; i < PAGE_WEATHER_HOURS; i++) {
        lv_obj_set_hidden(hour_slots[i].root, i >= count);
        if(i >= count) continue;

        lv_label_set_text(hour_slots[i].when, hours[i].when ? hours[i].when : "");
        ui_weather_icon_set(hour_slots[i].icon, hours[i].condition);
        temp_label_set(hour_slots[i].temp, hours[i].temp);
        rain_label_set(hour_slots[i].rain, hours[i].rain_chance);
    }
}

void page_weather_set_daily(const page_weather_day_t days[], uint32_t count)
{
    if(count > PAGE_WEATHER_DAYS) count = PAGE_WEATHER_DAYS;

    /*One scale for the whole week, so the bars can be compared.*/
    int32_t coldest = count > 0 ? days[0].low : 0;
    int32_t warmest = count > 0 ? days[0].high : 1;

    for(uint32_t i = 1; i < count; i++) {
        coldest = LV_MIN(coldest, days[i].low);
        warmest = LV_MAX(warmest, days[i].high);
    }
    if(warmest <= coldest) warmest = coldest + 1;

    for(uint32_t i = 0; i < PAGE_WEATHER_DAYS; i++) {
        lv_obj_set_hidden(day_slots[i].root, i >= count);
        if(i >= count) continue;

        lv_label_set_text(day_slots[i].day, days[i].day ? days[i].day : "");
        ui_weather_icon_set(day_slots[i].icon, days[i].condition);
        rain_label_set(day_slots[i].rain, days[i].rain_chance);
        temp_label_set(day_slots[i].high, days[i].high);
        temp_label_set(day_slots[i].low, days[i].low);

        /*Range mode clamps each end against the other, so open the bar all the
         *way first; otherwise a day warmer than the last one shown there could
         *not move its low above the old high.*/
        lv_obj_t * bar = day_slots[i].bar;
        lv_bar_set_range(bar, coldest, warmest);
        lv_bar_set_start_value(bar, coldest, LV_ANIM_OFF);
        lv_bar_set_value(bar, days[i].high, LV_ANIM_OFF);
        lv_bar_set_start_value(bar, days[i].low, LV_ANIM_OFF);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create(lv_obj_t * parent)
{
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {NOW_HEIGHT, HOURLY_HEIGHT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(root, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_grid_dsc_array(root, cols, rows);

    now_card_create(root);
    hourly_card_create(root);
    daily_card_create(root);

    /* TODO: the weather service replaces these with the real forecast, and
     * calls them again whenever it refreshes. */
    /*Times go through ui_format, as the service's must, so the samples follow
     *the clock setting. The page copies every string.*/
    page_weather_now_t  now = now_defaults;
    page_weather_hour_t strip[PAGE_WEATHER_HOURS];
    char                sunrise[12], sunset[12], stamp[12], updated[24];
    char                hours[PAGE_WEATHER_HOURS][12];

    ui_format_time(sunrise, sizeof(sunrise), 7, 4);
    ui_format_time(sunset, sizeof(sunset), 19, 31);
    now.sunrise = sunrise;
    now.sunset  = sunset;

    ui_format_time(stamp, sizeof(stamp), 10, 15);
    lv_snprintf(updated, sizeof(updated), "Updated %s", stamp);

    for(uint32_t i = 0; i < PAGE_WEATHER_HOURS; i++) {
        ui_format_hour(hours[i], sizeof(hours[i]), hour_defaults[i].hour);
        strip[i].when        = hours[i];
        strip[i].condition   = hour_defaults[i].condition;
        strip[i].temp        = hour_defaults[i].temp;
        strip[i].rain_chance = hour_defaults[i].rain_chance;
    }

    page_weather_set_location("Athens", updated);
    page_weather_set_now(&now);
    page_weather_set_hourly(strip, PAGE_WEATHER_HOURS);
    page_weather_set_daily(day_defaults, PAGE_WEATHER_DAYS);

    return root;
}

static void now_card_create(lv_obj_t * parent)
{
    static const int32_t stat_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t stat_rows[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, UI_GAP * 2, LV_PART_MAIN);

    /*Headline: the icon and temperature, large enough to read across a room.*/
    now_icon = ui_weather_icon_create(card, NOW_ICON_SIZE, UI_WEATHER_CLEAR);
    now_temp = ui_label_create(card, "--", UI_FONT_CLOCK, UI_COLOR_TEXT);

    lv_obj_t * summary = box_create(card);
    lv_obj_set_width(summary, NOW_SUMMARY_WIDTH);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(summary, 4, LV_PART_MAIN);

    now_summary = ui_label_create(summary, "", UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_set_width(now_summary, LV_PCT(100));
    ui_label_single_line(now_summary, UI_FONT_MD);

    now_range = ui_label_create(summary, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);

    now_location = ui_label_create(summary, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(now_location, LV_PCT(100));
    lv_obj_set_style_margin_top(now_location, UI_GAP / 2, LV_PART_MAIN);
    ui_label_single_line(now_location, UI_FONT_XS);

    /*Hairline between the headline and the details.*/
    lv_obj_t * rule = lv_obj_create(card);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 1, LV_PCT(80));
    lv_obj_set_style_bg_color(rule, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_clickable(rule, false);

    lv_obj_t * stats = box_create(card);
    lv_obj_set_flex_grow(stats, 1);
    lv_obj_set_style_pad_row(stats, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(stats, UI_GAP, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(stats, stat_cols, stat_rows);

    for(uint32_t i = 0; i < STAT_COUNT; i++) {
        lv_obj_t * stat = box_create(stats);
        lv_obj_set_grid_cell(stat, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_START, i / 3, 1);
        lv_obj_set_flex_flow(stat, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(stat, 2, LV_PART_MAIN);

        lv_obj_t * caption = ui_label_create(stat, stat_captions[i], UI_FONT_XS, UI_COLOR_TEXT_DIM);
        lv_obj_set_style_text_letter_space(caption, 1, LV_PART_MAIN);

        now_stats[i] = ui_label_create(stat, "--", UI_FONT_MD, UI_COLOR_TEXT);
        lv_obj_set_width(now_stats[i], LV_PCT(100));
        ui_label_single_line(now_stats[i], UI_FONT_MD);
    }
}

static void hourly_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    lv_obj_t * strip = box_create(card);
    lv_obj_set_width(strip, LV_PCT(100));
    lv_obj_set_flex_grow(strip, 1);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for(uint32_t i = 0; i < PAGE_WEATHER_HOURS; i++) {
        /*Equal shares of the width, so the slots stay evenly spaced whatever
         *their labels say.*/
        lv_obj_t * col = box_create(strip);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 4, LV_PART_MAIN);

        hour_slots[i].root = col;
        hour_slots[i].when = ui_label_create(col, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
        hour_slots[i].icon = ui_weather_icon_create(col, HOUR_ICON_SIZE, UI_WEATHER_CLEAR);
        hour_slots[i].temp = ui_label_create(col, "--", UI_FONT_MD, UI_COLOR_TEXT);
        hour_slots[i].rain = ui_label_create(col, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    }
}

static void daily_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

    lv_obj_t * strip = box_create(card);
    lv_obj_set_width(strip, LV_PCT(100));
    lv_obj_set_flex_grow(strip, 1);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(strip, UI_GAP / 2, LV_PART_MAIN);

    for(uint32_t i = 0; i < PAGE_WEATHER_DAYS; i++) {
        lv_obj_t * col = box_create(strip);
        lv_obj_set_height(col, LV_PCT(100));
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(col, UI_GAP / 2, LV_PART_MAIN);
        lv_obj_set_style_pad_row(col, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(col, UI_RADIUS - 4, LV_PART_MAIN);

        /*Today sits on a tile. Set before the icon is built, which cuts its
         *moon to match the surface.*/
        if(i == 0) {
            lv_obj_set_style_bg_color(col, UI_COLOR_CARD_ALT, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(col, LV_OPA_COVER, LV_PART_MAIN);
        }

        day_slots[i].root = col;
        day_slots[i].day  = ui_label_create(col, "", UI_FONT_SM, UI_COLOR_TEXT);

        /*Chance of rain beside the icon rather than under it: the week is
         *short of height and the range bar wants all it can get.*/
        lv_obj_t * head = box_create(col);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(head, 4, LV_PART_MAIN);

        day_slots[i].icon = ui_weather_icon_create(head, DAY_ICON_SIZE, UI_WEATHER_CLEAR);
        day_slots[i].rain = ui_label_create(head, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);

        day_slots[i].high = ui_label_create(col, "--", UI_FONT_MD, UI_COLOR_TEXT);

        /*Low to high on the week's shared scale. lv_bar paints an indicator
         *gradient across the whole track and clips it to the indicator, so a
         *colour always stands for the same temperature, whichever day it is.*/
        lv_obj_t * bar = lv_bar_create(col);
        lv_obj_set_width(bar, RANGE_BAR_WIDTH);
        lv_obj_set_flex_grow(bar, 1);
        lv_obj_set_clickable(bar, false);
        lv_bar_set_orientation(bar, LV_BAR_ORIENTATION_VERTICAL);
        lv_bar_set_mode(bar, LV_BAR_MODE_RANGE);
        lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, UI_COLOR_BORDER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, UI_COLOR_TEMP_WARM, LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(bar, UI_COLOR_TEMP_COOL, LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
        day_slots[i].bar = bar;

        day_slots[i].low = ui_label_create(col, "--", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    }
}

/**
 * An invisible, content-sized layout box. Not clickable, so a press on it
 * falls through to whatever it sits in.
 */
static lv_obj_t * box_create(lv_obj_t * parent)
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

static void temp_label_set(lv_obj_t * label, int32_t temp)
{
    lv_label_set_text_fmt(label, "%d" UI_DEG, (int)temp);
}

static void rain_label_set(lv_obj_t * label, int32_t chance)
{
    lv_label_set_text_fmt(label, LV_SYMBOL_TINT " %d%%", (int)chance);
    lv_obj_set_style_text_color(label, chance >= RAIN_NOTABLE_PERCENT ? UI_COLOR_RAIN : UI_COLOR_TEXT_DIM,
                                LV_PART_MAIN);
}
