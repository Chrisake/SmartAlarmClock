/**
 * @file page_settings.h
 *
 * Settings page: how the clock connects, keeps time, and looks and behaves.
 *
 * Layout:
 *
 *   +--------------------------------------------------------------+
 *   |  [ Wi-Fi ]  [ MQTT ]  [ Date & time ]  [ Device ]             |
 *   +--------------------------------------------------------------+
 *   |  Wi-Fi:  status             |  network list, Scan            |
 *   |          SSID / password    |  (tap one to fill the SSID)    |
 *   |          [Connect]          |                                |
 *   |                                                              |
 *   |  MQTT:   host, port, TLS, username, password, client id,     |
 *   |          configuration topic, [Save & connect], status        |
 *   |                                                              |
 *   |  Date & time: set automatically, time server, date, time,    |
 *   |          automatic time zone, time zone | 24-hour clock,     |
 *   |          date format, seconds                                |
 *   |                                                              |
 *   |  Device: brightness and idle brightness (each automatic or   |
 *   |          a level), theme, accent, ambient clock after,       |
 *   |          language, temperature unit                          |
 *   +--------------------------------------------------------------+
 *
 * Device and time settings take effect as they are changed. Wi-Fi and MQTT
 * credentials only go out when their Connect / Save button is pressed, so a
 * half-typed password never knocks the clock off its network. The date and
 * time are set through a wheel picker, and only on Apply.
 *
 * Text fields bring up an on-screen keyboard over the bottom of the screen.
 *
 * Like every page, it holds no state of its own beyond what is on screen: the
 * settings feed hands it the current settings, the time, scan results and
 * connection status, and applies whatever comes back through the callbacks.
 */

#ifndef PAGE_SETTINGS_H
#define PAGE_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_page.h"
#include "settings/settings.h"

#include <time.h>

/*********************
 *      DEFINES
 *********************/

/** Networks the scan list shows. */
#define PAGE_SETTINGS_NETWORK_MAX 12

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    PAGE_SETTINGS_TAB_WIFI,
    PAGE_SETTINGS_TAB_MQTT,
    PAGE_SETTINGS_TAB_TIME,
    PAGE_SETTINGS_TAB_DEVICE,
    PAGE_SETTINGS_TAB_COUNT,
} page_settings_tab_t;

/** State of a Wi-Fi or broker connection, as the status lines show it. */
typedef enum {
    PAGE_SETTINGS_LINK_IDLE,        /**< Not configured, or not tried */
    PAGE_SETTINGS_LINK_BUSY,        /**< Connecting */
    PAGE_SETTINGS_LINK_CONNECTED,
    PAGE_SETTINGS_LINK_FAILED,
} page_settings_link_t;

/** One network from a scan. */
typedef struct {
    const char * ssid;
    int8_t       rssi;      /**< dBm */
    bool         secured;
} page_settings_network_t;

/** A device or time setting was changed; the whole set is passed, already edited. */
typedef void (*page_settings_change_cb_t)(const settings_t * settings);

/** Scan was tapped. */
typedef void (*page_settings_scan_cb_t)(void);

/** Connect was tapped on the Wi-Fi tab. */
typedef void (*page_settings_wifi_cb_t)(const char * ssid, const char * password);

/** Save & connect was tapped on the MQTT tab; the whole set is passed. */
typedef void (*page_settings_mqtt_cb_t)(const settings_t * settings);

/**
 * A date or time was applied from the picker.
 * @param local   the full local date and time to set: year, month, day, hour, minute
 */
typedef void (*page_settings_time_cb_t)(const struct tm * local);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_settings_desc(void);

/**
 * Fill every tab from a set of settings. Fields being typed into are left
 * alone.
 * @param settings   copied
 */
void page_settings_set_values(const settings_t * settings);

/**
 * Show the device's current date and time on the Date & time tab.
 * @param local   copied
 */
void page_settings_set_now(const struct tm * local);

/**
 * Replace the network list.
 * @param networks   strongest first; SSIDs are copied
 * @param count      clamped to PAGE_SETTINGS_NETWORK_MAX
 */
void page_settings_set_networks(const page_settings_network_t networks[], uint32_t count);

/** Show or hide the scan in progress. */
void page_settings_set_scanning(bool scanning);

/**
 * @param link     connection state
 * @param detail   e.g. "Connected to Home - 192.168.1.42"; NULL for a default per state
 */
void page_settings_set_wifi_status(page_settings_link_t link, const char * detail);

/**
 * @param link     connection state
 * @param detail   e.g. "Connected to broker.local"; NULL for a default per state
 */
void page_settings_set_mqtt_status(page_settings_link_t link, const char * detail);

/** @param cb   called on every device or time setting change; NULL to clear */
void page_settings_set_change_cb(page_settings_change_cb_t cb);

/** @param cb   called when Scan is tapped; NULL to clear */
void page_settings_set_scan_cb(page_settings_scan_cb_t cb);

/** @param cb   called when Connect is tapped; NULL to clear */
void page_settings_set_wifi_cb(page_settings_wifi_cb_t cb);

/** @param cb   called when Save & connect is tapped; NULL to clear */
void page_settings_set_mqtt_cb(page_settings_mqtt_cb_t cb);

/** @param cb   called when a date or time is applied; NULL to clear */
void page_settings_set_time_cb(page_settings_time_cb_t cb);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_SETTINGS_H*/
