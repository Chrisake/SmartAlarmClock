/**
 * @file ui_alarm_screen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_alarm_screen.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

/** The disc behind the bell, and the square its halo grows within. */
#define BELL_DISC_SIZE 112
#define BELL_BOX_SIZE  196

/** The halo: grows to this percent of the disc while fading out. */
#define HALO_GROW_PERCENT 170
#define HALO_MS           1600

/** The bell swings twice either side, by up to this many tenths of a degree, then rests. */
#define BELL_SWING    160
#define BELL_SWING_MS 650
#define BELL_REST_MS  900

#define BUTTON_WIDTH  300
#define BUTTON_HEIGHT 108

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void       bell_create(lv_obj_t * parent);
static lv_obj_t * button_create(lv_obj_t * parent, const char * text, const char * detail, bool primary);
static lv_obj_t * spacer_create(lv_obj_t * parent);
static void       halo_exec(void * obj, int32_t value);
static void       swing_exec(void * obj, int32_t value);
static void       snooze_clicked(lv_event_t * e);
static void       stop_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_obj_t * screen;
static lv_obj_t * time_label;
static lv_obj_t * meridiem_label;
static lv_obj_t * note_label;

static ui_alarm_screen_cb_t snooze_cb;
static ui_alarm_screen_cb_t stop_cb;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_alarm_screen_show(const ui_alarm_screen_info_t * info, ui_alarm_screen_cb_t snooze, ui_alarm_screen_cb_t stop)
{
    ui_alarm_screen_hide();

    snooze_cb = snooze;
    stop_cb   = stop;

    screen = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(screen, UI_GAP * 3, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(screen, UI_GAP * 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(screen, false);
    /*Swallows every tap that misses the buttons.*/
    lv_obj_set_clickable(screen, true);

    spacer_create(screen);
    bell_create(screen);

    lv_obj_t * clock = lv_obj_create(screen);
    lv_obj_remove_style_all(clock);
    lv_obj_set_size(clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(clock, 6, LV_PART_MAIN);
    lv_obj_set_style_margin_top(clock, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_clickable(clock, false);

    time_label     = ui_label_create(clock, "", UI_FONT_CLOCK, UI_COLOR_TEXT);
    meridiem_label = ui_label_create(clock, "", UI_FONT_MD, UI_COLOR_TEXT_DIM);

    lv_obj_t * name = ui_label_create(screen, info->name ? info->name : "", UI_FONT_XL, UI_COLOR_TEXT);
    lv_obj_set_width(name, LV_PCT(80));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ui_label_single_line(name, UI_FONT_XL);

    lv_obj_t * detail = ui_label_create(screen, info->detail ? info->detail : "", UI_FONT_MD, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(detail, LV_PCT(80));
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ui_label_single_line(detail, UI_FONT_MD);

    note_label = ui_label_create(screen, "", UI_FONT_SM, UI_COLOR_WARN);
    lv_obj_set_width(note_label, LV_PCT(80));
    lv_obj_set_style_text_align(note_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(note_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_hidden(note_label, true);

    spacer_create(screen);

    lv_obj_t * buttons = lv_obj_create(screen);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(buttons, UI_GAP * 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_clickable(buttons, false);

    if(info->snooze) {
        char minutes[16];
        lv_snprintf(minutes, sizeof(minutes), "%u min", (unsigned)info->snooze_minutes);

        lv_obj_t * snooze_button = button_create(buttons, "Snooze", minutes, false);
        lv_obj_add_event_cb(snooze_button, snooze_clicked, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t * stop_button = button_create(buttons, "Stop", NULL, true);
    lv_obj_add_event_cb(stop_button, stop_clicked, LV_EVENT_CLICKED, NULL);
}

void ui_alarm_screen_set_time(const char * time, const char * meridiem)
{
    if(!screen) return;

    lv_label_set_text(time_label, time);
    lv_label_set_text(meridiem_label, meridiem ? meridiem : "");
    lv_obj_set_hidden(meridiem_label, meridiem == NULL);
}

void ui_alarm_screen_set_note(const char * note)
{
    if(!screen) return;

    lv_label_set_text(note_label, note ? note : "");
    lv_obj_set_hidden(note_label, note == NULL);
}

void ui_alarm_screen_hide(void)
{
    if(!screen) return;

    /*Its own buttons call this, and must not be deleted from under themselves.*/
    lv_obj_delete_async(screen);
    screen = NULL;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** A bell on a disc that swings every second or so, with a halo rippling out behind it. */
static void bell_create(lv_obj_t * parent)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, BELL_BOX_SIZE, BELL_BOX_SIZE);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);

    lv_obj_t * halo = lv_obj_create(box);
    lv_obj_remove_style_all(halo);
    lv_obj_set_size(halo, BELL_DISC_SIZE, BELL_DISC_SIZE);
    lv_obj_center(halo);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(halo, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(halo, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(halo, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_clickable(halo, false);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, halo);
    lv_anim_set_exec_cb(&a, halo_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, HALO_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_obj_t * disc = lv_obj_create(box);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, BELL_DISC_SIZE, BELL_DISC_SIZE);
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(disc, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_clickable(disc, false);

    lv_obj_t * bell = ui_label_create(disc, LV_SYMBOL_BELL, UI_FONT_CLOCK, UI_COLOR_ACCENT);
    lv_obj_center(bell);
    /*Hung from the top, like a bell.*/
    lv_obj_set_style_transform_pivot_x(bell, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(bell, 0, LV_PART_MAIN);

    lv_anim_init(&a);
    lv_anim_set_var(&a, bell);
    lv_anim_set_exec_cb(&a, swing_exec);
    lv_anim_set_values(&a, 0, 720);
    lv_anim_set_duration(&a, BELL_SWING_MS);
    lv_anim_set_repeat_delay(&a, BELL_REST_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static lv_obj_t * button_create(lv_obj_t * parent, const char * text, const char * detail, bool primary)
{
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_set_size(button, BUTTON_WIDTH, BUTTON_HEIGHT);
    lv_obj_set_style_radius(button, UI_RADIUS * 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(button, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if(primary) {
        lv_obj_set_style_bg_color(button, UI_COLOR_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_darken(UI_COLOR_ACCENT, LV_OPA_20), LV_PART_MAIN | LV_STATE_PRESSED);
    }
    else {
        lv_obj_set_style_bg_color(button, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(button, UI_COLOR_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    }

    ui_label_create(button, text, UI_FONT_LG, primary ? lv_color_white() : UI_COLOR_TEXT);
    if(detail) ui_label_create(button, detail, UI_FONT_XS, UI_COLOR_TEXT_DIM);

    return button;
}

/** Takes up whatever height is left over, shared with the other spacers. */
static lv_obj_t * spacer_create(lv_obj_t * parent)
{
    lv_obj_t * spacer = lv_obj_create(parent);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_clickable(spacer, false);
    return spacer;
}

static void halo_exec(void * obj, int32_t value)
{
    int32_t scale = LV_SCALE_NONE + LV_SCALE_NONE * (HALO_GROW_PERCENT - 100) / 100 * value / 1000;

    lv_obj_set_style_transform_scale(obj, scale, LV_PART_MAIN);
    lv_obj_set_style_opa(obj, (lv_opa_t)(LV_OPA_40 * (1000 - value) / 1000), LV_PART_MAIN);
}

static void swing_exec(void * obj, int32_t value)
{
    int32_t angle = lv_trigo_sin((int16_t)(value % 360)) * BELL_SWING / LV_TRIGO_SIN_MAX;
    lv_obj_set_style_transform_rotation(obj, angle, LV_PART_MAIN);
}

static void snooze_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(snooze_cb) snooze_cb();
}

static void stop_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    if(stop_cb) stop_cb();
}
