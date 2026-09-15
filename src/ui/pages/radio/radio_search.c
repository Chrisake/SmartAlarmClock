/**
 * @file radio_search.c
 *
 * The add-station dialog: choose what to search by, type, and add stations
 * from the results.
 *
 * It opens over the dimmed page with the keyboard already up, so the first
 * thing to do is type. Enter searches and puts the keyboard away to give the
 * results the whole height; tapping the field brings it back. Each result has
 * a + that saves it, which becomes a tick once it is saved -- including for
 * stations that were saved before the search.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/radio/radio_private.h"
#include "ui/ui_keyboard.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

#define PANEL_WIDTH         780
#define KEYBOARD_HEIGHT_PCT 42
#define QUERY_MAX           64
#define HEAD_HEIGHT         44
#define ADD_SIZE            44

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void keyboard_show(bool show);
static void field_select(page_radio_search_field_t field);
static void search_run(void);

static void close_clicked(lv_event_t * e);
static void segment_clicked(lv_event_t * e);
static void field_clicked(lv_event_t * e);
static void keyboard_ready(lv_event_t * e);
static void keyboard_cancel(lv_event_t * e);
static void add_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const char * const segment_names[PAGE_RADIO_SEARCH_COUNT] = {"Name", "Tag", "Country"};

static const char * const field_hints[PAGE_RADIO_SEARCH_COUNT] = {
    "Station name", "Genre or tag, e.g. jazz", "Country, e.g. Greece",
};

static lv_obj_t * backdrop;   /**< NULL while closed */
static lv_obj_t * panel;
static lv_obj_t * field;
static lv_obj_t * status_label;
static lv_obj_t * results_list;
static lv_obj_t * keyboard;
static lv_obj_t * segments[PAGE_RADIO_SEARCH_COUNT];
static lv_obj_t * result_items[PAGE_RADIO_RESULT_MAX];
static bool       result_added[PAGE_RADIO_RESULT_MAX];

/** Kept across openings, so the dialog comes back searching the same way. */
static page_radio_search_field_t search_field = PAGE_RADIO_SEARCH_NAME;

static page_radio_search_cb_t search_cb;
static page_radio_add_cb_t    add_cb;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void radio_search_open(void)
{
    if(backdrop) return;

    lv_memzero(result_items, sizeof(result_items));
    lv_memzero(result_added, sizeof(result_added));

    /*Dims the page and keeps taps from reaching it. A stray tap outside does
     *not close the dialog: that would throw away what was typed.*/
    backdrop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(backdrop, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(backdrop, false);

    panel = ui_card_create(backdrop);
    lv_obj_set_width(panel, PANEL_WIDTH);
    lv_obj_set_style_pad_row(panel, UI_GAP, LV_PART_MAIN);
    /*The panel's right padding moves into its children, and into the results
     *becomes the gutter their scroll bar runs in, clear of the + buttons.*/
    lv_obj_set_style_pad_right(panel, 0, LV_PART_MAIN);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, UI_GAP);

    /*[Name|Tag|Country] [field ..........] [x]*/
    lv_obj_t * head = lv_obj_create(panel);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(head, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(head, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(head, UI_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_column(head, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(head, false);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * segmented = lv_obj_create(head);
    lv_obj_set_size(segmented, LV_SIZE_CONTENT, HEAD_HEIGHT);
    lv_obj_set_style_bg_color(segmented, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(segmented, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(segmented, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(segmented, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(segmented, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(segmented, 4, LV_PART_MAIN);
    lv_obj_set_scrollable(segmented, false);
    lv_obj_set_flex_flow(segmented, LV_FLEX_FLOW_ROW);

    for(uint32_t i = 0; i < PAGE_RADIO_SEARCH_COUNT; i++) {
        lv_obj_t * btn = lv_button_create(segmented);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_style_pad_hor(btn, 14, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);
        radio_press_grow_off(btn);
        lv_obj_add_event_cb(btn, segment_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, segment_names[i]);
        lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
        lv_obj_center(label);

        segments[i] = btn;
    }

    field = lv_textarea_create(head);
    lv_obj_set_height(field, HEAD_HEIGHT);
    lv_obj_set_width(field, 0);
    lv_obj_set_flex_grow(field, 1);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_max_length(field, QUERY_MAX);
    lv_obj_set_style_text_font(field, UI_FONT_SM, LV_PART_MAIN);
    lv_obj_set_style_text_color(field, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_color(field, UI_COLOR_TEXT_DIM, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(field, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(field, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(field, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(field, 11, LV_PART_MAIN);
    lv_obj_add_event_cb(field, field_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * close = radio_round_button_create(head, LV_SYMBOL_CLOSE, 40);
    lv_obj_set_ext_click_area(close, 8);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);

    status_label = ui_label_create(panel, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(status_label, LV_PCT(100));
    lv_obj_set_style_pad_right(status_label, UI_PAD, LV_PART_MAIN);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_hidden(status_label, true);

    results_list = lv_obj_create(panel);
    lv_obj_set_width(results_list, LV_PCT(100));
    lv_obj_set_flex_grow(results_list, 1);
    lv_obj_set_style_bg_opa(results_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(results_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(results_list, 0, LV_PART_MAIN);
    ui_scrollbar_gutter(results_list, UI_PAD);
    lv_obj_set_style_pad_row(results_list, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(results_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(results_list, LV_DIR_VER);

    keyboard = ui_keyboard_create(backdrop);
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(KEYBOARD_HEIGHT_PCT));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(keyboard, keyboard_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_cancel, LV_EVENT_CANCEL, NULL);

    field_select(search_field);
    keyboard_show(true);
}

void radio_search_close(void)
{
    if(!backdrop) return;

    /*Usually called from the close button, which must not be deleted from
     *under its own event.*/
    lv_obj_delete_async(backdrop);
    backdrop     = NULL;
    panel        = NULL;
    field        = NULL;
    status_label = NULL;
    results_list = NULL;
    keyboard     = NULL;
    lv_memzero(result_items, sizeof(result_items));
}

void page_radio_search_set_results(const page_radio_station_t results[], const bool added[], uint32_t count)
{
    if(!backdrop) return;

    lv_obj_clean(results_list);
    lv_memzero(result_items, sizeof(result_items));
    if(count > PAGE_RADIO_RESULT_MAX) count = PAGE_RADIO_RESULT_MAX;

    for(uint32_t i = 0; i < count; i++) {
        /*Not clickable, so a drag anywhere on it scrolls the results.*/
        lv_obj_t * row = lv_obj_create(results_list);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_clickable(row, false);
        radio_row_fill(row, &results[i]);

        lv_obj_t * add = radio_round_button_create(row, LV_SYMBOL_PLUS, ADD_SIZE);
        lv_obj_add_event_cb(add, add_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);

        result_items[i] = row;
        page_radio_search_set_added(i, added ? added[i] : false);
    }

    lv_obj_scroll_to_y(results_list, 0, LV_ANIM_OFF);
}

void page_radio_search_set_added(uint32_t index, bool added)
{
    if(!backdrop || index >= PAGE_RADIO_RESULT_MAX || !result_items[index]) return;

    lv_obj_t * btn   = lv_obj_get_child(result_items[index], -1);
    lv_obj_t * glyph = lv_obj_get_child(btn, 0);

    result_added[index] = added;
    lv_label_set_text(glyph, added ? LV_SYMBOL_OK : LV_SYMBOL_PLUS);
    lv_obj_set_style_bg_color(btn, added ? UI_COLOR_CARD : UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_color(glyph, added ? UI_COLOR_GOOD : lv_color_white(), LV_PART_MAIN);
}

void page_radio_search_set_status(const char * text)
{
    if(!backdrop) return;

    lv_label_set_text(status_label, text ? text : "");
    lv_obj_set_hidden(status_label, text == NULL);
}

void page_radio_search_update_result(uint32_t index, const page_radio_station_t * station)
{
    if(!backdrop || index >= PAGE_RADIO_RESULT_MAX || !result_items[index]) return;

    radio_row_update(result_items[index], station);
}

bool page_radio_search_is_open(void)
{
    return backdrop != NULL;
}

void page_radio_set_search_cb(page_radio_search_cb_t cb)
{
    search_cb = cb;
}

void page_radio_set_add_cb(page_radio_add_cb_t cb)
{
    add_cb = cb;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Show or hide the keyboard, and fit the panel into the space above it. */
static void keyboard_show(bool show)
{
    int32_t screen  = lv_display_get_vertical_resolution(lv_display_get_default());
    int32_t covered = show ? screen * KEYBOARD_HEIGHT_PCT / 100 : 0;

    lv_obj_set_hidden(keyboard, !show);
    ui_keyboard_attach(keyboard, show ? field : NULL, UI_KEYBOARD_TEXT);

    if(show) lv_obj_add_state(field, LV_STATE_FOCUSED);
    else lv_obj_remove_state(field, LV_STATE_FOCUSED);

    lv_obj_set_height(panel, screen - covered - 2 * UI_GAP);
}

static void field_select(page_radio_search_field_t which)
{
    search_field = which;

    for(uint32_t i = 0; i < PAGE_RADIO_SEARCH_COUNT; i++) {
        if(i == (uint32_t)which) lv_obj_add_state(segments[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(segments[i], LV_STATE_CHECKED);
    }

    lv_textarea_set_placeholder_text(field, field_hints[which]);
}

static void search_run(void)
{
    const char * query = lv_textarea_get_text(field);

    while(*query == ' ') query++;
    if(*query == '\0') return;

    keyboard_show(false);
    if(search_cb) search_cb(search_field, query);
}

static void close_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    radio_search_close();
}

static void segment_clicked(lv_event_t * e)
{
    field_select((page_radio_search_field_t)(lv_uintptr_t)lv_event_get_user_data(e));

    /*Something typed already: search it the new way straight away.*/
    const char * query = lv_textarea_get_text(field);
    while(*query == ' ') query++;
    if(*query) search_run();
}

static void field_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    keyboard_show(true);
}

static void keyboard_ready(lv_event_t * e)
{
    LV_UNUSED(e);
    search_run();
}

static void keyboard_cancel(lv_event_t * e)
{
    LV_UNUSED(e);
    keyboard_show(false);
}

static void add_clicked(lv_event_t * e)
{
    uint32_t index = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);

    if(index >= PAGE_RADIO_RESULT_MAX || result_added[index]) return;
    if(add_cb) add_cb(index);
}
