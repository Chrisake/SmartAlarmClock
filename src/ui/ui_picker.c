/**
 * @file ui_picker.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_picker.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

/** Panel width for one wheel; each further wheel adds WHEEL_EXTRA_WIDTH. */
#define PANEL_WIDTH        380
#define WHEEL_EXTRA_WIDTH  110

/** Rows a wheel shows at once; odd, so one sits in the middle. */
#define WHEEL_ROWS         5

/** Extra space between a wheel's rows, so each is a comfortable target. */
#define WHEEL_LINE_SPACE   22

#define CLOSE_SIZE         40
#define APPLY_HEIGHT       48

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void backdrop_clicked(lv_event_t * e);
static void close_clicked(lv_event_t * e);
static void apply_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static struct {
    lv_obj_t *           backdrop;   /**< NULL when closed */
    lv_obj_t *           wheels[UI_PICKER_COLUMNS_MAX];
    uint32_t             count;
    ui_picker_apply_cb_t apply;
    void *               user;
} picker;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_picker_open(const ui_picker_column_t columns[], uint32_t count, ui_picker_apply_cb_t apply, void * user)
{
    ui_picker_close();

    if(count == 0) return;
    if(count > UI_PICKER_COLUMNS_MAX) count = UI_PICKER_COLUMNS_MAX;

    picker.count = count;
    picker.apply = apply;
    picker.user  = user;

    /*Dims whatever is underneath, a page or another panel, and backs out of
     *the picker when tapped.*/
    picker.backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(picker.backdrop);
    lv_obj_set_size(picker.backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(picker.backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(picker.backdrop, LV_OPA_40, LV_PART_MAIN);
    lv_obj_add_event_cb(picker.backdrop, backdrop_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * panel = ui_card_create(picker.backdrop);
    lv_obj_set_size(panel, PANEL_WIDTH + (int32_t)(count - 1) * WHEEL_EXTRA_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(panel, UI_PAD + 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, UI_GAP, LV_PART_MAIN);
    lv_obj_center(panel);

    lv_obj_t * head = lv_obj_create(panel);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * close = lv_button_create(head);
    lv_obj_set_size(close, CLOSE_SIZE, CLOSE_SIZE);
    lv_obj_set_style_radius(close, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(close, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(close, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(close, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * close_label = ui_label_create(close, LV_SYMBOL_CLOSE, UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_center(close_label);

    lv_obj_t * row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_GAP / 2, LV_PART_MAIN);

    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t * wheel = lv_roller_create(row);
        lv_roller_set_options(wheel, columns[i].options,
                              columns[i].infinite ? LV_ROLLER_MODE_INFINITE : LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(wheel, WHEEL_ROWS);

        if(columns[i].width > 0) lv_obj_set_width(wheel, columns[i].width);
        else {
            lv_obj_set_width(wheel, 1);
            lv_obj_set_flex_grow(wheel, 1);
        }

        lv_obj_set_style_bg_opa(wheel, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(wheel, 0, LV_PART_MAIN);
        lv_obj_set_style_text_font(wheel, UI_FONT_MD, LV_PART_MAIN);
        lv_obj_set_style_text_color(wheel, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_line_space(wheel, WHEEL_LINE_SPACE, LV_PART_MAIN);
        lv_obj_set_style_text_align(wheel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        /*The band across the middle marks what Apply will take.*/
        lv_obj_set_style_bg_color(wheel, UI_COLOR_CARD_ALT, LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(wheel, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(wheel, UI_COLOR_TEXT, LV_PART_SELECTED);
        lv_obj_set_style_text_font(wheel, UI_FONT_MD, LV_PART_SELECTED);
        lv_obj_set_style_radius(wheel, 10, LV_PART_SELECTED);

        lv_roller_set_selected(wheel, columns[i].selected, LV_ANIM_OFF);
        picker.wheels[i] = wheel;
    }

    lv_obj_t * apply_button = lv_button_create(panel);
    lv_obj_set_size(apply_button, LV_PCT(100), APPLY_HEIGHT);
    lv_obj_set_style_radius(apply_button, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(apply_button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(apply_button, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(apply_button, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(apply_button, apply_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * apply_label = ui_label_create(apply_button, "Apply", UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_center(apply_label);
}

void ui_picker_close(void)
{
    if(!picker.backdrop) return;

    /*Usually called from one of the picker's own buttons, which must not be
     *deleted from under their event.*/
    lv_obj_delete_async(picker.backdrop);
    lv_memzero(&picker, sizeof(picker));
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void backdrop_clicked(lv_event_t * e)
{
    /*Only a tap on the backdrop itself, not one that started on the panel.*/
    if(lv_event_get_target(e) == picker.backdrop) ui_picker_close();
}

static void close_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_picker_close();
}

static void apply_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    uint32_t             selected[UI_PICKER_COLUMNS_MAX];
    uint32_t             count = picker.count;
    ui_picker_apply_cb_t apply = picker.apply;
    void *               user  = picker.user;

    for(uint32_t i = 0; i < count; i++) selected[i] = lv_roller_get_selected(picker.wheels[i]);

    /*Closed first, so whatever the callback does -- open another picker,
     *rebuild the UI -- starts from a clean slate.*/
    ui_picker_close();
    if(apply) apply(selected, count, user);
}
