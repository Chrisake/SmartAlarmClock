/**
 * @file settings_device.c
 *
 * Device tab, in two cards. Display: the backlight, awake and on the ambient
 * clock, then theme, accent colour and how soon the ambient clock takes over.
 * General: language and temperature unit. Every control takes effect as soon
 * as it is changed.
 *
 * Brightness is the backlight's alone -- nothing on screen is drawn any
 * differently at any level. Each brightness has an automatic switch; while it
 * is on, the slider below it sets how strongly the light sensor moves the
 * backlight, and the slider says so.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/settings_private.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define SWATCH_SIZE 28

/**********************
 *      TYPEDEFS
 **********************/

/** An automatic switch over the slider it changes the meaning of. */
typedef struct {
    lv_obj_t *   auto_switch;
    lv_obj_t *   row;
    lv_obj_t *   slider;
    lv_obj_t *   value;
    const char * level_name;
    const char * sensitivity_name;
} brightness_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void brightness_create(brightness_t * b, lv_obj_t * parent, const char * auto_name,
                              const char * level_name, const char * sensitivity_name, bool idle);
static void brightness_show(brightness_t * b, bool automatic, uint8_t level);

static void auto_changed(lv_event_t * e);
static void level_moved(lv_event_t * e);
static void level_released(lv_event_t * e);
static void theme_clicked(lv_event_t * e);
static void accent_clicked(lv_event_t * e);
static void timeout_changed(lv_event_t * e);
static void language_changed(lv_event_t * e);
static void unit_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Ambient clock timeouts on offer, in seconds; 0 is never. */
static const uint16_t timeouts[] = {30, 60, 120, 240, 600, 0};
static const char     timeout_options[] = "30 seconds\n1 minute\n2 minutes\n4 minutes\n10 minutes\nNever";

static const char * const theme_options[] = {"Dark", "Light"};
static const char * const unit_options[]  = {UI_DEG "C", UI_DEG "F"};

/** [0] awake, [1] idle -- the index is the event user data. */
static brightness_t brightness[2];

static lv_obj_t * theme_segmented;
static lv_obj_t * swatches[SETTINGS_ACCENT_COUNT];
static lv_obj_t * timeout_dropdown;
static lv_obj_t * language_dropdown;
static lv_obj_t * unit_segmented;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sp_device_create(lv_obj_t * tab)
{
    lv_obj_t * columns = sp_box_create(tab);
    lv_obj_set_width(columns, LV_PCT(100));
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(columns, UI_GAP, LV_PART_MAIN);

    /*Display.*/
    lv_obj_t * display = sp_card_create(columns);
    lv_obj_set_width(display, 0);
    lv_obj_set_flex_grow(display, 3);
    lv_obj_set_style_pad_row(display, UI_GAP / 2, LV_PART_MAIN);

    brightness_create(&brightness[0], display, "Automatic brightness", "Brightness", "Sensitivity", false);
    brightness_create(&brightness[1], display, "Automatic idle brightness", "Idle brightness",
                      "Idle sensitivity", true);

    lv_obj_t * row = sp_row_create(display, "Theme");
    theme_segmented = sp_segmented_create(row, theme_options, 2, theme_clicked);

    row = sp_row_create(display, "Accent colour");
    lv_obj_t * palette = sp_box_create(row);
    lv_obj_set_flex_flow(palette, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(palette, 6, LV_PART_MAIN);
    /*Room for the ring around the chosen swatch.*/
    lv_obj_set_style_pad_all(palette, 4, LV_PART_MAIN);

    for(uint32_t i = 0; i < SETTINGS_ACCENT_COUNT; i++) {
        lv_obj_t * swatch = lv_obj_create(palette);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_size(swatch, SWATCH_SIZE, SWATCH_SIZE);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(swatch, ui_accent_color(i), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_outline_color(swatch, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(swatch, 2, LV_PART_MAIN);
        lv_obj_set_ext_click_area(swatch, 3);
        lv_obj_set_clickable(swatch, true);
        lv_obj_add_event_cb(swatch, accent_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
        swatches[i] = swatch;
    }

    row = sp_row_create(display, "Ambient clock after");
    timeout_dropdown = sp_dropdown_create(row, timeout_options, timeout_changed);

    /*General.*/
    lv_obj_t * general = sp_card_create(columns);
    lv_obj_set_width(general, 0);
    lv_obj_set_flex_grow(general, 2);
    lv_obj_set_style_pad_row(general, UI_GAP / 2, LV_PART_MAIN);

    row = sp_row_create(general, "Language");
    /*English is all there is so far; the list is where the rest will go.*/
    language_dropdown = sp_dropdown_create(row, "English", language_changed);
    lv_obj_set_width(language_dropdown, 150);

    row = sp_row_create(general, "Temperature unit");
    unit_segmented = sp_segmented_create(row, unit_options, 2, unit_clicked);
}

void sp_device_values(void)
{
    brightness_show(&brightness[0], sp_values.brightness_auto, sp_values.brightness);
    brightness_show(&brightness[1], sp_values.idle_brightness_auto, sp_values.idle_brightness);

    sp_segmented_select(theme_segmented, sp_values.theme == SETTINGS_THEME_LIGHT ? 1 : 0);

    for(uint32_t i = 0; i < SETTINGS_ACCENT_COUNT; i++) {
        lv_obj_set_style_outline_width(swatches[i], i == sp_values.accent ? 2 : 0, LV_PART_MAIN);
    }

    /*An unlisted timeout, from a hand-edited file, shows as the nearest longer one.*/
    uint32_t selected = sizeof(timeouts) / sizeof(timeouts[0]) - 1;
    for(uint32_t i = 0; i + 1 < sizeof(timeouts) / sizeof(timeouts[0]); i++) {
        if(sp_values.ambient_timeout != 0 && sp_values.ambient_timeout <= timeouts[i]) {
            selected = i;
            break;
        }
    }
    lv_dropdown_set_selected(timeout_dropdown, selected);

    lv_dropdown_set_selected(language_dropdown, 0);
    sp_segmented_select(unit_segmented, sp_values.fahrenheit ? 1 : 0);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void brightness_create(brightness_t * b, lv_obj_t * parent, const char * auto_name,
                              const char * level_name, const char * sensitivity_name, bool idle)
{
    lv_uintptr_t which = idle ? 1 : 0;

    b->level_name       = level_name;
    b->sensitivity_name = sensitivity_name;

    lv_obj_t * row = sp_row_create(parent, auto_name);
    b->auto_switch = sp_switch_create(row, auto_changed);
    lv_obj_set_user_data(b->auto_switch, (void *)which);

    b->row = sp_row_create(parent, level_name);
    /*The name takes only its own width here; the slider has the rest.*/
    lv_obj_t * name = lv_obj_get_child(b->row, 0);
    lv_obj_set_flex_grow(name, 0);
    lv_obj_set_width(name, 150);
    lv_obj_set_style_pad_column(b->row, UI_SLIDER_KNOB_OVERHANG, LV_PART_MAIN);

    b->slider = ui_slider_create(b->row, UI_COLOR_SUN);
    lv_obj_set_flex_grow(b->slider, 1);
    lv_slider_set_range(b->slider, SETTINGS_BRIGHTNESS_MIN, 100);
    lv_obj_add_event_cb(b->slider, level_moved, LV_EVENT_VALUE_CHANGED, (void *)which);
    lv_obj_add_event_cb(b->slider, level_released, LV_EVENT_RELEASED, (void *)which);

    b->value = ui_label_create(b->row, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(b->value, 44);
    lv_obj_set_style_text_align(b->value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
}

/** Automatic turns the level into a sensitivity; the row's name follows. */
static void brightness_show(brightness_t * b, bool automatic, uint8_t level)
{
    sp_switch_set(b->auto_switch, automatic);
    sp_row_rename(b->row, automatic ? b->sensitivity_name : b->level_name);

    if(!lv_slider_is_dragged(b->slider)) {
        lv_slider_set_value(b->slider, level, LV_ANIM_OFF);
        lv_label_set_text_fmt(b->value, "%d%%", (int)level);
    }
}

static void auto_changed(lv_event_t * e)
{
    lv_obj_t * sw   = lv_event_get_target_obj(e);
    bool       idle = (lv_uintptr_t)lv_obj_get_user_data(sw) == 1;
    bool       on   = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if(idle) sp_values.idle_brightness_auto = on;
    else     sp_values.brightness_auto = on;

    brightness_show(&brightness[idle ? 1 : 0], on, idle ? sp_values.idle_brightness : sp_values.brightness);
    sp_changed();
}

static void level_moved(lv_event_t * e)
{
    brightness_t * b = &brightness[(lv_uintptr_t)lv_event_get_user_data(e)];
    lv_label_set_text_fmt(b->value, "%d%%", (int)lv_slider_get_value(b->slider));
}

static void level_released(lv_event_t * e)
{
    bool    idle  = (lv_uintptr_t)lv_event_get_user_data(e) == 1;
    uint8_t level = (uint8_t)lv_slider_get_value(brightness[idle ? 1 : 0].slider);

    /*On release, so a slide is stored once rather than on every step.*/
    if(idle) sp_values.idle_brightness = level;
    else     sp_values.brightness = level;
    sp_changed();
}

static void theme_clicked(lv_event_t * e)
{
    settings_theme_t theme = (settings_theme_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(theme == sp_values.theme) return;

    sp_values.theme = theme;
    sp_segmented_select(theme_segmented, theme == SETTINGS_THEME_LIGHT ? 1 : 0);
    /*The feed rebuilds the UI in the new colours once this event is over.*/
    sp_changed();
}

static void accent_clicked(lv_event_t * e)
{
    uint8_t accent = (uint8_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(accent == sp_values.accent) return;

    sp_values.accent = accent;
    sp_changed();
}

static void timeout_changed(lv_event_t * e)
{
    uint32_t i = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if(i >= sizeof(timeouts) / sizeof(timeouts[0])) return;

    sp_values.ambient_timeout = timeouts[i];
    sp_changed();
}

static void language_changed(lv_event_t * e)
{
    LV_UNUSED(e);
    lv_strlcpy(sp_values.language, "en", sizeof(sp_values.language));
    sp_changed();
}

static void unit_clicked(lv_event_t * e)
{
    sp_values.fahrenheit = (lv_uintptr_t)lv_event_get_user_data(e) == 1;
    sp_segmented_select(unit_segmented, sp_values.fahrenheit ? 1 : 0);
    sp_changed();
}
