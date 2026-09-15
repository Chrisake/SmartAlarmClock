/**
 * @file settings_wifi.c
 *
 * Wi-Fi tab. Left, the connection: its status, the network name and password,
 * and Connect. Right, what a scan found; tapping a network fills in its name
 * and moves on to the password.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/settings_private.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define NETWORK_ROW_HEIGHT 52
#define NETWORK_LIST_HEIGHT 360
#define SPINNER_SIZE       28

/** Signal strength, in dBm, above which a network's glyph is drawn full strength. */
#define RSSI_GOOD  -60
#define RSSI_FAIR  -75

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void networks_rebuild(void);
static void network_clicked(lv_event_t * e);
static void scan_clicked(lv_event_t * e);
static void connect_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const char * const status_text[] = {
    [PAGE_SETTINGS_LINK_IDLE]      = "Not connected",
    [PAGE_SETTINGS_LINK_BUSY]      = "Connecting...",
    [PAGE_SETTINGS_LINK_CONNECTED] = "Connected",
    [PAGE_SETTINGS_LINK_FAILED]    = "Could not connect",
};

static lv_obj_t * status;
static lv_obj_t * ssid_field;
static lv_obj_t * password_field;
static lv_obj_t * list;
static lv_obj_t * list_empty;
static lv_obj_t * spinner;

/*The page copies scan results, so they outlive the call that gave them.*/
static struct {
    char   ssid[SETTINGS_SSID_LEN];
    int8_t rssi;
    bool   secured;
} networks[PAGE_SETTINGS_NETWORK_MAX];
static uint32_t network_count;
static bool     scanned;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sp_wifi_create(lv_obj_t * tab)
{
    lv_obj_t * columns = sp_box_create(tab);
    lv_obj_set_width(columns, LV_PCT(100));
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(columns, UI_GAP, LV_PART_MAIN);

    /*The connection.*/
    lv_obj_t * form = sp_card_create(columns);
    lv_obj_set_width(form, 0);
    lv_obj_set_flex_grow(form, 1);

    status = sp_status_create(form, LV_SYMBOL_WIFI);
    sp_field_create(form, "Network", "Network name", SP_FIELD_TEXT, &ssid_field);
    lv_textarea_set_max_length(ssid_field, SETTINGS_SSID_LEN - 1);
    sp_field_create(form, "Password", "Password", SP_FIELD_PASSWORD, &password_field);
    lv_textarea_set_max_length(password_field, SETTINGS_WIFI_PASS_LEN - 1);

    lv_obj_t * actions = sp_box_create(form);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_style_pad_top(actions, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * connect = sp_button_create(actions, "Connect", true);
    lv_obj_add_event_cb(connect, connect_clicked, LV_EVENT_CLICKED, NULL);

    /*What is around.*/
    lv_obj_t * found = sp_card_create(columns);
    lv_obj_set_width(found, 0);
    lv_obj_set_flex_grow(found, 1);

    lv_obj_t * head = sp_box_create(found);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, UI_GAP, LV_PART_MAIN);

    spinner = lv_spinner_create(head);
    lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_hidden(spinner, true);

    lv_obj_t * scan = sp_button_create(head, LV_SYMBOL_REFRESH "  Scan", false);
    lv_obj_add_event_cb(scan, scan_clicked, LV_EVENT_CLICKED, NULL);

    list = sp_box_create(found);
    lv_obj_set_size(list, LV_PCT(100), NETWORK_LIST_HEIGHT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    /*Clickable, so a drag from the gap between networks still scrolls.*/
    lv_obj_set_clickable(list, true);

    list_empty = ui_label_create(found, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_floating(list_empty, true);
    lv_obj_align(list_empty, LV_ALIGN_CENTER, 0, 0);

    networks_rebuild();
}

void sp_wifi_values(void)
{
    sp_field_set(ssid_field, sp_values.wifi_ssid);
    sp_field_set(password_field, sp_values.wifi_password);
    networks_rebuild();
}

void sp_wifi_networks(const page_settings_network_t list_in[], uint32_t count)
{
    if(count > PAGE_SETTINGS_NETWORK_MAX) count = PAGE_SETTINGS_NETWORK_MAX;

    for(uint32_t i = 0; i < count; i++) {
        lv_strlcpy(networks[i].ssid, list_in[i].ssid ? list_in[i].ssid : "", sizeof(networks[i].ssid));
        networks[i].rssi    = list_in[i].rssi;
        networks[i].secured = list_in[i].secured;
    }

    network_count = count;
    scanned       = true;
    networks_rebuild();
}

void sp_wifi_scanning(bool scanning)
{
    lv_obj_set_hidden(spinner, !scanning);
}

void sp_wifi_status(page_settings_link_t link, const char * detail)
{
    sp_status_set(status, link, detail, status_text);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void networks_rebuild(void)
{
    lv_obj_clean(list);

    for(uint32_t i = 0; i < network_count; i++) {
        bool current = strcmp(networks[i].ssid, sp_values.wifi_ssid) == 0;

        lv_obj_t * row = lv_button_create(list);
        lv_obj_set_size(row, LV_PCT(100), NETWORK_ROW_HEIGHT);
        lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, UI_GAP, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, UI_GAP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        /*The network the clock is set to wears an outline.*/
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, current ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /*Signal as the glyph's strength.*/
        lv_color_t signal = networks[i].rssi > RSSI_GOOD ? UI_COLOR_TEXT
                            : (networks[i].rssi > RSSI_FAIR ? UI_COLOR_TEXT_DIM : UI_COLOR_DEVICE_OFF);
        ui_label_create(row, LV_SYMBOL_WIFI, UI_FONT_SM, signal);

        lv_obj_t * name = ui_label_create(row, networks[i].ssid, UI_FONT_SM, UI_COLOR_TEXT);
        lv_obj_set_flex_grow(name, 1);
        ui_label_single_line(name, UI_FONT_SM);

        if(networks[i].secured) ui_label_create(row, UI_GLYPH_LOCK, UI_FONT_ICON, UI_COLOR_TEXT_DIM);

        lv_obj_add_event_cb(row, network_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
    }

    lv_label_set_text(list_empty, !scanned ? "Scan to find networks nearby"
                                  : (network_count == 0 ? "No networks found" : ""));
    lv_obj_set_hidden(list_empty, network_count > 0);
}

static void network_clicked(lv_event_t * e)
{
    uint32_t i = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if(i >= network_count) return;

    lv_textarea_set_text(ssid_field, networks[i].ssid);

    /*A secured network needs its password next; an open one needs nothing.*/
    if(networks[i].secured) {
        lv_textarea_set_text(password_field, "");
        lv_obj_send_event(password_field, LV_EVENT_CLICKED, NULL);
    }
    else {
        lv_textarea_set_text(password_field, "");
    }
}

static void scan_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    sp_scan();
}

static void connect_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    sp_wifi_connect(lv_textarea_get_text(ssid_field), lv_textarea_get_text(password_field));
}
