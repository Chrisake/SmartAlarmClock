/**
 * @file settings.h
 *
 * The device's own configuration: how it reaches the network and the MQTT
 * broker, how it keeps time, and how it looks and behaves.
 *
 * Plain C with no LVGL, like the device model. The settings page edits a copy
 * and hands it back; ui_settings_feed.c applies and stores it. Stored as JSON
 * so the same code serves the simulator (a file) and the clock (a blob in
 * NVS), and so a settings file can be read and fixed by hand.
 *
 *   {
 *     "wifi":    { "ssid": "Home", "password": "..." },
 *     "mqtt":    { "host": "broker.local", "port": 1883, "tls": false,
 *                  "username": "", "password": "", "client_id": "smartclock",
 *                  "config_topic": "smartclock/config/devices" },
 *     "display": { "brightness_auto": true, "brightness": 70,
 *                  "idle_brightness_auto": true, "idle_brightness": 30,
 *                  "ambient_timeout": 240, "theme": "dark", "accent": 0 },
 *     "time":    { "auto": true, "server": "pool.ntp.org",
 *                  "timezone_auto": true, "timezone": "Europe/Athens",
 *                  "timezone_posix": "EET-2EEST,M3.5.0/3,M10.5.0/4",
 *                  "clock_24h": true, "show_seconds": true, "date_format": "dmy" },
 *     "general": { "language": "en", "fahrenheit": false }
 *   }
 *
 * Anything missing or out of range takes its default, so an old or partial
 * file still loads.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

#define SETTINGS_SSID_LEN      33   /**< 32 bytes, the 802.11 maximum, plus NUL */
#define SETTINGS_WIFI_PASS_LEN 65   /**< 64, the WPA2 maximum, plus NUL */
#define SETTINGS_HOST_LEN      64
#define SETTINGS_CRED_LEN      64
#define SETTINGS_CLIENT_ID_LEN 33
#define SETTINGS_TOPIC_LEN     96
#define SETTINGS_LANGUAGE_LEN  8
#define SETTINGS_ZONE_LEN      48   /**< Time zone name, and its POSIX string */

/** Accent colours the UI offers; ui_theme.c names them, in this many. */
#define SETTINGS_ACCENT_COUNT  8

/** Lowest brightness or sensitivity, so the screen can never be turned black by accident. */
#define SETTINGS_BRIGHTNESS_MIN 5

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    SETTINGS_THEME_DARK,
    SETTINGS_THEME_LIGHT,
} settings_theme_t;

/** Order of day, month and year in dates. */
typedef enum {
    SETTINGS_DATE_DMY,   /**< 14/09/2026, Monday 14 September */
    SETTINGS_DATE_MDY,   /**< 09/14/2026, Monday September 14 */
    SETTINGS_DATE_YMD,   /**< 2026-09-14 */
    SETTINGS_DATE_COUNT,
} settings_date_format_t;

typedef struct {
    /*Wi-Fi*/
    char wifi_ssid[SETTINGS_SSID_LEN];
    char wifi_password[SETTINGS_WIFI_PASS_LEN];

    /*MQTT*/
    char     mqtt_host[SETTINGS_HOST_LEN];
    uint16_t mqtt_port;
    bool     mqtt_tls;
    char     mqtt_username[SETTINGS_CRED_LEN];
    char     mqtt_password[SETTINGS_CRED_LEN];
    char     mqtt_client_id[SETTINGS_CLIENT_ID_LEN];
    char     mqtt_config_topic[SETTINGS_TOPIC_LEN];   /**< Retained device configuration */

    /*Display. Brightness is the backlight's alone; nothing on screen changes
     *with it. With automatic on, the level is the light sensor's sensitivity.*/
    bool             brightness_auto;
    uint8_t          brightness;             /**< Percent: the level, or the sensitivity when automatic */
    bool             idle_brightness_auto;
    uint8_t          idle_brightness;        /**< The same, for the ambient clock face */
    uint16_t         ambient_timeout;        /**< Seconds of no touch before the ambient clock; 0 = never */
    settings_theme_t theme;
    uint8_t          accent;                 /**< Index, below SETTINGS_ACCENT_COUNT */

    /*Time*/
    bool                   time_auto;                           /**< From the time server; otherwise set by hand */
    char                   time_server[SETTINGS_HOST_LEN];
    bool                   timezone_auto;                       /**< From the location of the public IP */
    char                   timezone[SETTINGS_ZONE_LEN];         /**< Name shown, e.g. "Europe/Athens" */
    char                   timezone_posix[SETTINGS_ZONE_LEN];   /**< Rules used, e.g. "EET-2EEST,..." */
    bool                   clock_24h;
    bool                   show_seconds;
    settings_date_format_t date_format;

    /*General*/
    char language[SETTINGS_LANGUAGE_LEN];   /**< "en"; the only one there is so far */
    bool fahrenheit;
} settings_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** Fill `s` with the out-of-the-box settings. */
void settings_defaults(settings_t * s);

/**
 * Read settings from JSON. Starts from the defaults, so missing keys keep them;
 * values of the wrong type or out of range are ignored.
 * @param json   the document; need not be NUL-terminated
 * @param len    its length
 * @param out    receives the settings
 * @return       false if the text is not a JSON object at all (`out` then holds the defaults)
 */
bool settings_from_json(const char * json, size_t len, settings_t * out);

/**
 * Write settings as indented JSON.
 * @return   a string to release with settings_json_free(), or NULL if out of memory
 */
char * settings_to_json(const settings_t * s);

/** Release a string from settings_to_json(). */
void settings_json_free(char * json);

/** @return   the settings in force */
const settings_t * settings_get(void);

/** Replace the settings in force. Does not store or apply them. */
void settings_set(const settings_t * s);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SETTINGS_H*/
