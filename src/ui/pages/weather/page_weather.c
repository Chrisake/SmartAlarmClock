/**
 * @file page_weather.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/weather/page_weather.h"
#include "ui/ui_moon_icon.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

/** Height of the current-conditions card: the freshness line over the
 *  headline on the left, two rows of readouts on the right. */
#define NOW_HEIGHT 140

/** Side of the moon drawn beside how much of it is lit. */
#define MOON_ICON_SIZE 18

/** Height of the hourly card. The week takes whatever is left. */
#define HOURLY_HEIGHT 148

#define NOW_ICON_SIZE  72
#define HOUR_ICON_SIZE 48
#define DAY_ICON_SIZE  40

/** Width of the column holding the condition, high/low and city. A long
 *  condition wraps onto a second line at its spaces, so the readouts keep their
 *  room; the longest word, "Thunderstorm", still fits. */
#define NOW_SUMMARY_WIDTH 148

/** Widest a city's name is drawn on its button before it ends in dots. */
#define PLACE_NAME_MAX_WIDTH 110

/** Width of the list of cities. */
#define PLACE_POPUP_WIDTH 260

/** Gaps between the current-conditions card's parts, and between its readouts'
 *  equal columns: tight enough that "100% Lit" with its arrow, "12 km/h NW" and
 *  "Wed 30 Sep" each fit a column. */
#define NOW_GAP      12
#define NOW_STAT_GAP 10

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

/** The two buttons naming the city: in the card, and over the page's cover. */
typedef enum {
    PLACE_BUTTON_CARD,
    PLACE_BUTTON_COVER,
    PLACE_BUTTON_COUNT,
} place_button_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);

static void now_card_create(lv_obj_t * parent);
static void hourly_card_create(lv_obj_t * parent);
static void daily_card_create(lv_obj_t * parent);

static lv_obj_t * box_create(lv_obj_t * parent);
static void       stat_create(lv_obj_t * grid, uint32_t col, uint32_t row, const char * caption, lv_obj_t ** value);
static void       temp_label_set(lv_obj_t * label, int32_t temp);
static void       rain_label_set(lv_obj_t * label, int32_t chance);
static void       refresh_clicked(lv_event_t * e);

static lv_obj_t * place_button_create(lv_obj_t * parent);
static void       places_show(void);
static void       place_clicked(lv_event_t * e);
static void       place_popup_close(void);
static void       place_popup_dismissed(lv_event_t * e);
static void       place_option_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "Weather",
    .icon    = UI_SYMBOL_WEATHER,
    .create  = create,
    .on_show = NULL,
    /*The list of cities lives on the top layer, above every page.*/
    .on_hide = place_popup_close,
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
static lv_obj_t * now_updated;
static lv_obj_t * now_stats[STAT_COUNT];
static lv_obj_t * moon_icon;
static lv_obj_t * moon_lit;
static lv_obj_t * moon_trend;
static lv_obj_t * moon_new;
static lv_obj_t * refresh;
static lv_obj_t * status;

static page_weather_refresh_cb_t refresh_cb;
static page_weather_place_cb_t   place_cb;

/** Survive ui_rebuild(): the feed says them once. */
static char              places[PAGE_WEATHER_PLACES_MAX][PAGE_WEATHER_PLACE_LEN];
static uint32_t          place_count;
static uint32_t          place_selected;
static ui_status_state_t state = UI_STATUS_LOADING;

static lv_obj_t * place_buttons[PLACE_BUTTON_COUNT];
static lv_obj_t * place_popup;   /**< Backdrop on the top layer; NULL when closed */

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

void page_weather_set_updated(const char * updated, bool stale)
{
    lv_label_set_text(now_updated, updated ? updated : "");
    lv_obj_set_style_text_color(now_updated, stale ? UI_COLOR_WARN : UI_COLOR_TEXT_DIM, LV_PART_MAIN);
}

void page_weather_set_places(const char * const names[], uint32_t count, uint32_t selected)
{
    if(count > PAGE_WEATHER_PLACES_MAX) count = PAGE_WEATHER_PLACES_MAX;

    for(uint32_t i = 0; i < count; i++) lv_strlcpy(places[i], names[i] ? names[i] : "", sizeof(places[i]));
    place_count    = count;
    place_selected = selected < count ? selected : 0;

    /*A list open on the old cities would pick the wrong one.*/
    place_popup_close();
    places_show();
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

void page_weather_set_moon(const page_weather_moon_t * moon)
{
    if(!moon) return;

    bool known = moon->illumination >= 0;

    ui_moon_icon_set(moon_icon, moon->age, moon->southern);
    lv_obj_set_hidden(moon_icon, !known);
    lv_obj_set_hidden(moon_trend, !known);

    if(known) lv_label_set_text_fmt(moon_lit, "%d%% ", (int)moon->illumination);
    else      lv_label_set_text(moon_lit, "--");

    lv_label_set_text(moon_trend, moon->waxing ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(moon_trend, moon->waxing ? UI_COLOR_GOOD : UI_COLOR_BAD, LV_PART_MAIN);

    lv_label_set_text(moon_new, moon->new_moon ? moon->new_moon : "--");
}

void page_weather_set_state(ui_status_state_t value, const char * message)
{
    state = value;
    ui_status_set(status, value, message);
    places_show();
}

void page_weather_set_refreshing(bool refreshing)
{
    ui_refresh_set_busy(refresh, refreshing);
}

void page_weather_set_refresh_cb(page_weather_refresh_cb_t cb)
{
    refresh_cb = cb;
}

void page_weather_set_place_cb(page_weather_place_cb_t cb)
{
    place_cb = cb;
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

    /*The whole page, until the weather feed has a forecast for it.*/
    status = ui_status_create(root, UI_COLOR_BG, refresh_clicked, NULL);

    /*Over the cover, where the card's own button would be: a city that will
     *not load must not keep the page from being switched to another.*/
    place_buttons[PLACE_BUTTON_COVER] = place_button_create(root);
    lv_obj_set_floating(place_buttons[PLACE_BUTTON_COVER], true);
    lv_obj_align(place_buttons[PLACE_BUTTON_COVER], LV_ALIGN_TOP_LEFT, UI_PAD, UI_PAD);

    ui_status_set(status, state, NULL);
    places_show();

    return root;
}

static void now_card_create(lv_obj_t * parent)
{
    /*Four readouts to a row, in columns of equal width.*/
    static const int32_t stat_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                        LV_GRID_TEMPLATE_LAST};
    static const int32_t stat_rows[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, NOW_GAP, LV_PART_MAIN);

    /*How fresh the forecast is, and a way to fetch it again, in the top left
     *corner -- over the headline, which leaves room there -- so the readouts
     *on the right can start at the top.*/
    lv_obj_t * left = box_create(card);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 2, LV_PART_MAIN);

    lv_obj_t * corner = box_create(left);
    lv_obj_set_flex_flow(corner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(corner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(corner, 8, LV_PART_MAIN);

    refresh     = ui_refresh_create(corner, refresh_clicked, NULL);
    now_updated = ui_label_create(corner, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);

    /*Headline: the icon and temperature, large enough to read across a room.*/
    lv_obj_t * headline = box_create(left);
    lv_obj_set_flex_flow(headline, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(headline, NOW_GAP, LV_PART_MAIN);

    now_icon = ui_weather_icon_create(headline, NOW_ICON_SIZE, UI_WEATHER_CLEAR);
    now_temp = ui_label_create(headline, "--", UI_FONT_CLOCK, UI_COLOR_TEXT);

    lv_obj_t * summary = box_create(headline);
    lv_obj_set_width(summary, NOW_SUMMARY_WIDTH);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(summary, 4, LV_PART_MAIN);

    now_summary = ui_label_create(summary, "", UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_set_width(now_summary, LV_PCT(100));
    lv_label_set_long_mode(now_summary, LV_LABEL_LONG_MODE_WRAP);

    now_range = ui_label_create(summary, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);

    place_buttons[PLACE_BUTTON_CARD] = place_button_create(summary);

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
    lv_obj_set_style_pad_column(stats, NOW_STAT_GAP, LV_PART_MAIN);
    /*A long reading in the last column may run on into the card's padding
     *rather than be cut off.*/
    lv_obj_set_overflow_visible(stats, true);
    lv_obj_set_grid_dsc_array(stats, stat_cols, stat_rows);

    /*Feels like, humidity, rain and wind across the top; sunrise and sunset
     *under the first two.*/
    for(uint32_t i = 0; i < STAT_COUNT; i++) {
        stat_create(stats, i < 4 ? i : i - 4, i < 4 ? 0 : 1, stat_captions[i], &now_stats[i]);
    }

    /*The moon under the rain: drawn as it looks, how much of it is lit, and an
     *arrow for whether that grows or shrinks in the days ahead. Then the next
     *new moon under the wind.*/
    lv_obj_t * moon = box_create(stats);
    lv_obj_set_grid_cell(moon, LV_GRID_ALIGN_START, 2, 1, LV_GRID_ALIGN_START, 1, 1);
    lv_obj_set_flex_flow(moon, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(moon, 2, LV_PART_MAIN);

    lv_obj_t * caption = ui_label_create(moon, "MOON PHASE", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(caption, 1, LV_PART_MAIN);

    lv_obj_t * reading = box_create(moon);
    lv_obj_set_flex_flow(reading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(reading, 4, LV_PART_MAIN);

    moon_icon  = ui_moon_icon_create(reading, MOON_ICON_SIZE);
    moon_lit   = ui_label_create(reading, "--", UI_FONT_MD, UI_COLOR_TEXT);
    moon_trend = ui_label_create(reading, LV_SYMBOL_UP, UI_FONT_MD, UI_COLOR_GOOD);
    lv_obj_set_hidden(moon_icon, true);
    lv_obj_set_hidden(moon_trend, true);

    stat_create(stats, 3, 1, "NEW MOON", &moon_new);
}

/**
 * A captioned readout in the current-conditions grid, as wide as its contents.
 * @param value   receives the value's label
 */
static void stat_create(lv_obj_t * grid, uint32_t col, uint32_t row, const char * caption, lv_obj_t ** value)
{
    lv_obj_t * stat = box_create(grid);
    lv_obj_set_grid_cell(stat, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_START, row, 1);
    lv_obj_set_flex_flow(stat, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(stat, 2, LV_PART_MAIN);

    lv_obj_t * label = ui_label_create(stat, caption, UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);

    *value = ui_label_create(stat, "--", UI_FONT_MD, UI_COLOR_TEXT);
}

static void hourly_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    /*A slot stands about 122 px -- time, icon, temperature, rain chance -- and
     *the card's usual padding left it 114, clipping the top and bottom. Half
     *that padding above and below gives it room without taking any height
     *from the week.*/
    lv_obj_set_style_pad_ver(card, UI_PAD / 2, LV_PART_MAIN);

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

static void refresh_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(refresh_cb) refresh_cb();
}

/** The city's name, and a chevron for the list when there is one: name first, chevron second. */
static lv_obj_t * place_button_create(lv_obj_t * parent)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_ext_click_area(btn, 8);
    lv_obj_add_event_cb(btn, place_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * name = ui_label_create(btn, "", UI_FONT_XS, UI_COLOR_TEXT);
    lv_obj_set_style_max_width(name, PLACE_NAME_MAX_WIDTH, LV_PART_MAIN);
    ui_label_single_line(name, UI_FONT_XS);

    ui_label_create(btn, LV_SYMBOL_DOWN, UI_FONT_XS, UI_COLOR_TEXT_DIM);
    return btn;
}

/**
 * Name the city on both buttons. With other cities to pick from, each is a
 * button with a chevron; with none, plain dim text. The one over the cover is
 * only up while the cover is, and there is a choice to make.
 */
static void places_show(void)
{
    bool         choice = place_count > 1;
    const char * name   = place_count > 0 ? places[place_selected] : "";

    for(uint32_t i = 0; i < PLACE_BUTTON_COUNT; i++) {
        lv_obj_t * btn = place_buttons[i];
        if(!btn) continue;

        lv_label_set_text(lv_obj_get_child(btn, 0), name);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), choice ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM,
                                    LV_PART_MAIN);
        lv_obj_set_hidden(lv_obj_get_child(btn, 1), !choice);
        lv_obj_set_clickable(btn, choice);
        lv_obj_set_style_bg_opa(btn, choice ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(btn, choice ? 8 : 0, LV_PART_MAIN);
    }

    if(place_buttons[PLACE_BUTTON_COVER]) {
        lv_obj_set_hidden(place_buttons[PLACE_BUTTON_COVER], !choice || state == UI_STATUS_READY);
    }
}

/** Open the list of cities under the button tapped. */
static void place_clicked(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target_obj(e);

    if(place_popup || place_count < 2) return;

    /*Dims the page and catches a tap anywhere outside the panel, which
     *closes the list without changing the city.*/
    place_popup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(place_popup);
    lv_obj_set_size(place_popup, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(place_popup, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(place_popup, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_event_cb(place_popup, place_popup_dismissed, LV_EVENT_CLICKED, NULL);

    lv_obj_t * panel = ui_card_create(place_popup);
    lv_obj_set_size(panel, PLACE_POPUP_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, lv_display_get_vertical_resolution(lv_display_get_default()) - 2 * UI_GAP,
                                LV_PART_MAIN);
    lv_obj_set_scrollable(panel, true);

    for(uint32_t i = 0; i < place_count; i++) {
        lv_obj_t * option = lv_button_create(panel);
        lv_obj_set_size(option, LV_PCT(100), UI_TOUCH_MIN);
        lv_obj_set_style_radius(option, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(option, UI_GAP, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(option, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(option, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(option, UI_COLOR_TEXT, LV_PART_MAIN);
        if(i == place_selected) lv_obj_add_state(option, LV_STATE_CHECKED);

        /*The clock's own location, the one on the clock page, is marked with a house.*/
        lv_obj_t * label = lv_label_create(option);
        if(i == 0) lv_label_set_text_fmt(label, LV_SYMBOL_HOME "  %s", places[i]);
        else       lv_label_set_text(label, places[i]);
        lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
        lv_obj_set_width(label, LV_PCT(100));
        ui_label_single_line(label, UI_FONT_SM);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_add_event_cb(option, place_option_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
    }

    lv_obj_align_to(panel, btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, UI_GAP / 2);
}

static void place_popup_close(void)
{
    if(!place_popup) return;

    /*Usually called from an event on one of the popup's own children, which
     *must not be deleted from under itself.*/
    lv_obj_delete_async(place_popup);
    place_popup = NULL;
}

static void place_popup_dismissed(lv_event_t * e)
{
    LV_UNUSED(e);
    place_popup_close();
}

static void place_option_clicked(lv_event_t * e)
{
    uint32_t index = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);

    place_popup_close();
    if(index == place_selected || index >= place_count) return;

    place_selected = index;
    places_show();
    if(place_cb) place_cb(index);
}
