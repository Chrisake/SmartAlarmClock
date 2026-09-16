/**
 * @file settings_wake.c
 *
 * Wake tab, in two cards. Left: how long the clock waits before it goes idle,
 * and face wake -- whether the camera wakes the screen for a face, how many
 * frames a second it looks at and how many in a row must hold one. Right:
 * movement wake -- whether the radar wakes the screen for someone coming to
 * the clock, how little movement that takes, and what it does once the room is
 * dark. Under them, whether the radar is there and answering.
 *
 * These rows were the Device tab's until movement wake gave them more than
 * that tab could hold without scrolling.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/settings_private.h"

/*********************
 *      DEFINES
 *********************/

/** The name beside a slider takes its own width, and the slider the rest. */
#define SLIDER_NAME_WIDTH 180

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void timeout_changed(lv_event_t * e);
static void face_wake_changed(lv_event_t * e);
static void face_fps_clicked(lv_event_t * e);
static void face_frames_changed(lv_event_t * e);
static void radar_wake_changed(lv_event_t * e);
static void radar_moved(lv_event_t * e);
static void radar_released(lv_event_t * e);
static void radar_dark_clicked(lv_event_t * e);
static void radar_show(void);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Ambient clock timeouts on offer, in seconds; 0 is never. */
static const uint16_t timeouts[] = {30, 60, 120, 240, 600, 0};
static const char     timeout_options[] = "30 seconds\n1 minute\n2 minutes\n4 minutes\n10 minutes\nNever";

static const char * const fps_options[] = {"5", "10"};
static const char         frames_options[] = "3\n4\n5\n6\n7";

/*In settings_radar_dark_t order.*/
static const char * const dark_options[] = {"On", "Reduced", "Off"};

static lv_obj_t * timeout_row;
static lv_obj_t * timeout_dropdown;
static lv_obj_t * face_switch;
static lv_obj_t * face_fps_row;
static lv_obj_t * face_fps_segmented;
static lv_obj_t * face_frames_row;
static lv_obj_t * face_frames_dropdown;

static lv_obj_t * radar_switch;
static lv_obj_t * radar_row;
static lv_obj_t * radar_slider;
static lv_obj_t * radar_value;
static lv_obj_t * radar_dark_row;
static lv_obj_t * radar_dark_segmented;
static lv_obj_t * radar_status;

/** What the feed last said, so the status is right however the tab is rebuilt. */
static page_settings_radar_t radar_state = PAGE_SETTINGS_RADAR_UNKNOWN;
static bool                  radar_room_dark;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sp_wake_create(lv_obj_t * tab)
{
    lv_obj_t * columns = sp_box_create(tab);
    lv_obj_set_width(columns, LV_PCT(100));
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(columns, UI_GAP, LV_PART_MAIN);

    /*Going idle, and the camera.*/
    lv_obj_t * face = sp_card_create(columns);
    lv_obj_set_width(face, 0);
    lv_obj_set_flex_grow(face, 1);
    lv_obj_set_style_pad_row(face, UI_GAP / 2, LV_PART_MAIN);

    timeout_row      = sp_row_create(face, "Ambient clock after");
    timeout_dropdown = sp_dropdown_create(timeout_row, timeout_options, timeout_changed);
    lv_obj_set_width(timeout_dropdown, 150);

    lv_obj_t * row = sp_row_create(face, "Wake on face");
    face_switch    = sp_switch_create(row, face_wake_changed);

    face_fps_row       = sp_row_create(face, "Frames a second");
    face_fps_segmented = sp_segmented_create(face_fps_row, fps_options, 2, face_fps_clicked);

    face_frames_row      = sp_row_create(face, "Frames in a row");
    face_frames_dropdown = sp_dropdown_create(face_frames_row, frames_options, face_frames_changed);
    lv_obj_set_width(face_frames_dropdown, 90);

    /*The radar.*/
    lv_obj_t * movement = sp_card_create(columns);
    lv_obj_set_width(movement, 0);
    lv_obj_set_flex_grow(movement, 1);
    lv_obj_set_style_pad_row(movement, UI_GAP / 2, LV_PART_MAIN);

    row          = sp_row_create(movement, "Wake on movement");
    radar_switch = sp_switch_create(row, radar_wake_changed);

    radar_row = sp_row_create(movement, "Sensitivity");

    lv_obj_t * name = lv_obj_get_child(radar_row, 0);
    lv_obj_set_flex_grow(name, 0);
    lv_obj_set_width(name, SLIDER_NAME_WIDTH);
    lv_obj_set_style_pad_column(radar_row, UI_SLIDER_KNOB_OVERHANG, LV_PART_MAIN);

    radar_slider = ui_slider_create(radar_row, UI_COLOR_ACCENT);
    lv_obj_set_flex_grow(radar_slider, 1);
    lv_slider_set_range(radar_slider, SETTINGS_RADAR_SENSITIVITY_MIN, SETTINGS_RADAR_SENSITIVITY_MAX);
    lv_obj_add_event_cb(radar_slider, radar_moved, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(radar_slider, radar_released, LV_EVENT_RELEASED, NULL);

    radar_value = ui_label_create(radar_row, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(radar_value, 44);
    lv_obj_set_style_text_align(radar_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    /*What it does once the light sensor says the room is dark: nothing, less
     *sensitive, or nothing wakes the screen at all.*/
    radar_dark_row       = sp_row_create(movement, "In the dark");
    radar_dark_segmented = sp_segmented_create(radar_dark_row, dark_options, SETTINGS_RADAR_DARK_COUNT,
                                               radar_dark_clicked);

    radar_status = ui_label_create(movement, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(radar_status, LV_PCT(100));
    lv_label_set_long_mode(radar_status, LV_LABEL_LONG_MODE_WRAP);
}

void sp_wake_values(void)
{
    /*An unlisted timeout, from a hand-edited file, shows as the nearest longer one.*/
    uint32_t selected = sizeof(timeouts) / sizeof(timeouts[0]) - 1;
    for(uint32_t i = 0; i + 1 < sizeof(timeouts) / sizeof(timeouts[0]); i++) {
        if(sp_values.ambient_timeout != 0 && sp_values.ambient_timeout <= timeouts[i]) {
            selected = i;
            break;
        }
    }
    lv_dropdown_set_selected(timeout_dropdown, selected);
    sp_row_rename(timeout_row, sp_values.always_on ? "Ambient clock after" : "Screen off after");

    sp_switch_set(face_switch, sp_values.face_wake);
    sp_segmented_select(face_fps_segmented, sp_values.face_wake_fps >= SETTINGS_FACE_WAKE_FPS_HIGH ? 1 : 0);
    lv_dropdown_set_selected(face_frames_dropdown,
                             (uint32_t)LV_CLAMP(SETTINGS_FACE_WAKE_FRAMES_MIN, sp_values.face_wake_frames,
                                                SETTINGS_FACE_WAKE_FRAMES_MAX) - SETTINGS_FACE_WAKE_FRAMES_MIN);
    lv_obj_set_hidden(face_fps_row, !sp_values.face_wake);
    lv_obj_set_hidden(face_frames_row, !sp_values.face_wake);

    sp_switch_set(radar_switch, sp_values.radar_wake);
    if(!lv_slider_is_dragged(radar_slider)) {
        lv_slider_set_value(radar_slider, sp_values.radar_sensitivity, LV_ANIM_OFF);
        lv_label_set_text_fmt(radar_value, "%d%%", (int)sp_values.radar_sensitivity);
    }
    sp_segmented_select(radar_dark_segmented, (uint32_t)sp_values.radar_dark);

    lv_obj_set_hidden(radar_row, !sp_values.radar_wake);
    lv_obj_set_hidden(radar_dark_row, !sp_values.radar_wake);

    radar_show();
}

void sp_wake_radar(page_settings_radar_t state, bool dark)
{
    radar_state     = state;
    radar_room_dark = dark;

    /*Told before the tab is built, on the way back from a rebuild.*/
    if(radar_status) radar_show();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Whether the sensor is there, and whether the dark setting is in force now. */
static void radar_show(void)
{
    if(!sp_values.radar_wake || radar_state == PAGE_SETTINGS_RADAR_UNKNOWN) {
        lv_obj_set_hidden(radar_status, true);
        return;
    }

    lv_obj_set_hidden(radar_status, false);

    if(radar_state == PAGE_SETTINGS_RADAR_MISSING) {
        lv_label_set_text(radar_status, LV_SYMBOL_WARNING " No movement sensor found");
        lv_obj_set_style_text_color(radar_status, UI_COLOR_BAD, LV_PART_MAIN);
        return;
    }

    lv_label_set_text(radar_status, radar_room_dark ? LV_SYMBOL_OK " Sensor ready; the room is dark just now"
                                                    : LV_SYMBOL_OK " Sensor ready");
    lv_obj_set_style_text_color(radar_status, radar_room_dark ? UI_COLOR_TEXT_DIM : UI_COLOR_GOOD, LV_PART_MAIN);
}

static void timeout_changed(lv_event_t * e)
{
    uint32_t i = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if(i >= sizeof(timeouts) / sizeof(timeouts[0])) return;

    sp_values.ambient_timeout = timeouts[i];
    sp_changed();
}

static void face_wake_changed(lv_event_t * e)
{
    sp_values.face_wake = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    sp_wake_values();
    sp_changed();
}

static void face_fps_clicked(lv_event_t * e)
{
    bool high = (lv_uintptr_t)lv_event_get_user_data(e) == 1;

    sp_values.face_wake_fps = high ? SETTINGS_FACE_WAKE_FPS_HIGH : SETTINGS_FACE_WAKE_FPS_LOW;
    sp_segmented_select(face_fps_segmented, high ? 1 : 0);
    sp_changed();
}

static void face_frames_changed(lv_event_t * e)
{
    uint32_t selected = lv_dropdown_get_selected(lv_event_get_target_obj(e));

    sp_values.face_wake_frames = (uint8_t)LV_MIN(SETTINGS_FACE_WAKE_FRAMES_MIN + selected,
                                                 SETTINGS_FACE_WAKE_FRAMES_MAX);
    sp_changed();
}

static void radar_wake_changed(lv_event_t * e)
{
    sp_values.radar_wake = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    sp_wake_values();
    sp_changed();
}

static void radar_moved(lv_event_t * e)
{
    LV_UNUSED(e);
    lv_label_set_text_fmt(radar_value, "%d%%", (int)lv_slider_get_value(radar_slider));
}

static void radar_released(lv_event_t * e)
{
    LV_UNUSED(e);

    /*On release, so a slide is stored once rather than on every step.*/
    sp_values.radar_sensitivity = (uint8_t)lv_slider_get_value(radar_slider);
    sp_changed();
}

static void radar_dark_clicked(lv_event_t * e)
{
    settings_radar_dark_t dark = (settings_radar_dark_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(dark == sp_values.radar_dark) return;

    sp_values.radar_dark = dark;
    sp_segmented_select(radar_dark_segmented, (uint32_t)dark);
    sp_changed();
}
