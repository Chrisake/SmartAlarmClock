/**
 * @file page_settings.c
 *
 * The tabs, the on-screen keyboard, and the building blocks every tab is made
 * of. The tabs themselves are in settings_wifi.c, settings_mqtt.c,
 * settings_time.c and settings_device.c.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/settings_private.h"
#include "ui/ui_picker.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define TAB_BAR_HEIGHT   56
#define FIELD_RADIUS     10
#define BUTTON_HEIGHT    48
#define SEGMENT_HEIGHT   40
#define DROPDOWN_WIDTH   200

/** Share of the screen height the keyboard takes. */
#define KEYBOARD_HEIGHT_PCT 42

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);
static void       on_hide(void);

static void tab_changed(lv_event_t * e);
static void field_clicked(lv_event_t * e);
static void keyboard_done(lv_event_t * e);
static void keyboard_close(void);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "Settings",
    .icon    = LV_SYMBOL_SETTINGS,
    .create  = create,
    .on_show = NULL,
    /*The keyboard and the pickers live on the top layer, above every page.*/
    .on_hide = on_hide,
};

static const char * const tab_names[PAGE_SETTINGS_TAB_COUNT] = {
    [PAGE_SETTINGS_TAB_WIFI]   = "Wi-Fi",
    [PAGE_SETTINGS_TAB_MQTT]   = "MQTT",
    [PAGE_SETTINGS_TAB_TIME]   = "Date & time",
    [PAGE_SETTINGS_TAB_DEVICE] = "Device",
};

static bool       built;
static bool       values_given;
static lv_obj_t * tabview;
static lv_obj_t * tabs[PAGE_SETTINGS_TAB_COUNT];

/** Survives ui_rebuild(), so a change of theme leaves the same tab showing. */
static uint32_t active_tab;

static lv_obj_t * keyboard;
static lv_obj_t * keyboard_field;

static page_settings_change_cb_t change_cb;
static page_settings_scan_cb_t   scan_cb;
static page_settings_wifi_cb_t   wifi_cb;
static page_settings_mqtt_cb_t   mqtt_cb;
static page_settings_time_cb_t   time_cb;

/**********************
 *  GLOBAL VARIABLES
 **********************/

settings_t sp_values;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const ui_page_t * page_settings_desc(void)
{
    return &desc;
}

void page_settings_set_values(const settings_t * settings)
{
    sp_values    = *settings;
    values_given = true;
    if(!built) return;

    sp_wifi_values();
    sp_mqtt_values();
    sp_time_values();
    sp_device_values();
}

void page_settings_set_now(const struct tm * local)
{
    if(built) sp_time_now(local);
}

void page_settings_set_networks(const page_settings_network_t networks[], uint32_t count)
{
    if(built) sp_wifi_networks(networks, count);
}

void page_settings_set_scanning(bool scanning)
{
    if(built) sp_wifi_scanning(scanning);
}

void page_settings_set_wifi_status(page_settings_link_t link, const char * detail)
{
    if(built) sp_wifi_status(link, detail);
}

void page_settings_set_mqtt_status(page_settings_link_t link, const char * detail)
{
    if(built) sp_mqtt_status(link, detail);
}

void page_settings_set_change_cb(page_settings_change_cb_t cb)
{
    change_cb = cb;
}

void page_settings_set_scan_cb(page_settings_scan_cb_t cb)
{
    scan_cb = cb;
}

void page_settings_set_wifi_cb(page_settings_wifi_cb_t cb)
{
    wifi_cb = cb;
}

void page_settings_set_mqtt_cb(page_settings_mqtt_cb_t cb)
{
    mqtt_cb = cb;
}

void page_settings_set_time_cb(page_settings_time_cb_t cb)
{
    time_cb = cb;
}

/*=====================
 * Going back out, for the tabs
 *====================*/

void sp_changed(void)
{
    if(change_cb) change_cb(&sp_values);
}

void sp_scan(void)
{
    if(scan_cb) scan_cb();
}

void sp_wifi_connect(const char * ssid, const char * password)
{
    keyboard_close();
    if(wifi_cb) wifi_cb(ssid, password);
}

void sp_mqtt_save(void)
{
    keyboard_close();
    if(mqtt_cb) mqtt_cb(&sp_values);
}

void sp_set_time(const struct tm * local)
{
    if(time_cb) time_cb(local);
}

/*=====================
 * Building blocks
 *====================*/

lv_obj_t * sp_box_create(lv_obj_t * parent)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
    return box;
}

lv_obj_t * sp_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(card, UI_GAP, LV_PART_MAIN);
    return card;
}

lv_obj_t * sp_row_create(lv_obj_t * parent, const char * name)
{
    lv_obj_t * row = sp_box_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_min_height(row, UI_TOUCH_MIN, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * label = ui_label_create(row, name, UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(label, 1);
    ui_label_single_line(label, UI_FONT_SM);

    return row;
}

void sp_row_rename(lv_obj_t * row, const char * name)
{
    lv_label_set_text(lv_obj_get_child(row, 0), name);
}

lv_obj_t * sp_field_create(lv_obj_t * parent, const char * name, const char * placeholder,
                           sp_field_kind_t kind, lv_obj_t ** textarea)
{
    lv_obj_t * box = sp_box_create(parent);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 4, LV_PART_MAIN);

    ui_label_create(box, name, UI_FONT_XS, UI_COLOR_TEXT_DIM);

    lv_obj_t * ta = lv_textarea_create(box);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder ? placeholder : "");
    lv_obj_set_style_text_font(ta, UI_FONT_SM, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, UI_COLOR_TEXT_DIM, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, FIELD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 12, LV_PART_MAIN);

    if(kind == SP_FIELD_PASSWORD) lv_textarea_set_password_mode(ta, true);
    if(kind == SP_FIELD_NUMBER) {
        lv_textarea_set_accepted_chars(ta, "0123456789");
        lv_textarea_set_max_length(ta, 5);
    }

    lv_obj_add_event_cb(ta, field_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)kind);

    *textarea = ta;
    return box;
}

void sp_field_set(lv_obj_t * textarea, const char * text)
{
    /*Never overwrite what someone is in the middle of typing.*/
    if(textarea == keyboard_field) return;
    if(strcmp(lv_textarea_get_text(textarea), text) == 0) return;

    lv_textarea_set_text(textarea, text);
}

lv_obj_t * sp_button_create(lv_obj_t * parent, const char * text, bool primary)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, BUTTON_HEIGHT);
    lv_obj_set_style_min_width(btn, 120, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, UI_PAD + 4, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, FIELD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, primary ? UI_COLOR_ACCENT : UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t * label = ui_label_create(btn, text, UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_center(label);

    return btn;
}

void sp_enable(lv_obj_t * obj, bool enabled)
{
    if(enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else        lv_obj_add_state(obj, LV_STATE_DISABLED);

    lv_obj_set_style_opa(obj, enabled ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
}

lv_obj_t * sp_switch_create(lv_obj_t * parent, lv_event_cb_t cb)
{
    lv_obj_t * sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 60, 32);
    lv_obj_set_style_bg_color(sw, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, UI_COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(sw, 8);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

void sp_switch_set(lv_obj_t * sw, bool on)
{
    if(on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else   lv_obj_remove_state(sw, LV_STATE_CHECKED);
}

lv_obj_t * sp_segmented_create(lv_obj_t * parent, const char * const options[], uint32_t count,
                               lv_event_cb_t cb)
{
    lv_obj_t * segmented = sp_box_create(parent);
    lv_obj_set_flex_flow(segmented, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(segmented, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(segmented, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(segmented, FIELD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(segmented, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(segmented, 4, LV_PART_MAIN);

    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t * btn = lv_button_create(segmented);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, SEGMENT_HEIGHT);
        lv_obj_set_style_min_width(btn, 64, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(btn, 14, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, FIELD_RADIUS - 2, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, options[i]);
        lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
    }

    return segmented;
}

void sp_segmented_select(lv_obj_t * segmented, uint32_t index)
{
    for(uint32_t i = 0; i < lv_obj_get_child_count(segmented); i++) {
        lv_obj_t * btn = lv_obj_get_child(segmented, (int32_t)i);
        if(i == index) lv_obj_add_state(btn, LV_STATE_CHECKED);
        else           lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
}

lv_obj_t * sp_dropdown_create(lv_obj_t * parent, const char * options, lv_event_cb_t cb)
{
    lv_obj_t * dropdown = lv_dropdown_create(parent);
    lv_dropdown_set_options(dropdown, options);
    lv_obj_set_width(dropdown, DROPDOWN_WIDTH);
    lv_obj_set_style_text_font(dropdown, UI_FONT_SM, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dropdown, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, FIELD_RADIUS, LV_PART_MAIN);

    lv_obj_t * list = lv_dropdown_get_list(dropdown);
    lv_obj_set_style_text_font(list, UI_FONT_SM, LV_PART_MAIN);
    lv_obj_set_style_text_color(list, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_max_height(list, 300, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, UI_COLOR_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(list, UI_COLOR_TEXT, LV_PART_SELECTED | LV_STATE_CHECKED);

    lv_obj_add_event_cb(dropdown, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return dropdown;
}

lv_obj_t * sp_status_create(lv_obj_t * parent, const char * glyph)
{
    lv_obj_t * status = sp_box_create(parent);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, UI_GAP, LV_PART_MAIN);

    ui_label_create(status, glyph, UI_FONT_MD, UI_COLOR_TEXT_DIM);

    lv_obj_t * text = ui_label_create(status, "", UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);

    return status;
}

void sp_status_set(lv_obj_t * status, page_settings_link_t link, const char * detail,
                   const char * const defaults[])
{
    lv_color_t color = UI_COLOR_TEXT_DIM;

    switch(link) {
        case PAGE_SETTINGS_LINK_BUSY:      color = UI_COLOR_WARN; break;
        case PAGE_SETTINGS_LINK_CONNECTED: color = UI_COLOR_GOOD; break;
        case PAGE_SETTINGS_LINK_FAILED:    color = UI_COLOR_BAD;  break;
        default: break;
    }

    lv_obj_set_style_text_color(lv_obj_get_child(status, 0), color, LV_PART_MAIN);
    lv_label_set_text(lv_obj_get_child(status, 1), detail ? detail : defaults[link]);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create(lv_obj_t * parent)
{
    tabview = lv_tabview_create(parent);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_row(tabview, UI_GAP, LV_PART_MAIN);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, TAB_BAR_HEIGHT);

    for(uint32_t i = 0; i < PAGE_SETTINGS_TAB_COUNT; i++) {
        tabs[i] = lv_tabview_add_tab(tabview, tab_names[i]);
        lv_obj_set_style_bg_opa(tabs[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tabs[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(tabs[i], UI_GAP, LV_PART_MAIN);
        lv_obj_set_flex_flow(tabs[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(tabs[i], LV_DIR_VER);
    }

    /*The tab bar as a segmented control in a card, like the other pages'
     *filters, rather than the stock underlined tabs.*/
    lv_obj_t * bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(bar, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, UI_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(bar, 6, LV_PART_MAIN);

    for(uint32_t i = 0; i < PAGE_SETTINGS_TAB_COUNT; i++) {
        lv_obj_t * btn = lv_tabview_get_tab_button(tabview, (int32_t)i);
        lv_obj_set_style_radius(btn, FIELD_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_font(btn, UI_FONT_SM, LV_PART_MAIN);
    }

    /*Tabs change from the bar only. Swiping between them would also catch
     *the drags meant for the brightness sliders.*/
    lv_obj_set_scrollable(lv_tabview_get_content(tabview), false);
    lv_obj_add_event_cb(tabview, tab_changed, LV_EVENT_VALUE_CHANGED, NULL);

    sp_wifi_create(tabs[PAGE_SETTINGS_TAB_WIFI]);
    sp_mqtt_create(tabs[PAGE_SETTINGS_TAB_MQTT]);
    sp_time_create(tabs[PAGE_SETTINGS_TAB_TIME]);
    sp_device_create(tabs[PAGE_SETTINGS_TAB_DEVICE]);

    built = true;

    /*Filled from defaults until the settings feed reports; after a rebuild
     *the values already given.*/
    if(!values_given) settings_defaults(&sp_values);
    page_settings_set_values(&sp_values);

    lv_tabview_set_active(tabview, active_tab, LV_ANIM_OFF);

    return tabview;
}

static void on_hide(void)
{
    keyboard_close();
    ui_picker_close();

    /*Dropped rather than kept: a rebuild follows every change of theme, and a
     *keyboard kept from before would wear the old colours.*/
    if(keyboard) {
        lv_obj_delete_async(keyboard);
        keyboard = NULL;
    }
}

static void tab_changed(lv_event_t * e)
{
    LV_UNUSED(e);
    active_tab = lv_tabview_get_tab_active(tabview);
    keyboard_close();
}

static void field_clicked(lv_event_t * e)
{
    lv_obj_t *      field = lv_event_get_target_obj(e);
    sp_field_kind_t kind  = (sp_field_kind_t)(lv_uintptr_t)lv_event_get_user_data(e);

    if(lv_obj_has_state(field, LV_STATE_DISABLED)) return;

    if(!keyboard) {
        keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(KEYBOARD_HEIGHT_PCT));
        lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_CANCEL, NULL);
    }

    if(keyboard_field && keyboard_field != field) lv_obj_remove_state(keyboard_field, LV_STATE_FOCUSED);
    keyboard_field = field;

    lv_keyboard_set_mode(keyboard, kind == SP_FIELD_NUMBER ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, field);
    lv_obj_set_hidden(keyboard, false);

    /*Room at the bottom of the tabs to scroll the field clear of the keyboard.*/
    int32_t covered = lv_display_get_vertical_resolution(NULL) * KEYBOARD_HEIGHT_PCT / 100;
    for(uint32_t i = 0; i < PAGE_SETTINGS_TAB_COUNT; i++) {
        lv_obj_set_style_pad_bottom(tabs[i], covered, LV_PART_MAIN);
    }
    lv_obj_update_layout(tabs[active_tab]);
    lv_obj_scroll_to_view_recursive(field, LV_ANIM_ON);
}

static void keyboard_done(lv_event_t * e)
{
    LV_UNUSED(e);
    keyboard_close();
}

static void keyboard_close(void)
{
    if(!keyboard) return;

    lv_obj_set_hidden(keyboard, true);
    lv_keyboard_set_textarea(keyboard, NULL);

    /*Losing focus is what tells a field to commit what was typed.*/
    lv_obj_t * field = keyboard_field;
    keyboard_field   = NULL;
    if(field) {
        lv_obj_remove_state(field, LV_STATE_FOCUSED);
        lv_obj_send_event(field, LV_EVENT_DEFOCUSED, NULL);
    }

    if(!built) return;
    for(uint32_t i = 0; i < PAGE_SETTINGS_TAB_COUNT; i++) {
        lv_obj_set_style_pad_bottom(tabs[i], 0, LV_PART_MAIN);
    }
}
