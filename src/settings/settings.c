/**
 * @file settings.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "settings/settings.h"

#include "cJSON.h"

#include <string.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void read_string(const cJSON * obj, const char * key, char * dst, size_t size);
static void read_bool(const cJSON * obj, const char * key, bool * dst);
static bool read_int(const cJSON * obj, const char * key, int min, int max, int * out);
static bool read_double(const cJSON * obj, const char * key, double min, double max, double * out);

/**********************
 *  STATIC VARIABLES
 **********************/

static const char * const date_format_names[SETTINGS_DATE_COUNT] = {"dmy", "mdy", "ymd"};
static const char * const keyboard_codes[SETTINGS_KEYBOARD_COUNT] = {"en", "el", "de", "fr", "es", "ru", "uk"};

static settings_t current;
static bool       current_set;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void settings_defaults(settings_t * s)
{
    memset(s, 0, sizeof(*s));

    s->mqtt_port = 1883;
    strcpy(s->mqtt_client_id, "smartclock");
    strcpy(s->mqtt_config_topic, "smartclock/config/devices");

    s->brightness_auto      = true;
    s->brightness           = 70;
    s->always_on            = true;
    s->idle_brightness_auto = true;
    s->idle_brightness      = 15;
    s->ambient_timeout      = 240;
    s->theme                = SETTINGS_THEME_DARK;
    s->accent               = 0;
    s->face_wake            = false;
    s->face_wake_fps        = SETTINGS_FACE_WAKE_FPS_LOW;
    s->face_wake_frames     = 4;
    s->radar_wake           = true;
    s->radar_sensitivity    = 50;
    s->radar_dark           = SETTINGS_RADAR_DARK_REDUCED;

    s->time_auto     = true;
    strcpy(s->time_server, "pool.ntp.org");
    s->timezone_auto = true;
    strcpy(s->timezone, "UTC");
    strcpy(s->timezone_posix, "UTC0");
    s->clock_24h     = false;
    s->show_seconds  = true;
    s->date_format   = SETTINGS_DATE_DMY;

    strcpy(s->language, "en");
    s->fahrenheit = false;
    s->keyboards  = 1U << SETTINGS_KEYBOARD_EN;

    s->location_auto = true;

    s->alarm_snooze_minutes = 9;
    s->alarm_volume         = 80;
    s->alarm_ramp_seconds   = 30;

    s->sensor_interval  = 60;
    strcpy(s->sensor_topic, "smartclock/sensors");
    s->sensor_discovery = true;
}

bool settings_from_json(const char * json, size_t len, settings_t * out)
{
    settings_defaults(out);

    cJSON * root = cJSON_ParseWithLength(json, len);
    if(!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    int value;

    const cJSON * wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    read_string(wifi, "ssid", out->wifi_ssid, sizeof(out->wifi_ssid));
    read_string(wifi, "password", out->wifi_password, sizeof(out->wifi_password));

    const cJSON * mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    read_string(mqtt, "host", out->mqtt_host, sizeof(out->mqtt_host));
    if(read_int(mqtt, "port", 1, 65535, &value)) out->mqtt_port = (uint16_t)value;
    read_bool(mqtt, "tls", &out->mqtt_tls);
    read_string(mqtt, "username", out->mqtt_username, sizeof(out->mqtt_username));
    read_string(mqtt, "password", out->mqtt_password, sizeof(out->mqtt_password));
    read_string(mqtt, "client_id", out->mqtt_client_id, sizeof(out->mqtt_client_id));
    read_string(mqtt, "config_topic", out->mqtt_config_topic, sizeof(out->mqtt_config_topic));

    const cJSON * display = cJSON_GetObjectItemCaseSensitive(root, "display");
    read_bool(display, "brightness_auto", &out->brightness_auto);
    if(read_int(display, "brightness", SETTINGS_BRIGHTNESS_MIN, 100, &value)) out->brightness = (uint8_t)value;
    read_bool(display, "always_on", &out->always_on);
    read_bool(display, "idle_brightness_auto", &out->idle_brightness_auto);
    /*A brighter level from before the limit comes down to it.*/
    if(read_int(display, "idle_brightness", SETTINGS_BRIGHTNESS_MIN, 100, &value)) {
        out->idle_brightness = (uint8_t)(value > SETTINGS_IDLE_BRIGHTNESS_MAX ? SETTINGS_IDLE_BRIGHTNESS_MAX : value);
    }
    if(read_int(display, "ambient_timeout", 0, 3600, &value)) out->ambient_timeout = (uint16_t)value;
    if(read_int(display, "accent", 0, SETTINGS_ACCENT_COUNT - 1, &value)) out->accent = (uint8_t)value;

    read_bool(display, "face_wake", &out->face_wake);
    /*Whichever of the two rates is nearer.*/
    if(read_int(display, "face_wake_fps", SETTINGS_FACE_WAKE_FPS_LOW, SETTINGS_FACE_WAKE_FPS_HIGH, &value)) {
        out->face_wake_fps = value * 2 >= SETTINGS_FACE_WAKE_FPS_LOW + SETTINGS_FACE_WAKE_FPS_HIGH
                             ? SETTINGS_FACE_WAKE_FPS_HIGH : SETTINGS_FACE_WAKE_FPS_LOW;
    }
    if(read_int(display, "face_wake_frames", SETTINGS_FACE_WAKE_FRAMES_MIN, SETTINGS_FACE_WAKE_FRAMES_MAX, &value)) {
        out->face_wake_frames = (uint8_t)value;
    }

    read_bool(display, "radar_wake", &out->radar_wake);
    if(read_int(display, "radar_sensitivity", SETTINGS_RADAR_SENSITIVITY_MIN, SETTINGS_RADAR_SENSITIVITY_MAX,
                &value)) {
        out->radar_sensitivity = (uint8_t)value;
    }

    const cJSON * radar_dark = display ? cJSON_GetObjectItemCaseSensitive(display, "radar_dark") : NULL;
    if(cJSON_IsString(radar_dark)) {
        if(strcmp(radar_dark->valuestring, "on") == 0)       out->radar_dark = SETTINGS_RADAR_DARK_ON;
        else if(strcmp(radar_dark->valuestring, "off") == 0) out->radar_dark = SETTINGS_RADAR_DARK_OFF;
        else                                                 out->radar_dark = SETTINGS_RADAR_DARK_REDUCED;
    }

    const cJSON * theme = display ? cJSON_GetObjectItemCaseSensitive(display, "theme") : NULL;
    if(cJSON_IsString(theme)) {
        if(strcmp(theme->valuestring, "light") == 0)     out->theme = SETTINGS_THEME_LIGHT;
        else if(strcmp(theme->valuestring, "dark") == 0) out->theme = SETTINGS_THEME_DARK;
        else if(strcmp(theme->valuestring, "auto") == 0) out->theme = SETTINGS_THEME_AUTO;
    }

    const cJSON * general = cJSON_GetObjectItemCaseSensitive(root, "general");
    read_string(general, "language", out->language, sizeof(out->language));
    read_bool(general, "fahrenheit", &out->fahrenheit);

    /*Codes this build does not know are skipped. English is always kept.*/
    const cJSON * keyboards = cJSON_GetObjectItemCaseSensitive(general, "keyboards");
    if(cJSON_IsArray(keyboards)) {
        const cJSON * code;
        out->keyboards = 1U << SETTINGS_KEYBOARD_EN;
        cJSON_ArrayForEach(code, keyboards) {
            if(!cJSON_IsString(code)) continue;
            for(uint32_t i = 0; i < SETTINGS_KEYBOARD_COUNT; i++) {
                if(strcmp(code->valuestring, keyboard_codes[i]) == 0) out->keyboards |= (uint16_t)(1U << i);
            }
        }
    }

    /*Where the clock format lived before it had a "time" section of its own.*/
    read_bool(general, "clock_24h", &out->clock_24h);
    read_bool(general, "show_seconds", &out->show_seconds);

    const cJSON * time = cJSON_GetObjectItemCaseSensitive(root, "time");
    read_bool(time, "auto", &out->time_auto);
    read_string(time, "server", out->time_server, sizeof(out->time_server));
    read_bool(time, "timezone_auto", &out->timezone_auto);
    read_string(time, "timezone", out->timezone, sizeof(out->timezone));
    read_string(time, "timezone_posix", out->timezone_posix, sizeof(out->timezone_posix));
    read_bool(time, "clock_24h", &out->clock_24h);
    read_bool(time, "show_seconds", &out->show_seconds);

    const cJSON * format = time ? cJSON_GetObjectItemCaseSensitive(time, "date_format") : NULL;
    for(int i = 0; cJSON_IsString(format) && i < SETTINGS_DATE_COUNT; i++) {
        if(strcmp(format->valuestring, date_format_names[i]) == 0) out->date_format = (settings_date_format_t)i;
    }

    double number;

    const cJSON * location = cJSON_GetObjectItemCaseSensitive(root, "location");
    read_bool(location, "auto", &out->location_auto);
    read_string(location, "name", out->location_name, sizeof(out->location_name));
    if(read_double(location, "latitude", -90.0, 90.0, &number))    out->latitude = number;
    if(read_double(location, "longitude", -180.0, 180.0, &number)) out->longitude = number;

    /*A city without a name or with coordinates out of range is skipped.*/
    const cJSON * places = location ? cJSON_GetObjectItemCaseSensitive(location, "places") : NULL;
    if(cJSON_IsArray(places)) {
        const cJSON * entry;
        cJSON_ArrayForEach(entry, places) {
            if(out->place_count >= SETTINGS_PLACES_MAX) break;

            settings_place_t place = {0};
            read_string(entry, "name", place.name, sizeof(place.name));
            if(place.name[0] == '\0') continue;
            if(!read_double(entry, "latitude", -90.0, 90.0, &place.latitude)) continue;
            if(!read_double(entry, "longitude", -180.0, 180.0, &place.longitude)) continue;

            out->places[out->place_count++] = place;
        }
    }

    const cJSON * alarms = cJSON_GetObjectItemCaseSensitive(root, "alarms");
    if(read_int(alarms, "snooze_minutes", 1, 30, &value)) out->alarm_snooze_minutes = (uint8_t)value;
    if(read_int(alarms, "volume", 10, 100, &value))       out->alarm_volume = (uint8_t)value;
    if(read_int(alarms, "ramp_seconds", 0, 300, &value))  out->alarm_ramp_seconds = (uint16_t)value;

    const cJSON * sensors = cJSON_GetObjectItemCaseSensitive(root, "sensors");
    if(read_int(sensors, "publish_interval", 10, 3600, &value)) out->sensor_interval = (uint16_t)value;
    if(read_double(sensors, "temperature_offset", -10.0, 10.0, &number)) {
        out->sensor_temp_offset = (int16_t)(number * 10.0 + (number < 0 ? -0.5 : 0.5));
    }
    read_string(sensors, "topic", out->sensor_topic, sizeof(out->sensor_topic));
    read_bool(sensors, "discovery", &out->sensor_discovery);

    cJSON_Delete(root);
    return true;
}

char * settings_to_json(const settings_t * s)
{
    cJSON * root = cJSON_CreateObject();
    if(!root) return NULL;

    cJSON * wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", s->wifi_ssid);
    cJSON_AddStringToObject(wifi, "password", s->wifi_password);

    cJSON * mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddStringToObject(mqtt, "host", s->mqtt_host);
    cJSON_AddNumberToObject(mqtt, "port", s->mqtt_port);
    cJSON_AddBoolToObject(mqtt, "tls", s->mqtt_tls);
    cJSON_AddStringToObject(mqtt, "username", s->mqtt_username);
    cJSON_AddStringToObject(mqtt, "password", s->mqtt_password);
    cJSON_AddStringToObject(mqtt, "client_id", s->mqtt_client_id);
    cJSON_AddStringToObject(mqtt, "config_topic", s->mqtt_config_topic);

    cJSON * display = cJSON_AddObjectToObject(root, "display");
    cJSON_AddBoolToObject(display, "brightness_auto", s->brightness_auto);
    cJSON_AddNumberToObject(display, "brightness", s->brightness);
    cJSON_AddBoolToObject(display, "always_on", s->always_on);
    cJSON_AddBoolToObject(display, "idle_brightness_auto", s->idle_brightness_auto);
    cJSON_AddNumberToObject(display, "idle_brightness", s->idle_brightness);
    cJSON_AddNumberToObject(display, "ambient_timeout", s->ambient_timeout);
    cJSON_AddStringToObject(display, "theme", s->theme == SETTINGS_THEME_LIGHT ? "light"
                                              : s->theme == SETTINGS_THEME_AUTO ? "auto" : "dark");
    cJSON_AddNumberToObject(display, "accent", s->accent);
    cJSON_AddBoolToObject(display, "face_wake", s->face_wake);
    cJSON_AddNumberToObject(display, "face_wake_fps", s->face_wake_fps);
    cJSON_AddNumberToObject(display, "face_wake_frames", s->face_wake_frames);
    cJSON_AddBoolToObject(display, "radar_wake", s->radar_wake);
    cJSON_AddNumberToObject(display, "radar_sensitivity", s->radar_sensitivity);
    cJSON_AddStringToObject(display, "radar_dark", s->radar_dark == SETTINGS_RADAR_DARK_ON ? "on"
                                                   : s->radar_dark == SETTINGS_RADAR_DARK_OFF ? "off" : "reduced");

    cJSON * time = cJSON_AddObjectToObject(root, "time");
    cJSON_AddBoolToObject(time, "auto", s->time_auto);
    cJSON_AddStringToObject(time, "server", s->time_server);
    cJSON_AddBoolToObject(time, "timezone_auto", s->timezone_auto);
    cJSON_AddStringToObject(time, "timezone", s->timezone);
    cJSON_AddStringToObject(time, "timezone_posix", s->timezone_posix);
    cJSON_AddBoolToObject(time, "clock_24h", s->clock_24h);
    cJSON_AddBoolToObject(time, "show_seconds", s->show_seconds);
    cJSON_AddStringToObject(time, "date_format",
                            date_format_names[s->date_format < SETTINGS_DATE_COUNT ? s->date_format : 0]);

    cJSON * general = cJSON_AddObjectToObject(root, "general");
    cJSON_AddStringToObject(general, "language", s->language);
    cJSON_AddBoolToObject(general, "fahrenheit", s->fahrenheit);

    cJSON * keyboards = cJSON_AddArrayToObject(general, "keyboards");
    for(uint32_t i = 0; keyboards && i < SETTINGS_KEYBOARD_COUNT; i++) {
        if(s->keyboards & (1U << i)) cJSON_AddItemToArray(keyboards, cJSON_CreateString(keyboard_codes[i]));
    }

    cJSON * location = cJSON_AddObjectToObject(root, "location");
    cJSON_AddBoolToObject(location, "auto", s->location_auto);
    cJSON_AddStringToObject(location, "name", s->location_name);
    cJSON_AddNumberToObject(location, "latitude", s->latitude);
    cJSON_AddNumberToObject(location, "longitude", s->longitude);

    cJSON * places = cJSON_AddArrayToObject(location, "places");
    for(uint32_t i = 0; places && i < s->place_count && i < SETTINGS_PLACES_MAX; i++) {
        cJSON * place = cJSON_CreateObject();
        if(!place) break;
        cJSON_AddStringToObject(place, "name", s->places[i].name);
        cJSON_AddNumberToObject(place, "latitude", s->places[i].latitude);
        cJSON_AddNumberToObject(place, "longitude", s->places[i].longitude);
        cJSON_AddItemToArray(places, place);
    }

    cJSON * alarms = cJSON_AddObjectToObject(root, "alarms");
    cJSON_AddNumberToObject(alarms, "snooze_minutes", s->alarm_snooze_minutes);
    cJSON_AddNumberToObject(alarms, "volume", s->alarm_volume);
    cJSON_AddNumberToObject(alarms, "ramp_seconds", s->alarm_ramp_seconds);

    cJSON * sensors = cJSON_AddObjectToObject(root, "sensors");
    cJSON_AddNumberToObject(sensors, "publish_interval", s->sensor_interval);
    cJSON_AddNumberToObject(sensors, "temperature_offset", s->sensor_temp_offset / 10.0);
    cJSON_AddStringToObject(sensors, "topic", s->sensor_topic);
    cJSON_AddBoolToObject(sensors, "discovery", s->sensor_discovery);

    char * text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

void settings_json_free(char * json)
{
    cJSON_free(json);
}

const settings_t * settings_get(void)
{
    if(!current_set) {
        settings_defaults(&current);
        current_set = true;
    }
    return &current;
}

void settings_set(const settings_t * s)
{
    current     = *s;
    current_set = true;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Copy a string that fits; leave the default for one that does not. */
static void read_string(const cJSON * obj, const char * key, char * dst, size_t size)
{
    const cJSON * item = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if(!cJSON_IsString(item) || strlen(item->valuestring) >= size) return;
    strcpy(dst, item->valuestring);
}

static void read_bool(const cJSON * obj, const char * key, bool * dst)
{
    const cJSON * item = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if(cJSON_IsBool(item)) *dst = cJSON_IsTrue(item);
}

static bool read_int(const cJSON * obj, const char * key, int min, int max, int * out)
{
    const cJSON * item = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if(!cJSON_IsNumber(item) || item->valuedouble < min || item->valuedouble > max) return false;
    *out = item->valueint;
    return true;
}

static bool read_double(const cJSON * obj, const char * key, double min, double max, double * out)
{
    const cJSON * item = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if(!cJSON_IsNumber(item) || item->valuedouble < min || item->valuedouble > max) return false;
    *out = item->valuedouble;
    return true;
}
