/**
 * @file page_alarms.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/alarms/page_alarms.h"
#include "ui/ui_format.h"
#include "ui/ui_keyboard.h"
#include "ui/ui_theme.h"

#include <stdio.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** Alarm cards laid out across the list. */
#define ALARM_CARD_WIDTH  272
#define ALARM_CARD_HEIGHT 104

/** Height of the time wheels in the editor. */
#define EDITOR_ROLLER_HEIGHT 132

/** Size of the day toggles. */
#define DAY_BUTTON_SIZE 46

/** Index meaning "the editor is creating a new alarm" rather than editing. */
#define EDITING_NONE UINT32_MAX

/**********************
 *      TYPEDEFS
 **********************/

/** The widgets of one alarm card that change with its contents. */
typedef struct {
    lv_obj_t * root;
    lv_obj_t * time;
    lv_obj_t * meridiem;
    lv_obj_t * name;
    lv_obj_t * repeat;
    lv_obj_t * toggle;
} alarm_card_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);

static void list_view_create(lv_obj_t * parent);
static void editor_view_create(lv_obj_t * parent);

static void alarms_sort(void);
static void list_refresh(void);
static void add_button_refresh(void);
static void editor_open(uint32_t index);
static void editor_close(void);
static void editor_load(const page_alarm_t * alarm);
static void editor_store(page_alarm_t * alarm);
static void alarms_changed(void);
static void sound_options_refresh(void);
static void sound_select(uint8_t tone, const char * station);

static lv_obj_t * flow_create(lv_obj_t * parent, lv_flex_flow_t flow);
static lv_obj_t * field_create(lv_obj_t * parent, const char * caption);
static lv_obj_t * action_button_create(lv_obj_t * parent, const char * text, lv_color_t color);

static void card_clicked(lv_event_t * e);
static void card_toggled(lv_event_t * e);
static void add_clicked(lv_event_t * e);
static void cancel_clicked(lv_event_t * e);
static void save_clicked(lv_event_t * e);
static void delete_clicked(lv_event_t * e);
static void day_clicked(lv_event_t * e);
static void name_focused(lv_event_t * e);
static void keyboard_done(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "Alarms",
    .icon    = LV_SYMBOL_BELL,
    .create  = create,
    .on_show = NULL,
    .on_hide = NULL,
};

static const char * const tone_names[PAGE_ALARM_TONE_COUNT] = {
    "Radar", "Chimes", "Beacon", "Signal", "Birdsong",
};

/*Monday first, matching page_alarm_days_t.*/
static const char * const day_names[7] = {"M", "T", "W", "T", "F", "S", "S"};

/*Enough to show the page doing something before storage exists.*/
static const page_alarm_t alarm_defaults[] = {
    {"Wake up",   7,  0, PAGE_ALARM_WEEKDAYS,  true,  true,  PAGE_ALARM_TONE_RADAR},
    {"Stand-up",  11, 0, PAGE_ALARM_MON | PAGE_ALARM_WED | PAGE_ALARM_FRI,
                                               true,  false, PAGE_ALARM_TONE_CHIMES},
    {"Lie-in",    9, 30, PAGE_ALARM_WEEKENDS,  false, true,  PAGE_ALARM_TONE_BIRDSONG},
};

static page_alarm_t alarms[PAGE_ALARMS_MAX];
static uint32_t     alarm_count;

static page_alarms_changed_cb_t changed_cb;

/*The stations the sound menu offers after the tones.*/
static struct {
    char uuid[PAGE_ALARMS_STATION_LEN];
    char name[PAGE_ALARMS_STATION_NAME_LEN];
} stations[PAGE_ALARMS_STATIONS_MAX];
static uint32_t station_count;

/*The tone of the alarm in the editor, kept to fall back on when a station is picked.*/
static uint8_t editor_tone;

/*Which alarm the editor is working on, or EDITING_NONE for a new one.*/
static uint32_t editing;

/*Structure*/
static lv_obj_t * list_view;
static lv_obj_t * editor_view;
static lv_obj_t * card_area;
static lv_obj_t * add_button;

static alarm_card_t cards[PAGE_ALARMS_MAX];

/*Editor fields*/
static lv_obj_t * editor_title;
static lv_obj_t * hour_roller;
static lv_obj_t * minute_roller;
static lv_obj_t * meridiem_roller;
static lv_obj_t * name_field;
static lv_obj_t * tone_field;
static lv_obj_t * snooze_switch;
static lv_obj_t * delete_button;
static lv_obj_t * keyboard;
static lv_obj_t * day_buttons[7];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const ui_page_t * page_alarms_desc(void)
{
    return &desc;
}

void page_alarms_set_alarms(const page_alarm_t list[], uint32_t count)
{
    if(count > PAGE_ALARMS_MAX) count = PAGE_ALARMS_MAX;

    memcpy(alarms, list, count * sizeof(alarms[0]));
    alarm_count = count;

    alarms_sort();
    list_refresh();
}

const page_alarm_t * page_alarms_get_alarms(uint32_t * count)
{
    if(count) *count = alarm_count;
    return alarms;
}

void page_alarms_set_changed_cb(page_alarms_changed_cb_t cb)
{
    changed_cb = cb;
}

void page_alarms_days_text(uint8_t days, char * buf, size_t len)
{
    if(!buf || len == 0) return;

    if(days == 0)                        { lv_snprintf(buf, len, "Never");     return; }
    if(days == PAGE_ALARM_EVERY_DAY)     { lv_snprintf(buf, len, "Every day"); return; }
    if(days == PAGE_ALARM_WEEKDAYS)      { lv_snprintf(buf, len, "Weekdays");  return; }
    if(days == PAGE_ALARM_WEEKENDS)      { lv_snprintf(buf, len, "Weekends");  return; }

    /*Otherwise spell the days out: "Mon Wed Fri".*/
    static const char * const abbrev[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    size_t used = 0;

    buf[0] = '\0';
    for(uint32_t i = 0; i < 7; i++) {
        if((days & (1 << i)) == 0) continue;
        int written = lv_snprintf(buf + used, len - used, used ? " %s" : "%s", abbrev[i]);
        if(written <= 0) break;
        used += (size_t)written;
        if(used >= len) break;
    }
}

const char * page_alarms_tone_name(uint8_t tone)
{
    if(tone >= PAGE_ALARM_TONE_COUNT) tone = 0;
    return tone_names[tone];
}

void page_alarms_set_stations(const page_alarm_station_t list[], uint32_t count)
{
    if(count > PAGE_ALARMS_STATIONS_MAX) count = PAGE_ALARMS_STATIONS_MAX;

    /*What an open editor has picked, to pick again from the new menu.*/
    char    picked[PAGE_ALARMS_STATION_LEN] = "";
    uint8_t tone                            = editor_tone;

    if(tone_field) {
        uint32_t sound = lv_dropdown_get_selected(tone_field);
        if(sound < PAGE_ALARM_TONE_COUNT) tone = (uint8_t)sound;
        else if(sound - PAGE_ALARM_TONE_COUNT < station_count) {
            lv_strlcpy(picked, stations[sound - PAGE_ALARM_TONE_COUNT].uuid, sizeof(picked));
        }
    }

    for(uint32_t i = 0; i < count; i++) {
        lv_strlcpy(stations[i].uuid, list[i].uuid ? list[i].uuid : "", sizeof(stations[i].uuid));
        lv_strlcpy(stations[i].name, list[i].name ? list[i].name : "", sizeof(stations[i].name));

        /*A line break would split the menu entry in two.*/
        for(char * c = stations[i].name; *c; c++) {
            if(*c == '\n' || *c == '\r') *c = ' ';
        }
    }
    station_count = count;

    if(!tone_field) return;

    uint8_t fallback = editor_tone;
    sound_options_refresh();
    sound_select(tone, picked);
    editor_tone = fallback;
}

const char * page_alarms_station_name(const char * uuid)
{
    for(uint32_t i = 0; uuid && uuid[0] && i < station_count; i++) {
        if(strcmp(stations[i].uuid, uuid) == 0) return stations[i].name;
    }
    return NULL;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create(lv_obj_t * parent)
{
    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(root, false);

    list_view_create(root);
    editor_view_create(root);

    page_alarms_set_alarms(alarm_defaults,
                           (uint32_t)(sizeof(alarm_defaults) / sizeof(alarm_defaults[0])));
    editor_close();

    return root;
}

/**
 * Transparent, non-scrolling container, as used across the other pages.
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
 * A captioned slot in the editor: dim label with the control beneath it.
 */
static lv_obj_t * field_create(lv_obj_t * parent, const char * caption)
{
    lv_obj_t * obj = flow_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, 6, LV_PART_MAIN);

    lv_obj_t * label = ui_label_create(obj, caption, UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);

    return obj;
}

static lv_obj_t * action_button_create(lv_obj_t * parent, const char * text, lv_color_t color)
{
    lv_obj_t * btn = lv_button_create(parent);

    lv_obj_set_height(btn, UI_TOUCH_MIN);
    lv_obj_set_style_min_width(btn, 110, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, UI_RADIUS - 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_center(label);

    return btn;
}

static void list_view_create(lv_obj_t * parent)
{
    list_view = flow_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(list_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(list_view, UI_GAP, LV_PART_MAIN);

    /*Header: caption on the left, add button hard right.*/
    lv_obj_t * header = flow_create(list_view, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * caption = ui_label_create(header, "ALARMS", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(caption, 1, LV_PART_MAIN);
    lv_obj_set_flex_grow(caption, 1);

    add_button = lv_button_create(header);
    lv_obj_set_size(add_button, UI_TOUCH_MIN, UI_TOUCH_MIN);
    lv_obj_set_style_radius(add_button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(add_button, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(add_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(add_button, add_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * plus = lv_label_create(add_button);
    lv_label_set_text(plus, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(plus, UI_FONT_MD, LV_PART_MAIN);
    lv_obj_center(plus);

    /*The cards themselves, wrapping across the width and scrolling down.*/
    card_area = lv_obj_create(list_view);
    lv_obj_set_width(card_area, LV_PCT(100));
    lv_obj_set_flex_grow(card_area, 1);
    lv_obj_set_style_bg_opa(card_area, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(card_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card_area, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(card_area, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(card_area, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(card_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(card_area, LV_SCROLLBAR_MODE_AUTO);

    for(uint32_t i = 0; i < PAGE_ALARMS_MAX; i++) {
        lv_obj_t * card = lv_obj_create(card_area);
        lv_obj_set_size(card, ALARM_CARD_WIDTH, ALARM_CARD_HEIGHT);
        lv_obj_set_style_bg_color(card, UI_COLOR_CARD, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, UI_COLOR_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(card, UI_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, UI_GAP, LV_PART_MAIN);
        lv_obj_set_style_pad_row(card, 2, LV_PART_MAIN);
        lv_obj_set_scrollable(card, false);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(card, card_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);

        /*Time and the on/off switch share the top line.*/
        lv_obj_t * top = flow_create(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(top, LV_PCT(100));
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(top, 6, LV_PART_MAIN);

        cards[i].time     = ui_label_create(top, "", UI_FONT_XL, UI_COLOR_TEXT);
        cards[i].meridiem = ui_label_create(top, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);

        lv_obj_t * spacer = flow_create(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_grow(spacer, 1);

        cards[i].toggle = lv_switch_create(top);
        lv_obj_set_size(cards[i].toggle, 52, 28);
        lv_obj_set_style_bg_color(cards[i].toggle, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(cards[i].toggle, UI_COLOR_ACCENT,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(cards[i].toggle, card_toggled, LV_EVENT_VALUE_CHANGED,
                            (void *)(lv_uintptr_t)i);

        cards[i].name = ui_label_create(card, "", UI_FONT_SM, UI_COLOR_TEXT);
        lv_obj_set_width(cards[i].name, LV_PCT(100));
        ui_label_single_line(cards[i].name, UI_FONT_SM);

        cards[i].repeat = ui_label_create(card, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
        lv_obj_set_width(cards[i].repeat, LV_PCT(100));
        ui_label_single_line(cards[i].repeat, UI_FONT_XS);

        cards[i].root = card;
    }
}

static void editor_view_create(lv_obj_t * parent)
{
    editor_view = flow_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(editor_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(editor_view, UI_GAP, LV_PART_MAIN);

    /*Header: cancel, title, save -- the same shape as the iOS editor.*/
    lv_obj_t * header = flow_create(editor_view, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, UI_GAP, LV_PART_MAIN);

    lv_obj_t * cancel = action_button_create(header, "Cancel", UI_COLOR_TEXT_DIM);
    lv_obj_add_event_cb(cancel, cancel_clicked, LV_EVENT_CLICKED, NULL);

    editor_title = ui_label_create(header, "", UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(editor_title, 1);
    lv_obj_set_style_text_align(editor_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t * save = action_button_create(header, "Save", UI_COLOR_ACCENT);
    lv_obj_add_event_cb(save, save_clicked, LV_EVENT_CLICKED, NULL);

    /*Body: time wheels on the left, the rest of the settings on the right.*/
    lv_obj_t * body = flow_create(editor_view, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_column(body, UI_GAP * 2, LV_PART_MAIN);

    lv_obj_t * time_field = field_create(body, "TIME");

    lv_obj_t * wheels = flow_create(time_field, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(wheels, 4, LV_PART_MAIN);

    /*The hour wheel follows the clock setting: 00 to 23, or 12, 1 ... 11
     *beside an AM/PM wheel. The page is rebuilt when the setting changes.*/
    bool        h24 = ui_format_24h();
    static char hour_options[24 * 3 + 1];
    static char minute_options[60 * 3 + 1];
    size_t used = 0;
    for(uint32_t i = 0; i < (h24 ? 24U : 12U); i++) {
        if(h24) used += (size_t)lv_snprintf(hour_options + used, sizeof(hour_options) - used,
                                            i ? "\n%02d" : "%02d", (int)i);
        else    used += (size_t)lv_snprintf(hour_options + used, sizeof(hour_options) - used,
                                            i ? "\n%d" : "%d", i == 0 ? 12 : (int)i);
    }
    used = 0;
    for(uint32_t i = 0; i < 60; i++) {
        used += (size_t)lv_snprintf(minute_options + used, sizeof(minute_options) - used,
                                    i ? "\n%02d" : "%02d", (int)i);
    }

    hour_roller = lv_roller_create(wheels);
    lv_roller_set_options(hour_roller, hour_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_height(hour_roller, EDITOR_ROLLER_HEIGHT);
    lv_obj_set_style_text_font(hour_roller, UI_FONT_LG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hour_roller, UI_COLOR_ACCENT, LV_PART_SELECTED);

    minute_roller = lv_roller_create(wheels);
    lv_roller_set_options(minute_roller, minute_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_height(minute_roller, EDITOR_ROLLER_HEIGHT);
    lv_obj_set_style_text_font(minute_roller, UI_FONT_LG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(minute_roller, UI_COLOR_ACCENT, LV_PART_SELECTED);

    meridiem_roller = lv_roller_create(wheels);
    lv_roller_set_options(meridiem_roller, "AM\nPM", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_height(meridiem_roller, EDITOR_ROLLER_HEIGHT);
    lv_obj_set_style_text_font(meridiem_roller, UI_FONT_MD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(meridiem_roller, UI_COLOR_ACCENT, LV_PART_SELECTED);
    lv_obj_set_hidden(meridiem_roller, h24);

    /*Right-hand settings column.*/
    lv_obj_t * settings = flow_create(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(settings, LV_PCT(100));
    lv_obj_set_flex_grow(settings, 1);
    lv_obj_set_style_pad_row(settings, UI_GAP, LV_PART_MAIN);

    lv_obj_t * name_slot = field_create(settings, "NAME");
    lv_obj_set_width(name_slot, LV_PCT(100));

    name_field = lv_textarea_create(name_slot);
    lv_obj_set_width(name_field, LV_PCT(100));
    lv_textarea_set_one_line(name_field, true);
    /*Counted in characters, not bytes.*/
    lv_textarea_set_max_length(name_field, PAGE_ALARMS_NAME_CHARS);
    /*A long name scrolls along with the cursor; a scroll bar would only run
     *across the bottom of the text.*/
    lv_obj_set_scrollbar_mode(name_field, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_placeholder_text(name_field, "Alarm");
    lv_obj_set_style_bg_color(name_field, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_border_width(name_field, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(name_field, UI_FONT_SM, LV_PART_MAIN);
    lv_obj_add_event_cb(name_field, name_focused, LV_EVENT_FOCUSED, NULL);

    lv_obj_t * days_slot = field_create(settings, "REPEAT");
    lv_obj_set_width(days_slot, LV_PCT(100));

    lv_obj_t * days_row = flow_create(days_slot, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(days_row, LV_PCT(100));
    lv_obj_set_style_pad_column(days_row, 6, LV_PART_MAIN);

    for(uint32_t i = 0; i < 7; i++) {
        lv_obj_t * btn = lv_button_create(days_row);
        lv_obj_set_size(btn, DAY_BUTTON_SIZE, DAY_BUTTON_SIZE);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_event_cb(btn, day_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, day_names[i]);
        lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
        lv_obj_center(label);

        day_buttons[i] = btn;
    }

    lv_obj_t * extras = flow_create(settings, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(extras, LV_PCT(100));
    lv_obj_set_style_pad_column(extras, UI_GAP * 2, LV_PART_MAIN);

    lv_obj_t * tone_slot = field_create(extras, "SOUND");
    lv_obj_set_flex_grow(tone_slot, 1);

    tone_field = lv_dropdown_create(tone_slot);
    lv_obj_set_width(tone_field, LV_PCT(100));
    lv_obj_set_style_bg_color(tone_field, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_border_width(tone_field, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(tone_field, UI_FONT_SM, LV_PART_MAIN);
    sound_options_refresh();

    lv_obj_t * snooze_slot = field_create(extras, "SNOOZE");

    snooze_switch = lv_switch_create(snooze_slot);
    lv_obj_set_size(snooze_switch, 60, 32);
    lv_obj_set_style_bg_color(snooze_switch, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(snooze_switch, UI_COLOR_ACCENT,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);

    delete_button = action_button_create(settings, "Delete Alarm", UI_COLOR_BAD);
    lv_obj_set_width(delete_button, LV_PCT(100));
    lv_obj_add_event_cb(delete_button, delete_clicked, LV_EVENT_CLICKED, NULL);

    /*Only up while the name is being typed.*/
    keyboard = ui_keyboard_create(editor_view);
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(50));
    lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_CANCEL, NULL);
    lv_obj_set_hidden(keyboard, true);
}

/**
 * Order the list the way it is shown: by time of day, earliest first.
 *
 * Insertion sort, because the list is short and it is stable -- two alarms set
 * to the same time keep the order they were added in rather than trading
 * places on every unrelated edit.
 *
 * The array index is what the cards use to identify an alarm, so anything
 * holding one across a sort would be pointing at the wrong alarm. Callers run
 * it only after they have finished with theirs.
 */
static void alarms_sort(void)
{
    for(uint32_t i = 1; i < alarm_count; i++) {
        page_alarm_t moving  = alarms[i];
        uint32_t     minutes = (uint32_t)moving.hour * 60 + moving.minute;
        uint32_t     slot    = i;

        while(slot > 0) {
            const page_alarm_t * prev = &alarms[slot - 1];
            if((uint32_t)prev->hour * 60 + prev->minute <= minutes) break;
            alarms[slot] = alarms[slot - 1];
            slot--;
        }

        alarms[slot] = moving;
    }
}

/**
 * Push the model into the cards, hiding the slots that have no alarm.
 */
static void list_refresh(void)
{
    if(!card_area) return;

    for(uint32_t i = 0; i < PAGE_ALARMS_MAX; i++) {
        if(i >= alarm_count) {
            lv_obj_set_hidden(cards[i].root, true);
            continue;
        }

        const page_alarm_t * alarm = &alarms[i];
        const char *         meridiem = ui_format_meridiem(alarm->hour);
        char                 time_text[8];

        ui_format_clock(time_text, sizeof(time_text), alarm->hour, alarm->minute);

        lv_obj_set_hidden(cards[i].root, false);
        lv_label_set_text(cards[i].time, time_text);
        /*On the 24-hour clock there is no AM/PM to show beside the time.*/
        lv_label_set_text(cards[i].meridiem, meridiem ? meridiem : "");
        lv_obj_set_hidden(cards[i].meridiem, meridiem == NULL);
        lv_label_set_text(cards[i].name, alarm->name[0] ? alarm->name : "Alarm");

        char repeat[48];
        page_alarms_days_text(alarm->days, repeat, sizeof(repeat));
        lv_label_set_text(cards[i].repeat, repeat);

        if(alarm->enabled) lv_obj_add_state(cards[i].toggle, LV_STATE_CHECKED);
        else               lv_obj_remove_state(cards[i].toggle, LV_STATE_CHECKED);

        /*A disabled alarm reads as inactive without disappearing.*/
        lv_obj_set_style_text_color(cards[i].time,
                                    alarm->enabled ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM,
                                    LV_PART_MAIN);
    }

    add_button_refresh();
}

/**
 * The only thing that tells the user about PAGE_ALARMS_MAX: at the limit the
 * add button simply stops working.
 */
static void add_button_refresh(void)
{
    bool full = alarm_count >= PAGE_ALARMS_MAX;

    if(full) lv_obj_add_state(add_button, LV_STATE_DISABLED);
    else     lv_obj_remove_state(add_button, LV_STATE_DISABLED);

    lv_obj_set_style_bg_color(add_button, full ? UI_COLOR_TRACK : UI_COLOR_ACCENT, LV_PART_MAIN);
}

static void alarms_changed(void)
{
    alarms_sort();
    list_refresh();
    if(changed_cb) changed_cb(alarms, alarm_count);
}

static void editor_load(const page_alarm_t * alarm)
{
    /*The hour wheel holds 00 to 23 on the 24-hour clock, and 12, 1 ... 11
     *beside AM/PM otherwise, so index 0 is noon or midnight there.*/
    uint32_t hour = ui_format_24h() ? alarm->hour : alarm->hour % 12U;

    lv_roller_set_selected(hour_roller, hour, LV_ANIM_OFF);
    lv_roller_set_selected(minute_roller, alarm->minute, LV_ANIM_OFF);
    lv_roller_set_selected(meridiem_roller, alarm->hour < 12 ? 0 : 1, LV_ANIM_OFF);

    lv_textarea_set_text(name_field, alarm->name);
    sound_select(alarm->tone, alarm->station);

    if(alarm->snooze) lv_obj_add_state(snooze_switch, LV_STATE_CHECKED);
    else              lv_obj_remove_state(snooze_switch, LV_STATE_CHECKED);

    for(uint32_t i = 0; i < 7; i++) {
        if(alarm->days & (1 << i)) lv_obj_add_state(day_buttons[i], LV_STATE_CHECKED);
        else                       lv_obj_remove_state(day_buttons[i], LV_STATE_CHECKED);
    }
}

static void editor_store(page_alarm_t * alarm)
{
    uint32_t hour = lv_roller_get_selected(hour_roller);
    bool     pm   = lv_roller_get_selected(meridiem_roller) == 1;

    /*On the 12-hour clock the wheel counts 12, 1, 2 ... so index 0 is noon or
     *midnight, and the AM/PM wheel says which.*/
    if(ui_format_24h()) alarm->hour = (uint8_t)hour;
    else                alarm->hour = (uint8_t)((hour % 12) + (pm ? 12 : 0));
    alarm->minute = (uint8_t)lv_roller_get_selected(minute_roller);
    alarm->snooze = lv_obj_has_state(snooze_switch, LV_STATE_CHECKED);

    uint32_t sound = lv_dropdown_get_selected(tone_field);
    if(sound < PAGE_ALARM_TONE_COUNT) {
        alarm->tone       = (uint8_t)sound;
        alarm->station[0] = '\0';
    }
    else if(sound - PAGE_ALARM_TONE_COUNT < station_count) {
        /*A station keeps the alarm's tone, to fall back on.*/
        alarm->tone = editor_tone;
        lv_strlcpy(alarm->station, stations[sound - PAGE_ALARM_TONE_COUNT].uuid, sizeof(alarm->station));
    }

    ui_format_text_copy(alarm->name, sizeof(alarm->name), lv_textarea_get_text(name_field));

    alarm->days = 0;
    for(uint32_t i = 0; i < 7; i++) {
        if(lv_obj_has_state(day_buttons[i], LV_STATE_CHECKED)) alarm->days |= (uint8_t)(1 << i);
    }
}

static void editor_open(uint32_t index)
{
    editing = index;

    if(index == EDITING_NONE) {
        /*Sensible defaults for a new alarm, as iOS does.*/
        static const page_alarm_t blank = {"", 7, 0, 0, true, true, PAGE_ALARM_TONE_RADAR};
        editor_load(&blank);
        lv_label_set_text(editor_title, "Add Alarm");
        lv_obj_set_hidden(delete_button, true);
    }
    else {
        editor_load(&alarms[index]);
        lv_label_set_text(editor_title, "Edit Alarm");
        lv_obj_set_hidden(delete_button, false);
    }

    lv_obj_set_hidden(keyboard, true);
    lv_obj_set_hidden(list_view, true);
    lv_obj_set_hidden(editor_view, false);
}

static void editor_close(void)
{
    lv_obj_set_hidden(keyboard, true);
    lv_obj_set_hidden(editor_view, true);
    lv_obj_set_hidden(list_view, false);
}

static void card_clicked(lv_event_t * e)
{
    uint32_t index = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(index < alarm_count) editor_open(index);
}

static void card_toggled(lv_event_t * e)
{
    uint32_t index = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(index >= alarm_count) return;

    alarms[index].enabled = lv_obj_has_state(cards[index].toggle, LV_STATE_CHECKED);
    alarms_changed();
}

static void add_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(alarm_count >= PAGE_ALARMS_MAX) return;
    editor_open(EDITING_NONE);
}

static void cancel_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    editor_close();
}

static void save_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    if(editing == EDITING_NONE) {
        if(alarm_count >= PAGE_ALARMS_MAX) return;
        alarms[alarm_count].enabled = true;
        editor_store(&alarms[alarm_count]);
        alarm_count++;
    }
    else {
        editor_store(&alarms[editing]);
    }

    editor_close();
    alarms_changed();
}

static void delete_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(editing == EDITING_NONE || editing >= alarm_count) return;

    for(uint32_t i = editing; i + 1 < alarm_count; i++) alarms[i] = alarms[i + 1];
    alarm_count--;

    editor_close();
    alarms_changed();
}

static void day_clicked(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);

    if(lv_obj_has_state(btn, LV_STATE_CHECKED)) lv_obj_remove_state(btn, LV_STATE_CHECKED);
    else                                        lv_obj_add_state(btn, LV_STATE_CHECKED);
}

static void name_focused(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_keyboard_attach(keyboard, name_field, UI_KEYBOARD_SENTENCE);
    lv_obj_set_hidden(keyboard, false);
}

static void keyboard_done(lv_event_t * e)
{
    LV_UNUSED(e);
    lv_obj_set_hidden(keyboard, true);
}

/** Fill the sound menu: the tones, then the saved stations. */
static void sound_options_refresh(void)
{
    if(!tone_field) return;

    char   options[PAGE_ALARM_TONE_COUNT * 24 + PAGE_ALARMS_STATIONS_MAX * (PAGE_ALARMS_STATION_NAME_LEN + 8)];
    size_t used = 0;

    for(uint32_t i = 0; i < PAGE_ALARM_TONE_COUNT && used < sizeof(options); i++) {
        used += (size_t)lv_snprintf(options + used, sizeof(options) - used, "%s" LV_SYMBOL_BELL "  %s",
                                    i ? "\n" : "", tone_names[i]);
    }
    for(uint32_t i = 0; i < station_count && used < sizeof(options); i++) {
        used += (size_t)lv_snprintf(options + used, sizeof(options) - used, "\n" LV_SYMBOL_AUDIO "  %s",
                                    stations[i].name);
    }

    lv_dropdown_set_options(tone_field, options);
}

/** Pick a sound in the menu: the station if it is still saved, else the tone. */
static void sound_select(uint8_t tone, const char * station)
{
    uint32_t index = tone < PAGE_ALARM_TONE_COUNT ? tone : 0;

    editor_tone = (uint8_t)index;

    for(uint32_t i = 0; station && station[0] && i < station_count; i++) {
        if(strcmp(stations[i].uuid, station) == 0) index = PAGE_ALARM_TONE_COUNT + i;
    }

    lv_dropdown_set_selected(tone_field, index);
}
