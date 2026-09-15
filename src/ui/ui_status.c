/**
 * @file ui_status.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_status.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

#define SPINNER_SIZE 44
#define SPINNER_ARC  4

/** Covers shorter than this line their contents up in a row. */
#define COMPACT_HEIGHT 150

/** Side of the refresh glyph and its spinner: one line of UI_FONT_XS. */
#define REFRESH_SIZE 16

/** Taken with it: the glyph plus this all round makes a UI_TOUCH_MIN target. */
#define REFRESH_REACH ((UI_TOUCH_MIN - REFRESH_SIZE) / 2)

#define NOTICE_SHOW_MS   6000
#define NOTICE_FADE_MS   180
#define NOTICE_TEXT_MAX  600

/**********************
 *      TYPEDEFS
 **********************/

/** The cover's children, in creation order. */
typedef enum {
    COVER_SPINNER,
    COVER_ICON,
    COVER_MESSAGE,
    COVER_RETRY,
} cover_part_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void parent_resized(lv_event_t * e);
static void cover_fit(lv_obj_t * cover);
static void spinner_style(lv_obj_t * spinner, int32_t size, int32_t arc);

static void notice_clicked(lv_event_t * e);
static void notice_timer_cb(lv_timer_t * timer);
static void notice_hide(void);
static void opa_set(void * obj, int32_t value);
static void notice_faded(lv_anim_t * a);

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_obj_t *   notice;
static lv_timer_t * notice_timer;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * ui_status_create(lv_obj_t * parent, lv_color_t surface, lv_event_cb_t retry, void * user)
{
    lv_obj_t * cover = lv_obj_create(parent);
    lv_obj_remove_style_all(cover);
    lv_obj_set_floating(cover, true);
    lv_obj_set_style_bg_color(cover, surface, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cover, UI_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_row(cover, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(cover, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cover, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cover, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(cover, false);

    /*Takes the taps meant for the empty widgets it hides, and hands them to
     *the parent instead.*/
    lv_obj_set_clickable(cover, true);
    lv_obj_set_event_bubble(cover, true);

    /*Whether it has a Try again button, for ui_status_set().*/
    lv_obj_set_user_data(cover, (void *)(lv_uintptr_t)(retry != NULL));

    lv_obj_t * spinner = lv_spinner_create(cover);
    spinner_style(spinner, SPINNER_SIZE, SPINNER_ARC);

    ui_label_create(cover, LV_SYMBOL_WARNING, UI_FONT_LG, UI_COLOR_WARN);

    lv_obj_t * message = ui_label_create(cover, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(message, LV_PCT(100));
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t * button = lv_button_create(cover);
    lv_obj_set_height(button, UI_TOUCH_MIN - 8);
    lv_obj_set_style_pad_hor(button, UI_GAP * 2, LV_PART_MAIN);
    lv_obj_set_style_radius(button, UI_RADIUS - 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
    if(retry) lv_obj_add_event_cb(button, retry, LV_EVENT_CLICKED, user);

    lv_obj_t * label = ui_label_create(button, LV_SYMBOL_REFRESH "  Try again", UI_FONT_SM, UI_COLOR_ACCENT);
    lv_obj_center(label);

    lv_obj_add_event_cb(parent, parent_resized, LV_EVENT_SIZE_CHANGED, cover);
    cover_fit(cover);

    ui_status_set(cover, UI_STATUS_LOADING, NULL);
    return cover;
}

void ui_status_set(lv_obj_t * status, ui_status_state_t state, const char * message)
{
    if(!status) return;

    bool failed = state == UI_STATUS_FAILED;
    bool retry  = lv_obj_get_user_data(status) != NULL;

    lv_obj_t * label = lv_obj_get_child(status, COVER_MESSAGE);
    lv_label_set_text(label, message ? message : "");

    lv_obj_set_hidden(status, state == UI_STATUS_READY);
    lv_obj_set_hidden(lv_obj_get_child(status, COVER_SPINNER), state != UI_STATUS_LOADING);
    lv_obj_set_hidden(lv_obj_get_child(status, COVER_ICON), !failed);
    lv_obj_set_hidden(label, !failed || !message || !message[0]);
    lv_obj_set_hidden(lv_obj_get_child(status, COVER_RETRY), !failed || !retry);

    /*Above anything the parent gained after it.*/
    lv_obj_move_to_index(status, -1);
}

lv_obj_t * ui_refresh_create(lv_obj_t * parent, lv_event_cb_t clicked, void * user)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, REFRESH_SIZE, REFRESH_SIZE);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);

    lv_obj_t * button = lv_obj_create(box);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, REFRESH_SIZE, REFRESH_SIZE);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_clickable(button, true);
    lv_obj_set_ext_click_area(button, REFRESH_REACH);
    lv_obj_set_style_text_color(button, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user);

    lv_obj_t * glyph = lv_label_create(button);
    lv_label_set_text(glyph, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(glyph, UI_FONT_XS, LV_PART_MAIN);
    lv_obj_center(glyph);

    lv_obj_t * spinner = lv_spinner_create(box);
    spinner_style(spinner, REFRESH_SIZE, 2);
    lv_obj_set_hidden(spinner, true);

    return box;
}

void ui_refresh_set_busy(lv_obj_t * refresh, bool busy)
{
    if(!refresh) return;

    lv_obj_set_hidden(lv_obj_get_child(refresh, 0), busy);
    lv_obj_set_hidden(lv_obj_get_child(refresh, 1), !busy);
}

void ui_notice_show(ui_notice_level_t level, const char * text)
{
    /*Built afresh every time, so it is always in the current palette.*/
    if(notice) {
        lv_anim_delete(notice, opa_set);
        lv_obj_delete(notice);
    }

    notice = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(notice);
    lv_obj_set_size(notice, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(notice, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(notice, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(notice, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(notice, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(notice, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(notice, 32, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(notice, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(notice, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(notice, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(notice, UI_GAP * 2, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(notice, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(notice, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(notice, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(notice, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(notice, false);
    lv_obj_set_clickable(notice, true);
    lv_obj_add_event_cb(notice, notice_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_align(notice, LV_ALIGN_TOP_MID, 0, UI_GAP);

    bool error = level == UI_NOTICE_ERROR;
    ui_label_create(notice, error ? LV_SYMBOL_WARNING : LV_SYMBOL_OK, UI_FONT_SM,
                    error ? UI_COLOR_BAD : UI_COLOR_ACCENT);

    lv_obj_t * label = ui_label_create(notice, text, UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_style_max_width(label, NOTICE_TEXT_MAX, LV_PART_MAIN);
    ui_label_single_line(label, UI_FONT_SM);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, notice);
    lv_anim_set_exec_cb(&a, opa_set);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, NOTICE_FADE_MS);
    lv_anim_start(&a);

    if(!notice_timer) notice_timer = lv_timer_create(notice_timer_cb, NOTICE_SHOW_MS, NULL);
    lv_timer_reset(notice_timer);
    lv_timer_resume(notice_timer);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void parent_resized(lv_event_t * e)
{
    cover_fit(lv_event_get_user_data(e));
}

/**
 * Fill the parent inside its border. A child's position counts from inside
 * the parent's padding, so the cover steps back over the padding.
 */
static void cover_fit(lv_obj_t * cover)
{
    lv_obj_t * parent = lv_obj_get_parent(cover);
    int32_t    border = lv_obj_get_style_border_width(parent, LV_PART_MAIN);
    int32_t    radius = lv_obj_get_style_radius(parent, LV_PART_MAIN);
    int32_t    height = LV_MAX(0, lv_obj_get_height(parent) - 2 * border);

    lv_obj_set_pos(cover, -lv_obj_get_style_pad_left(parent, LV_PART_MAIN),
                   -lv_obj_get_style_pad_top(parent, LV_PART_MAIN));
    lv_obj_set_size(cover, LV_MAX(0, lv_obj_get_width(parent) - 2 * border), height);
    lv_obj_set_style_radius(cover, LV_MAX(0, radius - border), LV_PART_MAIN);

    /*A strip too short to stack the warning, the message and the button lines
     *them up instead, the message on one line.*/
    bool       compact = height > 0 && height < COMPACT_HEIGHT;
    lv_obj_t * message = lv_obj_get_child(cover, COVER_MESSAGE);

    lv_obj_set_flex_flow(cover, compact ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_ver(cover, compact ? 0 : UI_PAD, LV_PART_MAIN);

    if(compact) {
        lv_obj_set_width(message, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(message, LV_PCT(50), LV_PART_MAIN);
        ui_label_single_line(message, UI_FONT_SM);
    }
    else {
        lv_obj_set_size(message, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(message, LV_COORD_MAX, LV_PART_MAIN);
        lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    }
}

static void spinner_style(lv_obj_t * spinner, int32_t size, int32_t arc)
{
    lv_obj_set_size(spinner, size, size);
    lv_obj_set_style_arc_width(spinner, arc, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, arc, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_clickable(spinner, false);
}

static void notice_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    notice_hide();
}

static void notice_timer_cb(lv_timer_t * timer)
{
    lv_timer_pause(timer);
    notice_hide();
}

static void notice_hide(void)
{
    if(!notice) return;

    if(notice_timer) lv_timer_pause(notice_timer);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, notice);
    lv_anim_set_exec_cb(&a, opa_set);
    lv_anim_set_values(&a, lv_obj_get_style_opa(notice, LV_PART_MAIN), LV_OPA_TRANSP);
    lv_anim_set_duration(&a, NOTICE_FADE_MS);
    lv_anim_set_completed_cb(&a, notice_faded);
    lv_anim_start(&a);
}

static void opa_set(void * obj, int32_t value)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)value, LV_PART_MAIN);
}

static void notice_faded(lv_anim_t * a)
{
    LV_UNUSED(a);

    /*Asynchronously: this runs inside the notice's own animation. A notice
     *shown meanwhile deleted this animation along with the old notice, so
     *`notice` here is still the one that faded.*/
    if(!notice) return;
    lv_obj_delete_async(notice);
    notice = NULL;
}
