/**
 * @file ui_settings_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_settings_feed.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_clock_feed.h"
#include "ui/ui_devices_feed.h"
#include "ui/ui_air_feed.h"
#include "ui/ui_weather_feed.h"
#include "ui/pages/clock/page_clock.h"
#include "ui/pages/settings/page_settings.h"
#include "net/mqtt_client.h"
#include "settings/clock_time.h"
#include "settings/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** How long the pretend radio takes to scan, and to join a network or broker. */
#define SCAN_DELAY_MS    1200
#define CONNECT_DELAY_MS 1500

/** How often the backlight state and the settings page's clock are checked. */
#define TICK_MS          250

/** Largest settings file the simulator reads. */
#define SETTINGS_MAX_BYTES (16 * 1024)

/**********************
 *  STATIC PROTOTYPES
 **********************/

static char * file_read(const char * path, size_t * len);
static void   store(void);
static void   behaviour_apply(void);
static void   backlight_apply(bool force);
static bool   zone_detect(settings_t * s);

static void wifi_status(page_settings_link_t link, const char * detail);
static void mqtt_status(page_settings_link_t link, const char * detail);
static void wifi_connect_start(const char * ssid, const char * password);
static void mqtt_connect_start(void);

static void changed(const settings_t * edited);
static void scan_requested(void);
static void wifi_requested(const char * ssid, const char * password);
static void mqtt_requested(const settings_t * edited);
static void time_requested(const struct tm * local);

static void tick(lv_timer_t * timer);
static void scan_done(lv_timer_t * timer);
static void wifi_done(lv_timer_t * timer);
static void mqtt_done(lv_timer_t * timer);
static void rebuild_async(void * user);

/**********************
 *  STATIC VARIABLES
 **********************/

/*What the pretend radio finds.*/
static const page_settings_network_t sim_networks[] = {
    {"Home",               -42, true},
    {"Home-5G",            -51, true},
    {"Guest",              -63, false},
    {"Neighbour",          -71, true},
    {"PrinterDirect-4F2A", -77, false},
    {"Cafe Aroma",         -84, true},
};
#define SIM_NETWORK_COUNT (sizeof(sim_networks) / sizeof(sim_networks[0]))

/*What the settings page was last told, for ui_settings_feed_republish().*/
static uint32_t             network_count;
static bool                 scanning;
static page_settings_link_t wifi_link;
static char                 wifi_detail[96];
static page_settings_link_t mqtt_link;
static char                 mqtt_detail[96];

/*A connection attempt under way.*/
static char joining_ssid[SETTINGS_SSID_LEN];
static char joining_password[SETTINGS_WIFI_PASS_LEN];

/*The backlight as last set, so it is only touched on a change.*/
static bool    backlight_idle;
static bool    backlight_auto;
static uint8_t backlight_level;
static bool    backlight_known;

/** Minute last shown on the settings page; -1 when it needs showing again. */
static int shown_minute = -1;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_settings_feed_load(void)
{
    settings_t settings;
    size_t     len  = 0;
    char *     json = file_read(UI_SETTINGS_PATH, &len);

    /* TODO: on the clock, read the blob from NVS instead. */
    if(!json || !settings_from_json(json, len, &settings)) settings_defaults(&settings);
    free(json);

    if(settings.timezone_auto) zone_detect(&settings);

    settings_set(&settings);
    clock_time_set_zone(settings.timezone_posix);
    ui_theme_set(settings.theme == SETTINGS_THEME_LIGHT ? UI_THEME_LIGHT : UI_THEME_DARK, settings.accent);
}

void ui_settings_feed_init(void)
{
    page_settings_set_change_cb(changed);
    page_settings_set_scan_cb(scan_requested);
    page_settings_set_wifi_cb(wifi_requested);
    page_settings_set_mqtt_cb(mqtt_requested);
    page_settings_set_time_cb(time_requested);

    behaviour_apply();
    backlight_apply(true);

    wifi_link = PAGE_SETTINGS_LINK_IDLE;
    mqtt_link = PAGE_SETTINGS_LINK_IDLE;
    ui_settings_feed_republish();

    lv_timer_create(tick, TICK_MS, NULL);

    /*Join the stored network as a booting clock would; the broker follows.
     *TODO: on the clock, start SNTP against the time server once online.*/
    const settings_t * s = settings_get();
    if(s->wifi_ssid[0]) wifi_connect_start(s->wifi_ssid, s->wifi_password);
    else                mqtt_connect_start();
}

void ui_settings_feed_republish(void)
{
    struct tm now;
    clock_time_now(&now);

    page_settings_set_values(settings_get());
    page_settings_set_now(&now);
    page_settings_set_networks(sim_networks, network_count);
    page_settings_set_scanning(scanning);
    page_settings_set_wifi_status(wifi_link, wifi_detail[0] ? wifi_detail : NULL);
    page_settings_set_mqtt_status(mqtt_link, mqtt_detail[0] ? mqtt_detail : NULL);
    shown_minute = now.tm_min;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static char * file_read(const char * path, size_t * len)
{
    FILE * file = fopen(path, "rb");
    if(!file) return NULL;

    char * buf = malloc(SETTINGS_MAX_BYTES);
    size_t n   = buf ? fread(buf, 1, SETTINGS_MAX_BYTES, file) : 0;
    fclose(file);

    if(!buf || n == 0) {
        free(buf);
        return NULL;
    }

    *len = n;
    return buf;
}

/** Write the settings in force. TODO: on the clock, to NVS. */
static void store(void)
{
    char * json = settings_to_json(settings_get());
    if(!json) return;

    FILE * file = fopen(UI_SETTINGS_PATH, "wb");
    if(file) {
        fwrite(json, 1, strlen(json), file);
        fclose(file);
    }
    else {
        LV_LOG_WARN("could not write %s", UI_SETTINGS_PATH);
    }

    settings_json_free(json);
}

/** Everything a setting changes at once, without a rebuild. */
static void behaviour_apply(void)
{
    ui_set_idle_timeout((uint32_t)settings_get()->ambient_timeout * 1000U);
    backlight_apply(false);
}

/**
 * Set the backlight for what is showing: the idle brightness while the
 * ambient clock face is up, the normal one otherwise. Brightness is the
 * backlight's alone, so nothing is drawn differently.
 *
 * TODO: on the clock, drive the BSP's backlight PWM. In automatic mode, read
 * the ambient light sensor and let the level scale how strongly it moves the
 * backlight. The simulator has neither, so it only logs.
 */
static void backlight_apply(bool force)
{
    const settings_t * s         = settings_get();
    bool               idle      = page_clock_is_ambient();
    bool               automatic = idle ? s->idle_brightness_auto : s->brightness_auto;
    uint8_t            level     = idle ? s->idle_brightness : s->brightness;

    if(!force && backlight_known && idle == backlight_idle && automatic == backlight_auto &&
       level == backlight_level) return;

    backlight_known = true;
    backlight_idle  = idle;
    backlight_auto  = automatic;
    backlight_level = level;

    LV_LOG_USER("backlight: %s, %s %u%%", idle ? "idle" : "awake",
                automatic ? "automatic, sensitivity" : "level", (unsigned)level);
}

/**
 * Work out the time zone for "automatic time zone".
 *
 * TODO: on the clock, once online, ask an IP geolocation service -- e.g.
 * http://ip-api.com/json/?fields=timezone,offset -- for the IANA name, take
 * its POSIX rules from clock_zone_find(), and fall back to the plain offset
 * for a zone the table does not have.
 *
 * The simulator's stand-in is this computer's own UTC offset right now: it
 * has no daylight saving rules, but it shows the right time.
 *
 * @return   true if `s` was changed
 */
static bool zone_detect(settings_t * s)
{
    time_t    stamp = time(NULL);
    struct tm local, utc;

#if defined(_MSC_VER)
    if(localtime_s(&local, &stamp) != 0 || gmtime_s(&utc, &stamp) != 0) return false;
#else
    if(!localtime_r(&stamp, &local) || !gmtime_r(&stamp, &utc)) return false;
#endif

    /*Minutes east of UTC, allowing for the two being on different days.*/
    int day = local.tm_year != utc.tm_year ? (local.tm_year > utc.tm_year ? 1 : -1)
                                           : local.tm_yday - utc.tm_yday;
    int east = day * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);

    char name[SETTINGS_ZONE_LEN];
    char posix[SETTINGS_ZONE_LEN];
    int  hours = abs(east) / 60, minutes = abs(east) % 60;
    char sign  = east < 0 ? '-' : '+';

    lv_snprintf(name, sizeof(name), "UTC%c%02d:%02d", sign, hours, minutes);
    /*POSIX counts the other way: east of Greenwich is negative.*/
    if(minutes) lv_snprintf(posix, sizeof(posix), "<%c%02d%02d>%c%d:%02d", sign, hours, minutes,
                            east < 0 ? '+' : '-', hours, minutes);
    else        lv_snprintf(posix, sizeof(posix), "<%c%02d>%c%d", sign, hours, east < 0 ? '+' : '-', hours);

    if(strcmp(s->timezone, name) == 0 && strcmp(s->timezone_posix, posix) == 0) return false;

    lv_strlcpy(s->timezone, name, sizeof(s->timezone));
    lv_strlcpy(s->timezone_posix, posix, sizeof(s->timezone_posix));
    return true;
}

static void wifi_status(page_settings_link_t link, const char * detail)
{
    wifi_link = link;
    lv_strlcpy(wifi_detail, detail ? detail : "", sizeof(wifi_detail));
    page_settings_set_wifi_status(link, detail);
}

static void mqtt_status(page_settings_link_t link, const char * detail)
{
    mqtt_link = link;
    lv_strlcpy(mqtt_detail, detail ? detail : "", sizeof(mqtt_detail));
    page_settings_set_mqtt_status(link, detail);
}

/** TODO: on the clock, esp_wifi_set_config() and esp_wifi_connect(). */
static void wifi_connect_start(const char * ssid, const char * password)
{
    char text[96];

    lv_strlcpy(joining_ssid, ssid, sizeof(joining_ssid));
    lv_strlcpy(joining_password, password, sizeof(joining_password));

    lv_snprintf(text, sizeof(text), "Connecting to %s...", ssid);
    wifi_status(PAGE_SETTINGS_LINK_BUSY, text);

    lv_timer_set_repeat_count(lv_timer_create(wifi_done, CONNECT_DELAY_MS, NULL), 1);
}

/**
 * Connect to the broker in force. The devices page's broker status follows
 * it, unless no broker is set -- then the simulator's loopback stays the
 * broker. TODO: on the clock, esp_mqtt_client_start(), then subscribe to the
 * device configuration topic.
 */
static void mqtt_connect_start(void)
{
    const settings_t * s = settings_get();
    char               text[96];

    /*However this goes, the connection there was is gone.*/
    mqtt_client_set_connected(false);

    if(s->mqtt_host[0] == '\0') {
        mqtt_status(PAGE_SETTINGS_LINK_IDLE, "No broker set");
        return;
    }

    if(wifi_link != PAGE_SETTINGS_LINK_CONNECTED) {
        mqtt_status(PAGE_SETTINGS_LINK_FAILED, "Waiting for Wi-Fi");
        ui_devices_feed_set_link(PAGE_DEVICES_LINK_OFFLINE, "Waiting for Wi-Fi");
        return;
    }

    lv_snprintf(text, sizeof(text), "Connecting to %s:%u...", s->mqtt_host, (unsigned)s->mqtt_port);
    mqtt_status(PAGE_SETTINGS_LINK_BUSY, text);
    ui_devices_feed_set_link(PAGE_DEVICES_LINK_CONNECTING, s->mqtt_host);

    lv_timer_set_repeat_count(lv_timer_create(mqtt_done, CONNECT_DELAY_MS, NULL), 1);
}

static void changed(const settings_t * edited)
{
    settings_t before = *settings_get();
    settings_t after  = *edited;

    /*Switching to automatic time zone detects it straight away.*/
    if(after.timezone_auto && !before.timezone_auto) zone_detect(&after);

    settings_set(&after);
    store();
    behaviour_apply();

    /*The forecasts follow the location and the units; the sensors' readings the units.*/
    ui_weather_feed_settings_changed(&before, &after);
    if(before.fahrenheit != after.fahrenheit) ui_air_feed_republish();

    bool zone_changed = strcmp(before.timezone_posix, after.timezone_posix) != 0;
    bool retime       = zone_changed || before.show_seconds != after.show_seconds;
    bool restyle      = before.theme != after.theme || before.accent != after.accent;
    /*Times and dates are written into every page, so a change of format
     *rebuilds them all -- the same as a change of colours.*/
    bool reformat     = before.clock_24h != after.clock_24h || before.date_format != after.date_format;

    if(zone_changed) clock_time_set_zone(after.timezone_posix);

    /*Back to network time drops the hand-set offset. TODO: on the clock,
     *(re)start SNTP against the time server, also when the server changes.*/
    if(after.time_auto && !before.time_auto) {
        clock_time_use_system();
        retime = true;
    }

    if(restyle) {
        ui_theme_set(after.theme == SETTINGS_THEME_LIGHT ? UI_THEME_LIGHT : UI_THEME_DARK, after.accent);
    }

    if(restyle || reformat) {
        /*Not from inside the event that asked: the rebuild deletes the very
         *control that was tapped. It refreshes the clock and every feed.*/
        lv_async_call(rebuild_async, NULL);
        return;
    }

    if(retime) ui_clock_feed_refresh();

    /*The detected zone, or the clock in a new zone, shows on the page.*/
    if(zone_changed || retime) ui_settings_feed_republish();
}

/** TODO: on the clock, esp_wifi_scan_start() and the scan-done event. */
static void scan_requested(void)
{
    if(scanning) return;

    scanning = true;
    page_settings_set_scanning(true);
    lv_timer_set_repeat_count(lv_timer_create(scan_done, SCAN_DELAY_MS, NULL), 1);
}

static void wifi_requested(const char * ssid, const char * password)
{
    if(ssid[0] == '\0') {
        wifi_status(PAGE_SETTINGS_LINK_FAILED, "Enter the network's name");
        return;
    }

    wifi_connect_start(ssid, password);
}

static void mqtt_requested(const settings_t * edited)
{
    settings_t s = *settings_get();

    lv_memcpy(s.mqtt_host, edited->mqtt_host, sizeof(s.mqtt_host));
    s.mqtt_port = edited->mqtt_port;
    s.mqtt_tls  = edited->mqtt_tls;
    lv_memcpy(s.mqtt_username, edited->mqtt_username, sizeof(s.mqtt_username));
    lv_memcpy(s.mqtt_password, edited->mqtt_password, sizeof(s.mqtt_password));
    lv_memcpy(s.mqtt_client_id, edited->mqtt_client_id, sizeof(s.mqtt_client_id));
    lv_memcpy(s.mqtt_config_topic, edited->mqtt_config_topic, sizeof(s.mqtt_config_topic));

    settings_set(&s);
    store();
    mqtt_connect_start();
}

/**
 * The date or time set by hand. TODO: on the clock, settimeofday() and the
 * RTC; the simulator cannot set the computer's clock, so it keeps an offset,
 * which lasts until the simulator closes.
 */
static void time_requested(const struct tm * local)
{
    if(settings_get()->time_auto) return;

    clock_time_set_local(local);
    ui_clock_feed_refresh();
    ui_settings_feed_republish();
}

/** Keeps the backlight on the right level and the settings page's clock current. */
static void tick(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    backlight_apply(false);

    if(ui_current_page() != UI_PAGE_SETTINGS) {
        shown_minute = -1;
        return;
    }

    struct tm now;
    clock_time_now(&now);
    if(now.tm_min == shown_minute) return;

    shown_minute = now.tm_min;
    page_settings_set_now(&now);
}

static void scan_done(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    scanning      = false;
    network_count = SIM_NETWORK_COUNT;
    page_settings_set_networks(sim_networks, network_count);
    page_settings_set_scanning(false);
}

/**
 * The pretend network joins if it is in range, and is open or was given a
 * password of WPA2's minimum length. Only a network that was joined is
 * stored -- a mistyped password never replaces a working one.
 */
static void wifi_done(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    const page_settings_network_t * network = NULL;
    for(uint32_t i = 0; i < SIM_NETWORK_COUNT && !network; i++) {
        if(strcmp(sim_networks[i].ssid, joining_ssid) == 0) network = &sim_networks[i];
    }

    char text[96];

    if(!network) {
        lv_snprintf(text, sizeof(text), "%s is not in range", joining_ssid);
        wifi_status(PAGE_SETTINGS_LINK_FAILED, text);
        mqtt_connect_start();
        return;
    }

    if(network->secured && strlen(joining_password) < 8) {
        lv_snprintf(text, sizeof(text), "Wrong password for %s", joining_ssid);
        wifi_status(PAGE_SETTINGS_LINK_FAILED, text);
        mqtt_connect_start();
        return;
    }

    settings_t s = *settings_get();
    if(strcmp(s.wifi_ssid, joining_ssid) != 0 || strcmp(s.wifi_password, joining_password) != 0) {
        lv_strlcpy(s.wifi_ssid, joining_ssid, sizeof(s.wifi_ssid));
        lv_strlcpy(s.wifi_password, joining_password, sizeof(s.wifi_password));
        settings_set(&s);
        store();
        page_settings_set_values(settings_get());
    }

    lv_snprintf(text, sizeof(text), "Connected to %s  " UI_BULLET "  192.168.1.42", joining_ssid);
    wifi_status(PAGE_SETTINGS_LINK_CONNECTED, text);

    mqtt_connect_start();
}

/** The pretend broker always takes the connection. */
static void mqtt_done(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    const settings_t * s = settings_get();
    char               text[96];

    lv_snprintf(text, sizeof(text), "Connected to %s:%u%s", s->mqtt_host, (unsigned)s->mqtt_port,
                s->mqtt_tls ? " over TLS" : "");
    mqtt_status(PAGE_SETTINGS_LINK_CONNECTED, text);
    ui_devices_feed_set_link(PAGE_DEVICES_LINK_ONLINE, s->mqtt_host);
    mqtt_client_set_connected(true);
}

static void rebuild_async(void * user)
{
    LV_UNUSED(user);
    ui_rebuild();
}
