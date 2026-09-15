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
 *     "display": { "brightness_auto": true, "brightness": 70, "always_on": true,
 *                  "idle_brightness_auto": true, "idle_brightness": 15,
 *                  "ambient_timeout": 240, "theme": "auto", "accent": 0,
 *                  "face_wake": false, "face_wake_fps": 5, "face_wake_frames": 4 },
 *     "time":    { "auto": true, "server": "pool.ntp.org",
 *                  "timezone_auto": true, "timezone": "Europe/Athens",
 *                  "timezone_posix": "EET-2EEST,M3.5.0/3,M10.5.0/4",
 *                  "clock_24h": true, "show_seconds": true, "date_format": "dmy" },
 *     "general": { "language": "en", "fahrenheit": false, "keyboards": ["en", "el"] },
 *     "location": { "auto": true, "name": "Athens", "latitude": 37.98, "longitude": 23.73,
 *                   "places": [ { "name": "Paris", "latitude": 48.85, "longitude": 2.35 } ] },
 *     "alarms":  { "snooze_minutes": 9, "volume": 80, "ramp_seconds": 30 },
 *     "sensors": { "publish_interval": 60, "temperature_offset": -1.5,
 *                  "topic": "smartclock/sensors", "discovery": true }
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
#define SETTINGS_LOCATION_LEN  48   /**< Place name shown on the forecasts */

/** Cities besides the clock's own that the weather page can be switched to. */
#define SETTINGS_PLACES_MAX    6

/** Accent colours the UI offers; ui_theme.c names them, in this many. */
#define SETTINGS_ACCENT_COUNT  8

/** Lowest brightness or sensitivity, so the screen can never be turned black by accident. */
#define SETTINGS_BRIGHTNESS_MIN 5

/** Highest always-on brightness: the ambient face is for a dark room. */
#define SETTINGS_IDLE_BRIGHTNESS_MAX 20

/** Face wake: the frames looked at a second on offer, and how many in a row
 *  must hold a face to wake the screen. */
#define SETTINGS_FACE_WAKE_FPS_LOW    5
#define SETTINGS_FACE_WAKE_FPS_HIGH   10
#define SETTINGS_FACE_WAKE_FRAMES_MIN 3
#define SETTINGS_FACE_WAKE_FRAMES_MAX 7

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    SETTINGS_THEME_DARK,
    SETTINGS_THEME_LIGHT,
    SETTINGS_THEME_AUTO,   /**< Light from sunrise to sunset, dark the rest of the day */
} settings_theme_t;

/** Order of day, month and year in dates. */
typedef enum {
    SETTINGS_DATE_DMY,   /**< 14/09/2026, Monday 14 September */
    SETTINGS_DATE_MDY,   /**< 09/14/2026, Monday September 14 */
    SETTINGS_DATE_YMD,   /**< 2026-09-14 */
    SETTINGS_DATE_COUNT,
} settings_date_format_t;

/** Input languages the keyboard offers, as bits in settings_t::keyboards.
 *  Stored by code: "en", "el", "de", "fr", "es", "ru", "uk". */
typedef enum {
    SETTINGS_KEYBOARD_EN,   /**< English, always on */
    SETTINGS_KEYBOARD_EL,   /**< Greek */
    SETTINGS_KEYBOARD_DE,   /**< German */
    SETTINGS_KEYBOARD_FR,   /**< French */
    SETTINGS_KEYBOARD_ES,   /**< Spanish */
    SETTINGS_KEYBOARD_RU,   /**< Russian */
    SETTINGS_KEYBOARD_UK,   /**< Ukrainian */
    SETTINGS_KEYBOARD_COUNT,
} settings_keyboard_t;

/** A city the forecasts can be for. */
typedef struct {
    char   name[SETTINGS_LOCATION_LEN];
    double latitude;    /**< Degrees north */
    double longitude;   /**< Degrees east */
} settings_place_t;

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
    bool             always_on;              /**< Idle shows the ambient clock; off, idle turns the screen off */
    bool             idle_brightness_auto;
    uint8_t          idle_brightness;        /**< The same, for the ambient clock; at most SETTINGS_IDLE_BRIGHTNESS_MAX */
    uint16_t         ambient_timeout;        /**< Seconds of no touch before idle; 0 = never */
    settings_theme_t theme;
    uint8_t          accent;                 /**< Index, below SETTINGS_ACCENT_COUNT */
    bool             face_wake;              /**< While idle the camera looks for a face, and one wakes the screen */
    uint8_t          face_wake_fps;          /**< Frames looked at a second: SETTINGS_FACE_WAKE_FPS_LOW or _HIGH */
    uint8_t          face_wake_frames;       /**< Frames in a row with a face that wake it */

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
    char     language[SETTINGS_LANGUAGE_LEN];   /**< "en"; the only one there is so far */
    bool     fahrenheit;
    uint16_t keyboards;                         /**< Bit per settings_keyboard_t; English's always set */

    /*Where the forecasts are for. Automatic takes the public IP's location;
     *otherwise the coordinates below, shown under the name beside them.*/
    bool   location_auto;
    char   location_name[SETTINGS_LOCATION_LEN];
    double latitude;    /**< Degrees north */
    double longitude;   /**< Degrees east */

    /*More cities the weather page can be switched to, in the order added. The
     *clock page, and everything else, keeps to the location above.*/
    settings_place_t places[SETTINGS_PLACES_MAX];
    uint8_t          place_count;

    /*Alarms*/
    uint8_t  alarm_snooze_minutes;   /**< 1..30 */
    uint8_t  alarm_volume;           /**< Percent the alarm rises to */
    uint16_t alarm_ramp_seconds;     /**< How long it takes to get there; 0 starts at full volume */

    /*The on-board sensors*/
    uint16_t sensor_interval;                    /**< Seconds between readings sent to the broker */
    int16_t  sensor_temp_offset;                 /**< Tenths of a degree C added to the SHT45, which the case warms */
    char     sensor_topic[SETTINGS_TOPIC_LEN];   /**< Where the readings are published */
    bool     sensor_discovery;                   /**< Announce them to Home Assistant */
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
