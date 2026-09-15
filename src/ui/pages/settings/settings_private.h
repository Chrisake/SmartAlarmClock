/**
 * @file settings_private.h
 *
 * Shared between the files the settings page is split across:
 * page_settings.c (the tabs, the keyboard and the building blocks every tab
 * uses) and one file per tab. Not for use outside the page.
 */

#ifndef SETTINGS_PRIVATE_H
#define SETTINGS_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/settings/page_settings.h"
#include "ui/ui_theme.h"

/**********************
 *      TYPEDEFS
 **********************/

/** What a text field holds, which decides its keyboard and what it accepts. */
typedef enum {
    SP_FIELD_TEXT,
    SP_FIELD_PASSWORD,
    SP_FIELD_NUMBER,
} sp_field_kind_t;

/**********************
 *  GLOBAL VARIABLES
 **********************/

/** The settings the page shows. Tabs read it to fill themselves, and edit it
 *  before handing it back. */
extern settings_t sp_values;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*page_settings.c: going back out*/

/** A device or time setting in `sp_values` changed. */
void sp_changed(void);
void sp_scan(void);
void sp_wifi_connect(const char * ssid, const char * password);
/** The MQTT fields have been read into `sp_values`; save and connect. */
void sp_mqtt_save(void);
/** A date or time was applied. */
void sp_set_time(const struct tm * local);
/** A city was searched for. */
void sp_search(page_settings_search_t search, const char * name);

/*page_settings.c: building blocks*/

/** A transparent, content-sized, non-scrolling box that lets presses through. */
lv_obj_t * sp_box_create(lv_obj_t * parent);

/** A full-width card in a tab. */
lv_obj_t * sp_card_create(lv_obj_t * parent);

/** A row with a setting's name on the left; add its control to the row. */
lv_obj_t * sp_row_create(lv_obj_t * parent, const char * name);

/** Change the name shown on a row from sp_row_create(). */
void sp_row_rename(lv_obj_t * row, const char * name);

/**
 * A text field with its name above it. Tapping it brings up the keyboard.
 * @param textarea   receives the field itself
 * @return           the box holding name and field, for the caller to place
 */
lv_obj_t * sp_field_create(lv_obj_t * parent, const char * name, const char * placeholder,
                           sp_field_kind_t kind, lv_obj_t ** textarea);

/** Set a field's text, unless the user is typing in it. */
void sp_field_set(lv_obj_t * textarea, const char * text);

lv_obj_t * sp_button_create(lv_obj_t * parent, const char * text, bool primary);

/** Grey a control out and stop it taking touches, or bring it back. */
void sp_enable(lv_obj_t * obj, bool enabled);

/** A switch; `cb` gets LV_EVENT_VALUE_CHANGED. */
lv_obj_t * sp_switch_create(lv_obj_t * parent, lv_event_cb_t cb);
void       sp_switch_set(lv_obj_t * sw, bool on);

/**
 * A segmented control: one button per option, one of them lit.
 * `cb` gets LV_EVENT_CLICKED with the option's index as user data.
 */
lv_obj_t * sp_segmented_create(lv_obj_t * parent, const char * const options[], uint32_t count,
                               lv_event_cb_t cb);
void       sp_segmented_select(lv_obj_t * segmented, uint32_t index);

/** A dropdown in the page's style; `cb` gets LV_EVENT_VALUE_CHANGED. */
lv_obj_t * sp_dropdown_create(lv_obj_t * parent, const char * options, lv_event_cb_t cb);

/** A connection status line: a glyph coloured by state, then the words. */
lv_obj_t * sp_status_create(lv_obj_t * parent, const char * glyph);

/**
 * @param defaults   wording per page_settings_link_t, used when `detail` is NULL
 */
void sp_status_set(lv_obj_t * status, page_settings_link_t link, const char * detail,
                   const char * const defaults[]);

/*settings_wifi.c*/
void sp_wifi_create(lv_obj_t * tab);
void sp_wifi_values(void);
void sp_wifi_networks(const page_settings_network_t networks[], uint32_t count);
void sp_wifi_scanning(bool scanning);
void sp_wifi_status(page_settings_link_t link, const char * detail);

/*settings_mqtt.c*/
void sp_mqtt_create(lv_obj_t * tab);
void sp_mqtt_values(void);
void sp_mqtt_status(page_settings_link_t link, const char * detail);

/*settings_time.c*/
void sp_time_create(lv_obj_t * tab);
void sp_time_values(void);
void sp_time_now(const struct tm * local);

/*settings_weather.c*/
void sp_weather_create(lv_obj_t * tab);
void sp_weather_values(void);
void sp_weather_found(page_settings_search_t search, page_settings_found_t state,
                      const page_settings_place_t places[], uint32_t count);
/** Kept whether or not the tab is built. */
void sp_weather_detected(const char * name);

/*settings_device.c*/
void sp_device_create(lv_obj_t * tab);
void sp_device_values(void);
/** Close the keyboard languages panel, if it is open. */
void sp_device_close(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SETTINGS_PRIVATE_H*/
