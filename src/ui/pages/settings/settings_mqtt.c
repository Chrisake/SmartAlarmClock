/**
 * @file settings_mqtt.c
 *
 * MQTT tab: the broker and the credentials for it, in two columns, then the
 * connection status beside Save & connect. Nothing is sent until that button,
 * so a half-typed host never drops a working connection.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/settings_private.h"

#include <stdlib.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * field_place(lv_obj_t * grid, lv_obj_t * box, uint8_t col, uint8_t row);
static void       save_clicked(lv_event_t * e);
static void       tls_changed(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const char * const status_text[] = {
    [PAGE_SETTINGS_LINK_IDLE]      = "Not connected",
    [PAGE_SETTINGS_LINK_BUSY]      = "Connecting...",
    [PAGE_SETTINGS_LINK_CONNECTED] = "Connected",
    [PAGE_SETTINGS_LINK_FAILED]    = "Could not connect",
};

static lv_obj_t * host_field;
static lv_obj_t * port_field;
static lv_obj_t * username_field;
static lv_obj_t * password_field;
static lv_obj_t * client_id_field;
static lv_obj_t * topic_field;
static lv_obj_t * tls_switch;
static lv_obj_t * status;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sp_mqtt_create(lv_obj_t * tab)
{
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT,
                                   LV_GRID_TEMPLATE_LAST};

    lv_obj_t * card = sp_card_create(tab);

    lv_obj_t * grid = sp_box_create(card);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_style_pad_row(grid, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, UI_GAP * 2, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(grid, cols, rows);

    lv_obj_t * box;

    /*Host and port share a row, the port kept short.*/
    lv_obj_t * address = sp_box_create(grid);
    lv_obj_set_grid_cell(address, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 0, 1);
    lv_obj_set_flex_flow(address, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(address, UI_GAP, LV_PART_MAIN);

    box = sp_field_create(address, "Broker", "broker.local", SP_FIELD_TEXT, &host_field);
    lv_obj_set_width(box, 0);
    lv_obj_set_flex_grow(box, 1);
    lv_textarea_set_max_length(host_field, SETTINGS_HOST_LEN - 1);

    box = sp_field_create(address, "Port", "1883", SP_FIELD_NUMBER, &port_field);
    lv_obj_set_width(box, 96);

    box = sp_field_create(grid, "Client ID", "smartclock", SP_FIELD_TEXT, &client_id_field);
    field_place(grid, box, 1, 0);
    lv_textarea_set_max_length(client_id_field, SETTINGS_CLIENT_ID_LEN - 1);

    box = sp_field_create(grid, "Username", "Optional", SP_FIELD_TEXT, &username_field);
    field_place(grid, box, 0, 1);
    lv_textarea_set_max_length(username_field, SETTINGS_CRED_LEN - 1);

    box = sp_field_create(grid, "Password", "Optional", SP_FIELD_PASSWORD, &password_field);
    field_place(grid, box, 1, 1);
    lv_textarea_set_max_length(password_field, SETTINGS_CRED_LEN - 1);

    box = sp_field_create(grid, "Device configuration topic", "smartclock/config/devices", SP_FIELD_TEXT,
                          &topic_field);
    field_place(grid, box, 0, 2);
    lv_textarea_set_max_length(topic_field, SETTINGS_TOPIC_LEN - 1);

    lv_obj_t * tls = sp_row_create(grid, "Encrypt with TLS");
    lv_obj_set_grid_cell(tls, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_END, 2, 1);
    tls_switch = sp_switch_create(tls, tls_changed);

    /*Status, and the one button that sends anything.*/
    lv_obj_t * footer = sp_box_create(card);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_style_pad_top(footer, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(footer, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    status = sp_status_create(footer, LV_SYMBOL_DRIVE);
    lv_obj_set_width(status, 0);
    lv_obj_set_flex_grow(status, 1);

    lv_obj_t * save = sp_button_create(footer, "Save & connect", true);
    lv_obj_add_event_cb(save, save_clicked, LV_EVENT_CLICKED, NULL);
}

void sp_mqtt_values(void)
{
    char port[8];
    lv_snprintf(port, sizeof(port), "%u", (unsigned)sp_values.mqtt_port);

    sp_field_set(host_field, sp_values.mqtt_host);
    sp_field_set(port_field, port);
    sp_field_set(username_field, sp_values.mqtt_username);
    sp_field_set(password_field, sp_values.mqtt_password);
    sp_field_set(client_id_field, sp_values.mqtt_client_id);
    sp_field_set(topic_field, sp_values.mqtt_config_topic);
    sp_switch_set(tls_switch, sp_values.mqtt_tls);
}

void sp_mqtt_status(page_settings_link_t link, const char * detail)
{
    sp_status_set(status, link, detail, status_text);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * field_place(lv_obj_t * grid, lv_obj_t * box, uint8_t col, uint8_t row)
{
    LV_UNUSED(grid);
    lv_obj_set_grid_cell(box, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_START, row, 1);
    return box;
}

static void tls_changed(lv_event_t * e)
{
    /*Like the fields, only taken when saved.*/
    LV_UNUSED(e);
}

static void save_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    lv_strlcpy(sp_values.mqtt_host, lv_textarea_get_text(host_field), sizeof(sp_values.mqtt_host));
    lv_strlcpy(sp_values.mqtt_username, lv_textarea_get_text(username_field), sizeof(sp_values.mqtt_username));
    lv_strlcpy(sp_values.mqtt_password, lv_textarea_get_text(password_field), sizeof(sp_values.mqtt_password));
    lv_strlcpy(sp_values.mqtt_client_id, lv_textarea_get_text(client_id_field), sizeof(sp_values.mqtt_client_id));
    lv_strlcpy(sp_values.mqtt_config_topic, lv_textarea_get_text(topic_field), sizeof(sp_values.mqtt_config_topic));
    sp_values.mqtt_tls = lv_obj_has_state(tls_switch, LV_STATE_CHECKED);

    /*A port out of range keeps the one there was.*/
    long port = strtol(lv_textarea_get_text(port_field), NULL, 10);
    if(port >= 1 && port <= 65535) sp_values.mqtt_port = (uint16_t)port;

    sp_mqtt_save();
}
