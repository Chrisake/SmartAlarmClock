/**
 * @file ui_confirm.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_confirm.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

#define PANEL_WIDTH   440
#define BUTTON_HEIGHT 48

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * button_create(lv_obj_t * parent, const char * text, lv_color_t color, lv_color_t pressed,
                                lv_color_t text_color, lv_event_cb_t clicked);
static void       backdrop_clicked(lv_event_t * e);
static void       cancel_clicked(lv_event_t * e);
static void       action_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static struct {
    lv_obj_t *      backdrop;   /**< NULL when closed */
    ui_confirm_cb_t confirmed;
    void *          user;
} confirm;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_confirm_open(const char * title, const char * message, const char * action, ui_confirm_cb_t confirmed,
                     void * user)
{
    ui_confirm_close();

    confirm.confirmed = confirmed;
    confirm.user      = user;

    /*Dims whatever is underneath and backs out when tapped.*/
    confirm.backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(confirm.backdrop);
    lv_obj_set_size(confirm.backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(confirm.backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(confirm.backdrop, LV_OPA_40, LV_PART_MAIN);
    lv_obj_add_event_cb(confirm.backdrop, backdrop_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * panel = ui_card_create(confirm.backdrop);
    lv_obj_set_size(panel, PANEL_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(panel, UI_PAD + 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, UI_GAP, LV_PART_MAIN);
    lv_obj_center(panel);

    lv_obj_t * title_label = ui_label_create(panel, title ? title : "", UI_FONT_LG, UI_COLOR_TEXT);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    if(message && message[0]) {
        lv_obj_t * message_label = ui_label_create(panel, message, UI_FONT_SM, UI_COLOR_TEXT_DIM);
        lv_obj_set_width(message_label, LV_PCT(100));
        lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    lv_obj_t * row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(row, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    button_create(row, "Cancel", UI_COLOR_CARD_ALT, UI_COLOR_BORDER, UI_COLOR_TEXT, cancel_clicked);
    button_create(row, action ? action : "OK", UI_COLOR_BAD, lv_color_darken(UI_COLOR_BAD, LV_OPA_20),
                  lv_color_white(), action_clicked);
}

void ui_confirm_close(void)
{
    if(!confirm.backdrop) return;

    /*Usually called from one of the panel's own buttons, which must not be
     *deleted from under their event.*/
    lv_obj_delete_async(confirm.backdrop);
    lv_memzero(&confirm, sizeof(confirm));
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * button_create(lv_obj_t * parent, const char * text, lv_color_t color, lv_color_t pressed,
                                lv_color_t text_color, lv_event_cb_t clicked)
{
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_set_size(button, 0, BUTTON_HEIGHT);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label = ui_label_create(button, text, UI_FONT_SM, text_color);
    lv_obj_center(label);
    return button;
}

static void backdrop_clicked(lv_event_t * e)
{
    /*Only a tap on the backdrop itself, not one that started on the panel.*/
    if(lv_event_get_target(e) == confirm.backdrop) ui_confirm_close();
}

static void cancel_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_confirm_close();
}

static void action_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    ui_confirm_cb_t confirmed = confirm.confirmed;
    void *          user      = confirm.user;

    /*Closed first, so whatever the callback does starts from a clean slate.*/
    ui_confirm_close();
    if(confirmed) confirmed(user);
}
