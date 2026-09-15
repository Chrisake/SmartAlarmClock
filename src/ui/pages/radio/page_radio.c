/**
 * @file page_radio.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/radio/radio_private.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

/** Longest "country • language • genre" line kept for a station. */
#define SUBTITLE_LEN 160

/** The now-playing artwork's square, and the favicon inside it. */
#define ART_SIZE       150
#define ART_IMAGE_SIZE 96

/** Edit mode's remove button and drag handle. */
#define REMOVE_SIZE 32
#define HANDLE_SIZE 44

/** How long the remove buttons and handles take to slide in or out, and how
 *  much later each row starts than the one above it. */
#define EDIT_ANIM_MS      240
#define EDIT_ANIM_STAGGER 22

/** Rows below this many start together, so a long list is not left waiting. */
#define EDIT_ANIM_STAGGER_ROWS 8

/** How near the top or bottom of the list a dragged station scrolls it, and by how much a step. */
#define DRAG_EDGE        48
#define DRAG_SCROLL_STEP 8

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);
static void       on_hide(void);

static void       now_playing_card_create(lv_obj_t * parent);
static void       stations_card_create(lv_obj_t * parent);
static lv_obj_t * item_create(const page_radio_station_t * station);
static void       subtitle_write(const page_radio_station_t * station, char * buf, size_t len);
static lv_obj_t * edit_slot_create(lv_obj_t * item, int32_t size);
static void       edit_set(bool on);
static void       edit_reveal(lv_obj_t * item, int32_t amount);
static void       edit_reveal_anim(void * item, int32_t amount);
static void       station_select(uint32_t index);
static void       drag_follow(lv_obj_t * item);
static void       drag_finish(lv_obj_t * item);

static void play_clicked(lv_event_t * e);
static void skip_clicked(lv_event_t * e);
static void volume_changed(lv_event_t * e);
static void station_clicked(lv_event_t * e);
static void add_clicked(lv_event_t * e);
static void edit_clicked(lv_event_t * e);
static void remove_clicked(lv_event_t * e);
static void handle_event(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "Radio",
    .icon    = UI_SYMBOL_RADIO,
    .create  = create,
    .on_show = NULL,
    .on_hide = on_hide,
};

static lv_obj_t * station_name_label;
static lv_obj_t * track_label;
static lv_obj_t * status_label;
static lv_obj_t * art;
static lv_obj_t * play_button;
static lv_obj_t * play_icon;
static lv_obj_t * volume_slider;
static lv_obj_t * volume_label;
static lv_obj_t * station_list;
static lv_obj_t * edit_label;

static page_radio_station_t stations[PAGE_RADIO_STATION_MAX];
static lv_obj_t *           station_items[PAGE_RADIO_STATION_MAX];
static uint32_t             station_count;
static uint32_t             station_current = UINT32_MAX;

static bool playing;
static bool editing;

/** The station being dragged by its handle, and where it started. */
static struct {
    lv_obj_t * item;
    uint32_t   from;
} drag;

static page_radio_station_cb_t station_cb;
static page_radio_play_cb_t    play_cb;
static page_radio_volume_cb_t  volume_cb;
static page_radio_remove_cb_t  remove_cb;
static page_radio_move_cb_t    move_cb;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const ui_page_t * page_radio_desc(void)
{
    return &desc;
}

void page_radio_set_stations(const page_radio_station_t list[], uint32_t count)
{
    if(count > PAGE_RADIO_STATION_MAX) count = PAGE_RADIO_STATION_MAX;

    drag.item = NULL;
    lv_obj_clean(station_list);
    station_count   = count;
    station_current = UINT32_MAX;

    for(uint32_t i = 0; i < count; i++) {
        stations[i]      = list[i];
        station_items[i] = item_create(&list[i]);
    }
}

void page_radio_update_station(uint32_t index, const page_radio_station_t * station)
{
    if(index >= station_count) return;

    stations[index] = *station;
    radio_row_update(station_items[index], station);
}

void page_radio_set_current_station(uint32_t index)
{
    if(index >= station_count) return;

    page_radio_clear_current_station();

    station_current = index;
    lv_obj_add_state(station_items[index], LV_STATE_CHECKED);
    lv_obj_scroll_to_view(station_items[index], LV_ANIM_ON);
}

void page_radio_clear_current_station(void)
{
    if(station_current < station_count) {
        lv_obj_remove_state(station_items[station_current], LV_STATE_CHECKED);
    }
    station_current = UINT32_MAX;
}

void page_radio_set_now_playing(const char * station, const char * track, const lv_image_dsc_t * artwork)
{
    lv_label_set_text(station_name_label, station ? station : "No station selected");
    lv_obj_set_style_text_color(station_name_label, station ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_label_set_text(track_label, track ? track : "");
    radio_icon_set(art, artwork);
}

void page_radio_set_state(page_radio_state_t state, const char * detail)
{
    playing = (state == PAGE_RADIO_STATE_PLAYING || state == PAGE_RADIO_STATE_BUFFERING);
    lv_label_set_text(play_icon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    lv_color_t color = UI_COLOR_TEXT_DIM;
    const char * fallback = "Stopped";

    switch(state) {
        case PAGE_RADIO_STATE_BUFFERING:
            color = UI_COLOR_WARN;
            fallback = "Buffering...";
            break;
        case PAGE_RADIO_STATE_PLAYING:
            color = UI_COLOR_GOOD;
            fallback = "Playing";
            break;
        case PAGE_RADIO_STATE_ERROR:
            color = UI_COLOR_BAD;
            fallback = "Stream unavailable";
            break;
        case PAGE_RADIO_STATE_STOPPED:
        default:
            break;
    }

    lv_label_set_text(status_label, detail ? detail : fallback);
    lv_obj_set_style_text_color(status_label, color, LV_PART_MAIN);
}

void page_radio_set_volume(int32_t volume)
{
    lv_slider_set_value(volume_slider, volume, LV_ANIM_ON);
    lv_label_set_text_fmt(volume_label, "%d", (int)volume);
}

void page_radio_set_station_cb(page_radio_station_cb_t cb)
{
    station_cb = cb;
}

void page_radio_set_play_cb(page_radio_play_cb_t cb)
{
    play_cb = cb;
}

void page_radio_set_volume_cb(page_radio_volume_cb_t cb)
{
    volume_cb = cb;
}

void page_radio_set_remove_cb(page_radio_remove_cb_t cb)
{
    remove_cb = cb;
}

void page_radio_set_move_cb(page_radio_move_cb_t cb)
{
    move_cb = cb;
}

void radio_row_fill(lv_obj_t * row, const page_radio_station_t * station)
{
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, UI_TOUCH_MIN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, UI_PAD, LV_PART_MAIN);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon = radio_icon_create(row, RADIO_ROW_ICON_SIZE, RADIO_ROW_ICON_SIZE, UI_FONT_MD, UI_COLOR_CARD);
    lv_obj_set_user_data(row, icon);

    /*Name over subtitle, set close. The box must not be clickable, or it
     *swallows taps meant for the row.*/
    lv_obj_t * text = lv_obj_create(row);
    lv_obj_set_size(text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_style_bg_opa(text, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(text, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(text, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(text, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(text, false);
    lv_obj_set_clickable(text, false);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);

    lv_obj_t * name = ui_label_create(text, "", UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_set_width(name, LV_PCT(100));
    ui_label_single_line(name, UI_FONT_MD);

    /*Wraps rather than truncating, so a long country or language never hides
     *the genre at the end.*/
    lv_obj_t * subtitle = ui_label_create(text, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(subtitle, LV_PCT(100));
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_MODE_WRAP);

    radio_row_update(row, station);
}

void radio_row_update(lv_obj_t * row, const page_radio_station_t * station)
{
    lv_obj_t * icon     = lv_obj_get_user_data(row);
    lv_obj_t * text     = lv_obj_get_child(row, lv_obj_get_index(icon) + 1);
    lv_obj_t * subtitle = lv_obj_get_child(text, 1);
    char       line[SUBTITLE_LEN];

    radio_icon_set(icon, station->favicon);
    lv_label_set_text(lv_obj_get_child(text, 0), station->name ? station->name : "");

    subtitle_write(station, line, sizeof(line));
    lv_label_set_text(subtitle, line);
    lv_obj_set_hidden(subtitle, line[0] == '\0');
}

lv_obj_t * radio_icon_create(lv_obj_t * parent, int32_t size, int32_t image_size, const lv_font_t * font,
                             lv_color_t empty)
{
    lv_obj_t * icon = lv_obj_create(parent);
    lv_obj_set_size(icon, size, size);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
    /*Station logos are drawn for a light page; many are dark lettering on
     *nothing, which would vanish into a dark card. So a favicon -- USER_1,
     *see radio_icon_set() -- sits on white.*/
    lv_obj_set_style_bg_color(icon, empty, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon, lv_color_white(), LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(icon, LV_MIN(size / 5, UI_RADIUS), LV_PART_MAIN);
    lv_obj_set_style_clip_corner(icon, true, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(icon, false);
    lv_obj_set_clickable(icon, false);

    lv_obj_t * image = lv_image_create(icon);
    lv_obj_set_size(image, image_size, image_size);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(image);

    lv_obj_t * glyph = ui_label_create(icon, LV_SYMBOL_AUDIO, font, UI_COLOR_TEXT_DIM);
    lv_obj_center(glyph);

    radio_icon_set(icon, NULL);
    return icon;
}

void radio_icon_set(lv_obj_t * icon, const lv_image_dsc_t * favicon)
{
    lv_obj_t * image = lv_obj_get_child(icon, 0);
    lv_obj_t * glyph = lv_obj_get_child(icon, 1);

    if(favicon) lv_obj_add_state(icon, LV_STATE_USER_1);
    else lv_obj_remove_state(icon, LV_STATE_USER_1);

    lv_image_set_src(image, favicon);
    lv_obj_set_hidden(image, favicon == NULL);
    lv_obj_set_hidden(glyph, favicon != NULL);
}

lv_obj_t * radio_round_button_create(lv_obj_t * parent, const char * symbol, int32_t size)
{
    lv_obj_t * btn = lv_button_create(parent);

    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    radio_press_grow_off(btn);

    lv_obj_t * icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, UI_FONT_MD, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_center(icon);

    return btn;
}

/**
 * The default theme grows a pressed button by a few pixels past its edges,
 * which the zero-padded rows and lists these sit in then cut off. The theme's
 * press darkening is feedback enough.
 */
void radio_press_grow_off(lv_obj_t * obj)
{
    lv_obj_set_style_transform_width(obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create(lv_obj_t * parent)
{
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    /*Rebuilt with the rest of the UI when the theme changes.*/
    station_count   = 0;
    station_current = UINT32_MAX;
    editing         = false;
    drag.item       = NULL;

    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(root, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_grid_dsc_array(root, cols, rows);

    now_playing_card_create(root);
    stations_card_create(root);

    /*Empty until the radio feed reports in.*/
    page_radio_set_now_playing(NULL, NULL, NULL);
    page_radio_set_state(PAGE_RADIO_STATE_STOPPED, NULL);
    page_radio_set_volume(35);

    return root;
}

static void on_hide(void)
{
    radio_search_close();
    if(editing) edit_set(false);
}

static void now_playing_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, UI_GAP, LV_PART_MAIN);

    art = radio_icon_create(card, ART_SIZE, ART_IMAGE_SIZE, UI_FONT_XL, UI_COLOR_CARD_ALT);

    station_name_label = ui_label_create(card, "", UI_FONT_LG, UI_COLOR_TEXT);
    lv_obj_set_width(station_name_label, LV_PCT(100));
    ui_label_single_line(station_name_label, UI_FONT_LG);
    lv_obj_set_style_text_align(station_name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    track_label = ui_label_create(card, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_label_set_long_mode(track_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(track_label, LV_PCT(90));
    lv_obj_set_style_text_align(track_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    status_label = ui_label_create(card, "Stopped", UI_FONT_XS, UI_COLOR_TEXT_DIM);

    /*Transport row.*/
    lv_obj_t * transport = lv_obj_create(card);
    lv_obj_set_size(transport, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(transport, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(transport, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(transport, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(transport, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(transport, false);
    lv_obj_set_flex_flow(transport, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transport, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * prev = radio_round_button_create(transport, LV_SYMBOL_PREV, UI_TOUCH_MIN);
    lv_obj_add_event_cb(prev, skip_clicked, LV_EVENT_CLICKED, (void *)(lv_intptr_t) - 1);

    play_button = radio_round_button_create(transport, LV_SYMBOL_PLAY, 72);
    lv_obj_set_style_bg_color(play_button, UI_COLOR_ACCENT, LV_PART_MAIN);
    play_icon = lv_obj_get_child(play_button, 0);
    lv_obj_add_event_cb(play_button, play_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * next = radio_round_button_create(transport, LV_SYMBOL_NEXT, UI_TOUCH_MIN);
    lv_obj_add_event_cb(next, skip_clicked, LV_EVENT_CLICKED, (void *)(lv_intptr_t)1);

    /*Volume row.*/
    lv_obj_t * volume_row = lv_obj_create(card);
    lv_obj_set_size(volume_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(volume_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(volume_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(volume_row, 0, LV_PART_MAIN);
    /*Wide enough for the slider's knob to reach past the track's ends without
     *covering the icon at 0 or the reading at 100.*/
    lv_obj_set_style_pad_column(volume_row, UI_SLIDER_KNOB_OVERHANG, LV_PART_MAIN);
    lv_obj_set_scrollable(volume_row, false);
    lv_obj_set_flex_flow(volume_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volume_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_label_create(volume_row, LV_SYMBOL_VOLUME_MID, UI_FONT_MD, UI_COLOR_TEXT_DIM);

    /*The helper also stops the content-height row clipping the knob.*/
    volume_slider = ui_slider_create(volume_row, UI_COLOR_ACCENT);
    lv_obj_set_flex_grow(volume_slider, 1);
    lv_obj_add_event_cb(volume_slider, volume_changed, LV_EVENT_VALUE_CHANGED, NULL);

    volume_label = ui_label_create(volume_row, "35", UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_width(volume_label, 36);
    lv_obj_set_style_text_align(volume_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
}

static void stations_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_pad_row(card, UI_GAP, LV_PART_MAIN);

    /*[+] ...................... Edit*/
    lv_obj_t * bar = lv_obj_create(card);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(bar, false);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * add = radio_round_button_create(bar, LV_SYMBOL_PLUS, 40);
    lv_obj_set_style_bg_color(add, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_ext_click_area(add, 8);
    lv_obj_add_event_cb(add, add_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t * edit = lv_button_create(bar);
    lv_obj_set_size(edit, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_bg_opa(edit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(edit, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(edit, UI_GAP, LV_PART_MAIN);
    lv_obj_set_ext_click_area(edit, 8);
    radio_press_grow_off(edit);
    lv_obj_add_event_cb(edit, edit_clicked, LV_EVENT_CLICKED, NULL);

    edit_label = ui_label_create(edit, "Edit", UI_FONT_SM, UI_COLOR_ACCENT);
    lv_obj_center(edit_label);

    /*A scrolling flex column rather than lv_list, which LVGL 9.5 deprecates.*/
    station_list = lv_obj_create(card);
    lv_obj_set_width(station_list, LV_PCT(100));
    lv_obj_set_flex_grow(station_list, 1);
    lv_obj_set_style_bg_opa(station_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(station_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(station_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(station_list, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(station_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(station_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(station_list, LV_SCROLLBAR_MODE_AUTO);
}

/**
 * A station in the list: [remove] logo name/subtitle [handle]. The remove
 * button and handle only show in edit mode.
 */
static lv_obj_t * item_create(const page_radio_station_t * station)
{
    lv_obj_t * item = lv_button_create(station_list);

    lv_obj_t * remove = radio_round_button_create(edit_slot_create(item, REMOVE_SIZE), LV_SYMBOL_MINUS, REMOVE_SIZE);
    lv_obj_set_style_bg_color(remove, UI_COLOR_BAD, LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_ext_click_area(remove, (UI_TOUCH_MIN - REMOVE_SIZE) / 2);
    lv_obj_center(remove);
    lv_obj_add_event_cb(remove, remove_clicked, LV_EVENT_CLICKED, NULL);

    radio_row_fill(item, station);

    /*Selection is an accent outline rather than an accent fill: a solid
     *fill puts dim secondary text on a saturated blue and kills contrast.
     *Every row carries the border and only the selected one shows it, so
     *selecting a station does not nudge its text. The CHECKED background has
     *to be restated, or the default theme's checked style wins.*/
    lv_obj_set_style_bg_color(item, UI_COLOR_CARD_ALT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(item, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 2, LV_PART_MAIN);
    lv_obj_set_style_border_opa(item, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
    /*Lifted while dragged.*/
    lv_obj_set_style_bg_color(item, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_USER_1);
    radio_press_grow_off(item);

    /*Pressing the handle drags the row rather than scrolling the list, so
     *the scroll must not pass up from it.*/
    lv_obj_t * handle = lv_obj_create(edit_slot_create(item, HANDLE_SIZE));
    lv_obj_set_size(handle, HANDLE_SIZE, HANDLE_SIZE);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(handle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(handle, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(handle, false);
    lv_obj_set_scroll_chain(handle, false);
    lv_obj_set_press_lock(handle, true);
    lv_obj_set_ext_click_area(handle, (UI_TOUCH_MIN - HANDLE_SIZE) / 2);
    lv_obj_center(handle);
    lv_obj_add_event_cb(handle, handle_event, LV_EVENT_ALL, NULL);

    lv_obj_t * grip = ui_label_create(handle, LV_SYMBOL_BARS, UI_FONT_MD, UI_COLOR_TEXT_DIM);
    lv_obj_center(grip);

    edit_reveal(item, editing ? LV_SCALE_NONE : 0);
    lv_obj_add_event_cb(item, station_clicked, LV_EVENT_CLICKED, NULL);
    return item;
}

static void subtitle_write(const page_radio_station_t * station, char * buf, size_t len)
{
    const char * parts[] = {station->country, station->language, station->genre};
    size_t used = 0;

    buf[0] = '\0';
    for(uint32_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        if(parts[i] == NULL || parts[i][0] == '\0') continue;

        int n = lv_snprintf(buf + used, len - used, "%s%s", used ? " " UI_BULLET " " : "", parts[i]);
        if(n < 0 || (size_t)n >= len - used) break;
        used += (size_t)n;
    }
}

/**
 * The space a remove button or handle sits in, centred. The edit animation
 * widens and narrows it, so the row's name slides aside rather than jumping,
 * while the control inside grows to match -- it never draws past the slot, so
 * the slot need not clip, and leaving overflow visible lets the control's
 * enlarged touch area reach past it.
 */
static lv_obj_t * edit_slot_create(lv_obj_t * item, int32_t size)
{
    lv_obj_t * slot = lv_obj_create(item);
    lv_obj_set_size(slot, size, size);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(slot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(slot, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(slot, false);
    lv_obj_set_clickable(slot, false);
    lv_obj_set_overflow_visible(slot, true);
    return slot;
}

/**
 * Slide the remove buttons in from the left and the handles in from the
 * right, each row a moment after the one above, or fold them away again.
 * Every row starts from wherever it has got to, so tapping Edit and then Done
 * straight away turns the motion round smoothly.
 */
static void edit_set(bool on)
{
    editing = on;
    lv_label_set_text(edit_label, on ? "Done" : "Edit");

    for(uint32_t i = 0; i < station_count; i++) {
        lv_obj_t * item    = station_items[i];
        lv_obj_t * control = lv_obj_get_child(lv_obj_get_child(item, 0), 0);
        int32_t    from    = lv_obj_get_style_transform_scale_x(control, LV_PART_MAIN);

        lv_anim_delete(item, edit_reveal_anim);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, item);
        lv_anim_set_exec_cb(&a, edit_reveal_anim);
        lv_anim_set_values(&a, from, on ? LV_SCALE_NONE : 0);
        lv_anim_set_duration(&a, EDIT_ANIM_MS);
        lv_anim_set_delay(&a, LV_MIN(i, EDIT_ANIM_STAGGER_ROWS) * EDIT_ANIM_STAGGER);
        lv_anim_set_path_cb(&a, on ? lv_anim_path_ease_out : lv_anim_path_ease_in);
        lv_anim_start(&a);
    }
}

/**
 * How far out one row's edit controls are: 0 folded away, LV_SCALE_NONE fully
 * shown. Each slot takes that share of its width, and its control that scale
 * and opacity.
 */
static void edit_reveal(lv_obj_t * item, int32_t amount)
{
    static const int32_t sizes[2] = {REMOVE_SIZE, HANDLE_SIZE};
    lv_obj_t *           slots[2] = {lv_obj_get_child(item, 0), lv_obj_get_child(item, -1)};

    /*The row's column gap would still open up beside an empty slot. A
     *negative margin closes it, easing off as the control grows in.*/
    int32_t gap = -UI_PAD * (LV_SCALE_NONE - amount) / LV_SCALE_NONE;

    for(int i = 0; i < 2; i++) {
        lv_obj_t * control = lv_obj_get_child(slots[i], 0);

        lv_obj_set_hidden(slots[i], amount <= 0);
        lv_obj_set_width(slots[i], sizes[i] * amount / LV_SCALE_NONE);
        lv_obj_set_style_transform_pivot_x(control, sizes[i] / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(control, sizes[i] / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_scale(control, amount, LV_PART_MAIN);
        lv_obj_set_style_opa(control, (lv_opa_t)(amount * LV_OPA_COVER / LV_SCALE_NONE), LV_PART_MAIN);
    }

    lv_obj_set_style_margin_right(slots[0], gap, LV_PART_MAIN);
    lv_obj_set_style_margin_left(slots[1], gap, LV_PART_MAIN);
}

static void edit_reveal_anim(void * item, int32_t amount)
{
    edit_reveal(item, amount);
}

/** What a tap on a station and previous/next have in common. */
static void station_select(uint32_t index)
{
    page_radio_set_current_station(index);
    /*The old track belongs to the old station; the stream fills in the new one's.*/
    page_radio_set_now_playing(stations[index].name, "", stations[index].favicon);

    if(station_cb) station_cb(index);
}

/** Move the dragged row past a neighbour once the finger is over that neighbour's middle. */
static void drag_follow(lv_obj_t * item)
{
    lv_indev_t * indev = lv_indev_active();
    lv_point_t   point;
    lv_area_t    area;

    if(!indev) return;
    lv_indev_get_point(indev, &point);

    int32_t index = lv_obj_get_index(item);

    if(index > 0) {
        lv_obj_get_coords(lv_obj_get_child(station_list, index - 1), &area);
        if(point.y < area.y1 + lv_area_get_height(&area) / 2) {
            lv_obj_move_to_index(item, index - 1);
            lv_obj_update_layout(station_list);
        }
    }
    if(index + 1 < (int32_t)station_count) {
        lv_obj_get_coords(lv_obj_get_child(station_list, index + 1), &area);
        if(point.y > area.y1 + lv_area_get_height(&area) / 2) {
            lv_obj_move_to_index(item, index + 1);
            lv_obj_update_layout(station_list);
        }
    }

    /*Near either end, scroll the list along so the row can travel further.*/
    lv_obj_get_coords(station_list, &area);
    if(point.y < area.y1 + DRAG_EDGE) lv_obj_scroll_by_bounded(station_list, 0, DRAG_SCROLL_STEP, LV_ANIM_OFF);
    else if(point.y > area.y2 - DRAG_EDGE) lv_obj_scroll_by_bounded(station_list, 0, -DRAG_SCROLL_STEP, LV_ANIM_OFF);
}

static void drag_finish(lv_obj_t * item)
{
    uint32_t from = drag.from;
    uint32_t to   = (uint32_t)lv_obj_get_index(item);

    lv_obj_remove_state(item, LV_STATE_USER_1);
    drag.item = NULL;

    if(from == to || from >= station_count || to >= station_count) return;

    page_radio_station_t moving      = stations[from];
    lv_obj_t *           moving_item = station_items[from];

    if(from < to) {
        lv_memmove(&stations[from], &stations[from + 1], (to - from) * sizeof(stations[0]));
        lv_memmove(&station_items[from], &station_items[from + 1], (to - from) * sizeof(station_items[0]));
    }
    else {
        lv_memmove(&stations[to + 1], &stations[to], (from - to) * sizeof(stations[0]));
        lv_memmove(&station_items[to + 1], &station_items[to], (from - to) * sizeof(station_items[0]));
    }
    stations[to]      = moving;
    station_items[to] = moving_item;

    if(station_current == from) station_current = to;
    else if(station_current < station_count) {
        if(from < to && station_current > from && station_current <= to) station_current--;
        else if(from > to && station_current >= to && station_current < from) station_current++;
    }

    if(move_cb) move_cb(from, to);
}

static void play_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    /*Optimistic flip so the button feels instant; the feed corrects us
     *through page_radio_set_state() once it knows what really happened.*/
    playing = !playing;
    lv_label_set_text(play_icon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    if(play_cb) play_cb(playing);
}

static void skip_clicked(lv_event_t * e)
{
    if(station_count == 0) return;

    int32_t  step = (int32_t)(lv_intptr_t)lv_event_get_user_data(e);
    uint32_t index;

    if(station_current >= station_count) index = 0;
    else if(step > 0) index = (station_current + 1) % station_count;
    else index = (station_current + station_count - 1) % station_count;

    station_select(index);
}

static void volume_changed(lv_event_t * e)
{
    int32_t volume = lv_slider_get_value(lv_event_get_target_obj(e));

    lv_label_set_text_fmt(volume_label, "%d", (int)volume);

    if(volume_cb) volume_cb(volume);
}

static void station_clicked(lv_event_t * e)
{
    if(editing) return;

    int32_t index = lv_obj_get_index(lv_event_get_target_obj(e));
    if(index < 0 || (uint32_t)index >= station_count) return;

    station_select((uint32_t)index);
}

static void add_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    radio_search_open();
}

static void edit_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    edit_set(!editing);
}

static void remove_clicked(lv_event_t * e)
{
    /*The button, in its slot, in the row.*/
    lv_obj_t * item  = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target_obj(e)));
    int32_t    found = lv_obj_get_index(item);

    if(found < 0 || (uint32_t)found >= station_count) return;

    uint32_t index       = (uint32_t)found;
    bool     was_current = index == station_current;

    lv_memmove(&stations[index], &stations[index + 1], (station_count - index - 1) * sizeof(stations[0]));
    lv_memmove(&station_items[index], &station_items[index + 1],
               (station_count - index - 1) * sizeof(station_items[0]));
    station_count--;

    if(was_current) station_current = UINT32_MAX;
    else if(station_current < UINT32_MAX && station_current > index) station_current--;

    /*The button tapped is inside the row, so the row cannot be deleted from
     *under its own event. Take it out of the list now, so every index after it
     *is right straight away, and delete it once the event is over.*/
    lv_obj_set_parent(item, lv_layer_sys());
    lv_obj_set_hidden(item, true);
    lv_obj_delete_async(item);

    if(was_current) page_radio_set_now_playing(NULL, NULL, NULL);

    if(remove_cb) remove_cb(index);
}

static void handle_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *      item = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_current_target_obj(e)));

    if(code == LV_EVENT_PRESSED) {
        drag.item = item;
        drag.from = (uint32_t)lv_obj_get_index(item);
        lv_obj_add_state(item, LV_STATE_USER_1);
    }
    else if(drag.item != item) {
        return;
    }
    else if(code == LV_EVENT_PRESSING) {
        drag_follow(item);
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        drag_finish(item);
    }
}
