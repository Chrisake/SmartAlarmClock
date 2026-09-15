/**
 * @file settings_weather.c
 *
 * Weather tab, in two cards. Left, where the clock is: found from the public
 * IP, or a city searched for by name. Right, more cities the weather page can
 * be switched to, searched for the same way, each with a button to remove it.
 *
 * A search goes out when Enter is pressed on the keyboard. The cities found
 * come back under the field, with their region and country to tell namesakes
 * apart, and a tap on one takes it.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/settings_private.h"

#include <math.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define FIELD_RADIUS 10

/** Characters a city's name can be typed in: its bytes still fit a search. */
#define NAME_CHARS_MAX 40

/** A found city's button: its name over its region. */
#define FOUND_MIN_HEIGHT 52

/** The button removing a city from the list. */
#define REMOVE_SIZE 40

/** Two cities this close, in degrees, are taken for the same one. */
#define SAME_PLACE_DEGREES 0.001

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void search_create(lv_obj_t * parent, page_settings_search_t search, const char * name);
static void location_show(void);
static void places_show(void);
static void found_clear(page_settings_search_t search);
static void children_drop(lv_obj_t * box);
static bool name_trimmed(const char * text, char * out, size_t size);

static void auto_changed(lv_event_t * e);
static void search_ready(lv_event_t * e);
static void found_clicked(lv_event_t * e);
static void remove_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_obj_t * auto_switch;
static lv_obj_t * location_value;
static lv_obj_t * home_search;   /**< The clock's city: field and cities found, only while set by hand */
static lv_obj_t * fields[PAGE_SETTINGS_SEARCH_COUNT];
static lv_obj_t * found_boxes[PAGE_SETTINGS_SEARCH_COUNT];
static lv_obj_t * place_list;
static lv_obj_t * places_hint;

/** The cities on show under each field, for when one is tapped. */
static struct {
    char   name[SETTINGS_LOCATION_LEN];
    double latitude;
    double longitude;
} found[PAGE_SETTINGS_SEARCH_COUNT][PAGE_SETTINGS_FOUND_MAX];

/** Where the automatic location found the clock; empty while unknown. */
static char detected[SETTINGS_LOCATION_LEN];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sp_weather_create(lv_obj_t * tab)
{
    lv_obj_t * columns = sp_box_create(tab);
    lv_obj_set_width(columns, LV_PCT(100));
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(columns, UI_GAP, LV_PART_MAIN);

    /*Where the clock is.*/
    lv_obj_t * home = sp_card_create(columns);
    lv_obj_set_width(home, 0);
    lv_obj_set_flex_grow(home, 1);
    lv_obj_set_style_pad_row(home, UI_GAP / 2, LV_PART_MAIN);

    lv_obj_t * row = sp_row_create(home, "Automatic location");
    auto_switch    = sp_switch_create(row, auto_changed);

    row            = sp_row_create(home, "Location");
    location_value = ui_label_create(row, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_max_width(location_value, LV_PCT(65), LV_PART_MAIN);
    ui_label_single_line(location_value, UI_FONT_SM);

    home_search = sp_box_create(home);
    lv_obj_set_width(home_search, LV_PCT(100));
    lv_obj_set_flex_flow(home_search, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(home_search, UI_GAP / 2, LV_PART_MAIN);
    search_create(home_search, PAGE_SETTINGS_SEARCH_HOME, "The clock's city");

    /*More cities for the weather page.*/
    lv_obj_t * extra = sp_card_create(columns);
    lv_obj_set_width(extra, 0);
    lv_obj_set_flex_grow(extra, 1);
    lv_obj_set_style_pad_row(extra, UI_GAP / 2, LV_PART_MAIN);

    search_create(extra, PAGE_SETTINGS_SEARCH_EXTRA, "Add a city to the weather page");

    place_list = sp_box_create(extra);
    lv_obj_set_width(place_list, LV_PCT(100));
    lv_obj_set_flex_flow(place_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(place_list, 4, LV_PART_MAIN);

    places_hint = ui_label_create(extra, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(places_hint, LV_PCT(100));
    lv_label_set_long_mode(places_hint, LV_LABEL_LONG_MODE_WRAP);
}

void sp_weather_values(void)
{
    sp_switch_set(auto_switch, sp_values.location_auto);
    lv_obj_set_hidden(home_search, sp_values.location_auto);

    location_show();
    places_show();
}

void sp_weather_found(page_settings_search_t search, page_settings_found_t state,
                      const page_settings_place_t places[], uint32_t count)
{
    lv_obj_t * box = found_boxes[search];

    children_drop(box);
    lv_obj_set_hidden(box, false);

    if(state != PAGE_SETTINGS_FOUND_DONE || count == 0) {
        const char * text = state == PAGE_SETTINGS_FOUND_BUSY   ? "Searching..."
                            : state == PAGE_SETTINGS_FOUND_FAILED ? "Couldn't search for the city. Check the connection "
                                                                    "and try again."
                                                                  : "No city by that name was found.";

        lv_obj_t * label = ui_label_create(box, text, UI_FONT_SM,
                                           state == PAGE_SETTINGS_FOUND_FAILED ? UI_COLOR_WARN : UI_COLOR_TEXT_DIM);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        return;
    }

    if(count > PAGE_SETTINGS_FOUND_MAX) count = PAGE_SETTINGS_FOUND_MAX;

    for(uint32_t i = 0; i < count; i++) {
        lv_strlcpy(found[search][i].name, places[i].name ? places[i].name : "", sizeof(found[search][i].name));
        found[search][i].latitude  = places[i].latitude;
        found[search][i].longitude = places[i].longitude;

        lv_obj_t * option = lv_button_create(box);
        lv_obj_set_size(option, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(option, FOUND_MIN_HEIGHT, LV_PART_MAIN);
        lv_obj_set_style_radius(option, FIELD_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(option, UI_GAP, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(option, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_row(option, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(option, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_flex_flow(option, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(option, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(option, found_clicked, LV_EVENT_CLICKED,
                            (void *)(lv_uintptr_t)(search * PAGE_SETTINGS_FOUND_MAX + i));

        lv_obj_t * name = ui_label_create(option, found[search][i].name, UI_FONT_SM, UI_COLOR_TEXT);
        lv_obj_set_width(name, LV_PCT(100));
        ui_label_single_line(name, UI_FONT_SM);

        if(places[i].region && places[i].region[0]) {
            lv_obj_t * region = ui_label_create(option, places[i].region, UI_FONT_XS, UI_COLOR_TEXT_DIM);
            lv_obj_set_width(region, LV_PCT(100));
            ui_label_single_line(region, UI_FONT_XS);
        }
    }
}

void sp_weather_detected(const char * name)
{
    lv_strlcpy(detected, name ? name : "", sizeof(detected));
    location_show();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** A field to type a city's name in, and a box under it for the cities found. */
static void search_create(lv_obj_t * parent, page_settings_search_t search, const char * name)
{
    sp_field_create(parent, name, "Type a city, then press Enter", SP_FIELD_TEXT, &fields[search]);
    lv_textarea_set_max_length(fields[search], NAME_CHARS_MAX);
    lv_obj_add_event_cb(fields[search], search_ready, LV_EVENT_READY, (void *)(lv_uintptr_t)search);

    found_boxes[search] = sp_box_create(parent);
    lv_obj_set_width(found_boxes[search], LV_PCT(100));
    lv_obj_set_flex_flow(found_boxes[search], LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(found_boxes[search], 6, LV_PART_MAIN);
    lv_obj_set_hidden(found_boxes[search], true);
}

/** The clock's location: where it was found, or the city chosen. */
static void location_show(void)
{
    if(!location_value) return;

    const char * text;
    if(sp_values.location_auto) text = detected[0] ? detected : "Finding out...";
    else                        text = sp_values.location_name[0] ? sp_values.location_name : "Not set";

    lv_label_set_text(location_value, text);
}

/** The cities added, each with its remove button, and what to say about the list. */
static void places_show(void)
{
    children_drop(place_list);

    for(uint32_t i = 0; i < sp_values.place_count && i < SETTINGS_PLACES_MAX; i++) {
        lv_obj_t * row = sp_row_create(place_list, sp_values.places[i].name);

        lv_obj_t * remove = lv_button_create(row);
        lv_obj_set_size(remove, REMOVE_SIZE, REMOVE_SIZE);
        lv_obj_set_style_radius(remove, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(remove, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(remove, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(remove, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(remove, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(remove, 8);
        lv_obj_add_event_cb(remove, remove_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
        lv_obj_center(ui_label_create(remove, LV_SYMBOL_CLOSE, UI_FONT_SM, UI_COLOR_TEXT_DIM));
    }

    bool full = sp_values.place_count >= SETTINGS_PLACES_MAX;
    sp_enable(fields[PAGE_SETTINGS_SEARCH_EXTRA], !full);
    if(full) found_clear(PAGE_SETTINGS_SEARCH_EXTRA);

    if(sp_values.place_count == 0) {
        lv_label_set_text(places_hint,
                          "The weather page can switch between the clock's location and the cities added here.");
    }
    else if(full) {
        lv_label_set_text_fmt(places_hint, "%d cities at most: remove one to add another.", SETTINGS_PLACES_MAX);
    }
    lv_obj_set_hidden(places_hint, sp_values.place_count > 0 && !full);
}

static void found_clear(page_settings_search_t search)
{
    children_drop(found_boxes[search]);
    lv_obj_set_hidden(found_boxes[search], true);
}

/**
 * Take a box's children out of sight and delete them shortly after, not at
 * once: the button just tapped may be one of them, and is still in its event.
 */
static void children_drop(lv_obj_t * box)
{
    for(uint32_t i = 0; i < lv_obj_get_child_count(box); i++) {
        lv_obj_t * child = lv_obj_get_child(box, (int32_t)i);

        /*Hidden here already, and on its way out.*/
        if(lv_obj_is_hidden(child)) continue;

        lv_obj_set_hidden(child, true);
        lv_obj_delete_async(child);
    }
}

/** @return   false if nothing but spaces was typed */
static bool name_trimmed(const char * text, char * out, size_t size)
{
    while(*text == ' ') text++;

    size_t len = strlen(text);
    while(len > 0 && text[len - 1] == ' ') len--;
    if(len == 0 || len >= size) return false;

    memcpy(out, text, len);
    out[len] = '\0';
    return true;
}

static void auto_changed(lv_event_t * e)
{
    sp_values.location_auto = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);

    lv_obj_set_hidden(home_search, sp_values.location_auto);
    if(sp_values.location_auto) found_clear(PAGE_SETTINGS_SEARCH_HOME);
    location_show();
    sp_changed();
}

static void search_ready(lv_event_t * e)
{
    page_settings_search_t search = (page_settings_search_t)(lv_uintptr_t)lv_event_get_user_data(e);
    char                   name[NAME_CHARS_MAX * 4 + 1];

    if(!name_trimmed(lv_textarea_get_text(fields[search]), name, sizeof(name))) return;
    sp_search(search, name);
}

static void found_clicked(lv_event_t * e)
{
    uint32_t               value  = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);
    page_settings_search_t search = (page_settings_search_t)(value / PAGE_SETTINGS_FOUND_MAX);
    uint32_t               index  = value % PAGE_SETTINGS_FOUND_MAX;

    const char * name      = found[search][index].name;
    double       latitude  = found[search][index].latitude;
    double       longitude = found[search][index].longitude;

    if(search == PAGE_SETTINGS_SEARCH_HOME) {
        lv_strlcpy(sp_values.location_name, name, sizeof(sp_values.location_name));
        sp_values.latitude  = latitude;
        sp_values.longitude = longitude;
    }
    else {
        /*Once is enough.*/
        bool known = false;
        for(uint32_t i = 0; i < sp_values.place_count; i++) {
            known = known || (fabs(sp_values.places[i].latitude - latitude) < SAME_PLACE_DEGREES &&
                              fabs(sp_values.places[i].longitude - longitude) < SAME_PLACE_DEGREES);
        }

        if(!known && sp_values.place_count < SETTINGS_PLACES_MAX) {
            settings_place_t * place = &sp_values.places[sp_values.place_count++];
            lv_strlcpy(place->name, name, sizeof(place->name));
            place->latitude  = latitude;
            place->longitude = longitude;
        }
    }

    found_clear(search);
    lv_textarea_set_text(fields[search], "");
    location_show();
    places_show();
    sp_changed();
}

static void remove_clicked(lv_event_t * e)
{
    uint32_t index = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(index >= sp_values.place_count) return;

    for(uint32_t i = index; i + 1 < sp_values.place_count; i++) sp_values.places[i] = sp_values.places[i + 1];
    sp_values.place_count--;

    places_show();
    sp_changed();
}
