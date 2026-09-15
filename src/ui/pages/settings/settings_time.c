/**
 * @file settings_time.c
 *
 * Date & time tab, in two cards. Left, where the time comes from: the time
 * server, or a date and time set by hand, and the time zone, detected or
 * chosen. Right, how times and dates are written everywhere in the UI.
 *
 * The date and time are set with a wheel picker whose wheels follow the
 * format settings: day, month and year in the chosen order, and an AM/PM
 * wheel only on the 12-hour clock.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/settings_private.h"
#include "ui/ui_format.h"
#include "ui/ui_picker.h"
#include "settings/clock_time.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** Years the date wheel offers. */
#define YEAR_FIRST 2024
#define YEAR_LAST  2050

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void options_build(void);
static void enables_apply(void);

static void time_auto_changed(lv_event_t * e);
static void server_commit(lv_event_t * e);
static void date_clicked(lv_event_t * e);
static void time_clicked(lv_event_t * e);
static void zone_auto_changed(lv_event_t * e);
static void zone_changed(lv_event_t * e);
static void clock_24h_changed(lv_event_t * e);
static void date_format_clicked(lv_event_t * e);
static void seconds_changed(lv_event_t * e);

static void date_applied(const uint32_t selected[], uint32_t count, void * user);
static void time_applied(const uint32_t selected[], uint32_t count, void * user);

/**********************
 *  STATIC VARIABLES
 **********************/

static const char * const date_formats[SETTINGS_DATE_COUNT] = {"DD/MM", "MM/DD", "YYYY-MM-DD"};

/*Wheel contents, built once.*/
static char day_options[31 * 3 + 1];
static char month_options[12 * 10 + 1];
static char year_options[(YEAR_LAST - YEAR_FIRST + 1) * 5 + 1];
static char hour12_options[12 * 3 + 1];
static char hour24_options[24 * 3 + 1];
static char minute_options[60 * 3 + 1];
static char zone_options[1024];

static lv_obj_t * time_auto_switch;
static lv_obj_t * server_field;
static lv_obj_t * date_button;
static lv_obj_t * time_button;
static lv_obj_t * zone_auto_switch;
static lv_obj_t * zone_dropdown;
static lv_obj_t * clock_24h_switch;
static lv_obj_t * date_format_segmented;
static lv_obj_t * seconds_switch;

/** What the dropdown shows for a detected zone that is not in the list. The
 *  dropdown keeps the pointer. */
static char zone_text[SETTINGS_ZONE_LEN];

/** The device's time, as last reported, for the buttons and the wheels. */
static struct tm now_local;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sp_time_create(lv_obj_t * tab)
{
    options_build();

    lv_obj_t * columns = sp_box_create(tab);
    lv_obj_set_width(columns, LV_PCT(100));
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(columns, UI_GAP, LV_PART_MAIN);

    /*Where the time comes from.*/
    lv_obj_t * source = sp_card_create(columns);
    lv_obj_set_width(source, 0);
    lv_obj_set_flex_grow(source, 1);
    lv_obj_set_style_pad_row(source, UI_GAP / 2, LV_PART_MAIN);

    lv_obj_t * row = sp_row_create(source, "Set automatically");
    time_auto_switch = sp_switch_create(row, time_auto_changed);

    sp_field_create(source, "Time server", "pool.ntp.org", SP_FIELD_TEXT, &server_field);
    lv_textarea_set_max_length(server_field, SETTINGS_HOST_LEN - 1);
    lv_obj_add_event_cb(server_field, server_commit, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(server_field, server_commit, LV_EVENT_DEFOCUSED, NULL);

    row = sp_row_create(source, "Date");
    date_button = sp_button_create(row, "", false);
    lv_obj_add_event_cb(date_button, date_clicked, LV_EVENT_CLICKED, NULL);

    row = sp_row_create(source, "Time");
    time_button = sp_button_create(row, "", false);
    lv_obj_add_event_cb(time_button, time_clicked, LV_EVENT_CLICKED, NULL);

    row = sp_row_create(source, "Automatic time zone");
    zone_auto_switch = sp_switch_create(row, zone_auto_changed);

    row = sp_row_create(source, "Time zone");
    zone_dropdown = sp_dropdown_create(row, zone_options, zone_changed);
    lv_obj_set_width(zone_dropdown, 240);

    /*How it is written.*/
    lv_obj_t * format = sp_card_create(columns);
    lv_obj_set_width(format, 0);
    lv_obj_set_flex_grow(format, 1);
    lv_obj_set_style_pad_row(format, UI_GAP / 2, LV_PART_MAIN);

    row = sp_row_create(format, "24-hour clock");
    clock_24h_switch = sp_switch_create(row, clock_24h_changed);

    /*"YYYY-MM-DD" is too wide to share a row with its name, so the name sits
     *above like a field's and the options split the full width evenly.*/
    lv_obj_t * box = sp_box_create(format);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 4, LV_PART_MAIN);
    ui_label_create(box, "Date format", UI_FONT_XS, UI_COLOR_TEXT_DIM);

    date_format_segmented = sp_segmented_create(box, date_formats, SETTINGS_DATE_COUNT, date_format_clicked);
    lv_obj_set_width(date_format_segmented, LV_PCT(100));
    for(uint32_t i = 0; i < SETTINGS_DATE_COUNT; i++) {
        lv_obj_set_flex_grow(lv_obj_get_child(date_format_segmented, (int32_t)i), 1);
    }

    row = sp_row_create(format, "Show seconds");
    seconds_switch = sp_switch_create(row, seconds_changed);

    clock_time_now(&now_local);
}

void sp_time_values(void)
{
    sp_switch_set(time_auto_switch, sp_values.time_auto);
    sp_field_set(server_field, sp_values.time_server);
    sp_switch_set(zone_auto_switch, sp_values.timezone_auto);

    int32_t zone = clock_zone_find(sp_values.timezone);
    if(zone >= 0) {
        lv_dropdown_set_selected(zone_dropdown, (uint32_t)zone);
        lv_dropdown_set_text(zone_dropdown, NULL);
    }
    else {
        /*A detected zone the list does not name, shown as it is.*/
        lv_strlcpy(zone_text, sp_values.timezone, sizeof(zone_text));
        lv_dropdown_set_text(zone_dropdown, zone_text);
    }

    sp_switch_set(clock_24h_switch, sp_values.clock_24h);
    sp_segmented_select(date_format_segmented, sp_values.date_format);
    sp_switch_set(seconds_switch, sp_values.show_seconds);

    enables_apply();
    sp_time_now(&now_local);
}

void sp_time_now(const struct tm * local)
{
    char text[40];

    now_local = *local;

    ui_format_date_long(text, sizeof(text), local);
    lv_label_set_text(lv_obj_get_child(date_button, 0), text);

    ui_format_time(text, sizeof(text), local->tm_hour, local->tm_min);
    lv_label_set_text(lv_obj_get_child(time_button, 0), text);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void options_build(void)
{
    size_t used;

    used = 0;
    for(int i = 1; i <= 31; i++) used += lv_snprintf(day_options + used, sizeof(day_options) - used, i > 1 ? "\n%d" : "%d", i);

    used = 0;
    for(int i = 0; i < 12; i++) {
        used += lv_snprintf(month_options + used, sizeof(month_options) - used, i ? "\n%s" : "%s", ui_format_month(i, false));
    }

    used = 0;
    for(int i = YEAR_FIRST; i <= YEAR_LAST; i++) {
        used += lv_snprintf(year_options + used, sizeof(year_options) - used, i > YEAR_FIRST ? "\n%d" : "%d", i);
    }

    /*Twelve first, as a 12-hour clock face reads.*/
    used = 0;
    for(int i = 0; i < 12; i++) {
        used += lv_snprintf(hour12_options + used, sizeof(hour12_options) - used, i ? "\n%d" : "%d", i == 0 ? 12 : i);
    }

    used = 0;
    for(int i = 0; i < 24; i++) used += lv_snprintf(hour24_options + used, sizeof(hour24_options) - used, i ? "\n%02d" : "%02d", i);

    used = 0;
    for(int i = 0; i < 60; i++) used += lv_snprintf(minute_options + used, sizeof(minute_options) - used, i ? "\n%02d" : "%02d", i);

    used = 0;
    for(uint32_t i = 0; i < clock_zone_count(); i++) {
        used += lv_snprintf(zone_options + used, sizeof(zone_options) - used, i ? "\n%s" : "%s", clock_zone_get(i)->name);
    }
}

/** What automatic takes over is not the user's to set. */
static void enables_apply(void)
{
    sp_enable(server_field, sp_values.time_auto);
    sp_enable(date_button, !sp_values.time_auto);
    sp_enable(time_button, !sp_values.time_auto);
    sp_enable(zone_dropdown, !sp_values.timezone_auto);
}

static void time_auto_changed(lv_event_t * e)
{
    sp_values.time_auto = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    enables_apply();
    sp_changed();
}

static void server_commit(lv_event_t * e)
{
    LV_UNUSED(e);

    const char * text = lv_textarea_get_text(server_field);
    if(text[0] == '\0' || strcmp(text, sp_values.time_server) == 0) return;

    lv_strlcpy(sp_values.time_server, text, sizeof(sp_values.time_server));
    sp_changed();
}

/** The date wheels, in the order the date format writes them. */
static void date_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    ui_picker_column_t day   = {day_options, (uint32_t)now_local.tm_mday - 1, true, 72};
    ui_picker_column_t month = {month_options, (uint32_t)now_local.tm_mon, true, 0};
    ui_picker_column_t year  = {year_options, 0, false, 96};

    int y = now_local.tm_year + 1900;
    year.selected = (uint32_t)(y < YEAR_FIRST ? 0 : (y > YEAR_LAST ? YEAR_LAST - YEAR_FIRST : y - YEAR_FIRST));

    ui_picker_column_t columns[3];
    switch(sp_values.date_format) {
        case SETTINGS_DATE_MDY: columns[0] = month; columns[1] = day;   columns[2] = year; break;
        case SETTINGS_DATE_YMD: columns[0] = year;  columns[1] = month; columns[2] = day;  break;
        default:                columns[0] = day;   columns[1] = month; columns[2] = year; break;
    }

    ui_picker_open(columns, 3, date_applied, NULL);
}

static void time_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    bool                h24 = ui_format_24h();
    ui_picker_column_t  columns[3];
    uint32_t            count = h24 ? 2 : 3;

    columns[0] = (ui_picker_column_t){h24 ? hour24_options : hour12_options,
                                      (uint32_t)(h24 ? now_local.tm_hour : now_local.tm_hour % 12), true, 0};
    columns[1] = (ui_picker_column_t){minute_options, (uint32_t)now_local.tm_min, true, 0};
    columns[2] = (ui_picker_column_t){"AM\nPM", now_local.tm_hour < 12 ? 0U : 1U, false, 0};

    ui_picker_open(columns, count, time_applied, NULL);
}

static void date_applied(const uint32_t selected[], uint32_t count, void * user)
{
    LV_UNUSED(count);
    LV_UNUSED(user);

    uint32_t day, month, year;
    switch(sp_values.date_format) {
        case SETTINGS_DATE_MDY: month = selected[0]; day = selected[1];   year = selected[2]; break;
        case SETTINGS_DATE_YMD: year = selected[0];  month = selected[1]; day = selected[2];  break;
        default:                day = selected[0];   month = selected[1]; year = selected[2]; break;
    }

    struct tm local = now_local;
    local.tm_year   = (int)(YEAR_FIRST + year) - 1900;
    local.tm_mon    = (int)month;
    local.tm_mday   = (int)day + 1;

    /*31 February becomes the last day of February.*/
    int last = clock_time_days_in_month(local.tm_year + 1900, local.tm_mon);
    if(local.tm_mday > last) local.tm_mday = last;

    sp_set_time(&local);
}

static void time_applied(const uint32_t selected[], uint32_t count, void * user)
{
    LV_UNUSED(user);

    struct tm local = now_local;

    /*The 12-hour wheel counts 12, 1, 2 ... so index 0 is noon or midnight.*/
    if(count == 3) local.tm_hour = (int)(selected[0] % 12 + (selected[2] == 1 ? 12 : 0));
    else           local.tm_hour = (int)selected[0];

    local.tm_min = (int)selected[1];
    local.tm_sec = 0;

    sp_set_time(&local);
}

static void zone_auto_changed(lv_event_t * e)
{
    sp_values.timezone_auto = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    enables_apply();
    sp_changed();
}

static void zone_changed(lv_event_t * e)
{
    const clock_zone_t * zone = clock_zone_get(lv_dropdown_get_selected(lv_event_get_target_obj(e)));
    if(!zone) return;

    lv_strlcpy(sp_values.timezone, zone->name, sizeof(sp_values.timezone));
    lv_strlcpy(sp_values.timezone_posix, zone->posix, sizeof(sp_values.timezone_posix));
    lv_dropdown_set_text(zone_dropdown, NULL);
    sp_changed();
}

static void clock_24h_changed(lv_event_t * e)
{
    sp_values.clock_24h = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    /*The feed rebuilds the UI, so every time on screen is written afresh.*/
    sp_changed();
}

static void date_format_clicked(lv_event_t * e)
{
    settings_date_format_t format = (settings_date_format_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(format == sp_values.date_format) return;

    sp_values.date_format = format;
    sp_segmented_select(date_format_segmented, format);
    sp_changed();
}

static void seconds_changed(lv_event_t * e)
{
    sp_values.show_seconds = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    sp_changed();
}
