/**
 * @file ui_alarm_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_alarm_feed.h"
#include "ui/ui.h"
#include "ui/ui_alarm_screen.h"
#include "ui/ui_clock_feed.h"
#include "ui/ui_format.h"
#include "ui/ui_radio_feed.h"
#include "ui/ui_theme.h"
#include "ui/pages/alarms/page_alarms.h"
#include "ui/pages/clock/page_clock.h"
#include "audio/audio_sink.h"
#include "audio/radio_player.h"
#include "audio/tone_player.h"
#include "settings/clock_time.h"
#include "settings/settings.h"

#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** How often the clock is looked at, and a ringing alarm's volume stepped. */
#define TICK_MS 250

/** An alarm nobody answers stops by itself after this long. */
#define RING_LIMIT_MS (15UL * 60UL * 1000UL)

/** A station that has not started playing by then gives way to the tone. */
#define STREAM_WAIT_MS 15000

/** Where the volume starts, before it rises to the settings' alarm volume. */
#define RAMP_FROM_PERCENT 5

/** A tone previewed from the alarm editor plays a few rounds, then stops by itself. */
#define PREVIEW_MS 8000

/** Largest alarms file read. */
#define ALARMS_MAX_BYTES (16 * 1024)

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    SOUND_NONE,
    SOUND_TONE,
    SOUND_STATION,
} sound_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void tick(lv_timer_t * timer);
static void alarms_check(const struct tm * now);
static void once_disable(uint32_t index);
static void ring(const page_alarm_t * alarm);
static void ring_update(const struct tm * now);
static void ring_end(void);
static void screen_time_show(const struct tm * now);

static void sound_start(void);
static void sound_stop(void);
static void tone_start(void);
static void station_failed(void);
static void volume_set(int32_t percent);

static void snooze_pressed(void);
static void stop_pressed(void);
static void listen_pressed(void);

static void preview_requested(int32_t tone);
static void preview_end(void);

static void   alarms_changed(const page_alarm_t alarms[], uint32_t count);
static bool   alarms_load(void);
static void   alarms_store(const page_alarm_t alarms[], uint32_t count);
static char * file_read(const char * path, size_t * len);
static bool   same_text(const char * a, const char * b);

/**********************
 *  STATIC VARIABLES
 **********************/

/*Monday first, matching page_alarm_days_t.*/
static const char * const day_keys[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

/** The minute last looked at, as a number unique to it; -1 before the first. */
static int32_t checked_minute = -1;

static bool         ringing;
static page_alarm_t ringing_alarm;
static uint32_t     ring_start;
static int          shown_minute;
static char         note[160];

static sound_t  sound;
static uint32_t stream_start;
static bool     stream_heard;   /**< The station has played since it was started */
static int32_t  volume_now;

static bool         snoozed;
static page_alarm_t snoozed_alarm;
static uint32_t     snooze_start;
static uint32_t     snooze_ms;

static bool     previewing;
static uint32_t preview_start;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_alarm_feed_init(void)
{
    page_alarms_set_changed_cb(alarms_changed);
    page_alarms_set_preview_cb(preview_requested);

    /*With no file yet the page keeps its examples, stored at the first edit.*/
    if(!alarms_load()) LV_LOG_USER("alarm: no alarms stored yet in " UI_ALARMS_PATH);

    lv_timer_create(tick, TICK_MS, NULL);
    ui_clock_feed_refresh();
}

bool ui_alarm_feed_snooze(char * name, size_t name_size, int * hour, int * minute)
{
    if(!snoozed) return false;

    uint32_t elapsed = lv_tick_elaps(snooze_start);
    uint32_t left_ms = elapsed < snooze_ms ? snooze_ms - elapsed : 0;

    struct tm now;
    clock_time_now(&now);

    int32_t at = now.tm_hour * 3600 + now.tm_min * 60 + now.tm_sec + (int32_t)(left_ms / 1000U);
    *hour      = (at / 3600) % 24;
    *minute    = (at / 60) % 60;

    lv_strlcpy(name, snoozed_alarm.name, name_size);
    return true;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void tick(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    struct tm now;
    clock_time_now(&now);

    /*Each minute is looked at once, however often this runs.*/
    int32_t minute = ((now.tm_year * 366 + now.tm_yday) * 24 + now.tm_hour) * 60 + now.tm_min;
    if(minute != checked_minute) {
        checked_minute = minute;
        alarms_check(&now);
    }

    if(snoozed && lv_tick_elaps(snooze_start) >= snooze_ms) {
        page_alarm_t alarm = snoozed_alarm;
        ring(&alarm);
    }

    if(ringing) ring_update(&now);

    if(previewing && lv_tick_elaps(preview_start) >= PREVIEW_MS) {
        preview_end();
        page_alarms_preview_ended();
    }
}

static void alarms_check(const struct tm * now)
{
    uint32_t             count  = 0;
    const page_alarm_t * alarms = page_alarms_get_alarms(&count);

    /*tm_wday counts from Sunday; page_alarm_days_t from Monday.*/
    int weekday = (now->tm_wday + 6) % 7;

    for(uint32_t i = 0; i < count; i++) {
        const page_alarm_t * alarm = &alarms[i];

        if(!alarm->enabled || alarm->hour != now->tm_hour || alarm->minute != now->tm_min) continue;
        if(alarm->days != 0 && (alarm->days & (1 << weekday)) == 0) continue;

        /*Copied first: switching a one-off alarm off reloads the list.*/
        page_alarm_t due = *alarm;
        if(due.days == 0) once_disable(i);

        ring(&due);
        return;
    }
}

/** A one-off alarm has rung: switch it off, as its toggle would. */
static void once_disable(uint32_t index)
{
    uint32_t             count  = 0;
    const page_alarm_t * alarms = page_alarms_get_alarms(&count);
    page_alarm_t         list[PAGE_ALARMS_MAX];

    if(index >= count) return;

    memcpy(list, alarms, count * sizeof(list[0]));
    list[index].enabled = false;

    page_alarms_set_alarms(list, count);
    alarms_store(list, count);
    ui_clock_feed_refresh();
}

static void ring(const page_alarm_t * alarm)
{
    if(ringing) sound_stop();

    /*The alarm takes the output from a preview.*/
    if(previewing) {
        preview_end();
        page_alarms_preview_ended();
    }

    ringing_alarm = *alarm;
    ringing       = true;
    snoozed       = false;
    ring_start    = lv_tick_get();
    volume_now    = -1;

    LV_LOG_USER("alarm: \"%s\" is ringing", alarm->name);

    /*Wake the panel: on if it was off, off the ambient face, and so up to its awake brightness.*/
    ui_wake();

    const char * station = alarm->station[0] ? page_alarms_station_name(alarm->station) : NULL;
    const char * sound_name = station ? station : page_alarms_tone_name(alarm->tone);
    char         detail[PAGE_ALARMS_STATION_NAME_LEN + 64];

    if(alarm->days) {
        char repeat[48];
        page_alarms_days_text(alarm->days, repeat, sizeof(repeat));
        lv_snprintf(detail, sizeof(detail), "%s  " UI_BULLET "  %s", repeat, sound_name);
    }
    else {
        lv_strlcpy(detail, sound_name, sizeof(detail));
    }

    ui_alarm_screen_info_t info = {
        .name           = alarm->name[0] ? alarm->name : "Alarm",
        .detail         = detail,
        .snooze_minutes = settings_get()->alarm_snooze_minutes,
        .listen         = station != NULL,
    };
    ui_alarm_screen_show(&info, snooze_pressed, stop_pressed, listen_pressed);

    struct tm now;
    clock_time_now(&now);
    screen_time_show(&now);

    sound_start();
    ui_clock_feed_refresh();
}

static void ring_update(const struct tm * now)
{
    const settings_t * s       = settings_get();
    uint32_t           elapsed = lv_tick_elaps(ring_start);

    /*Keep the idle timeout from dropping to the ambient face underneath.*/
    lv_display_trigger_activity(lv_display_get_default());

    if(now->tm_min != shown_minute) screen_time_show(now);

    /*From a whisper up to the alarm volume, over the ramp.*/
    uint32_t ramp   = s->alarm_ramp_seconds * 1000U;
    int32_t  target = s->alarm_volume;
    int32_t  level  = (ramp == 0 || elapsed >= ramp)
                      ? target
                      : RAMP_FROM_PERCENT + (int32_t)((int64_t)(target - RAMP_FROM_PERCENT) * elapsed / ramp);
    if(level != volume_now) volume_set(level);

    if(sound == SOUND_STATION) {
        radio_player_status_t status;
        radio_player_get_status(&status);

        if(status.state == RADIO_PLAYER_PLAYING) stream_heard = true;

        if(status.state == RADIO_PLAYER_ERROR || status.state == RADIO_PLAYER_STOPPED ||
           (!stream_heard && lv_tick_elaps(stream_start) >= STREAM_WAIT_MS)) {
            station_failed();
        }
    }

    if(elapsed >= RING_LIMIT_MS) {
        LV_LOG_USER("alarm: nobody answered, so it stops");
        ring_end();
    }
}

static void ring_end(void)
{
    sound_stop();
    ui_alarm_screen_hide();
    ringing = false;
    ui_clock_feed_refresh();
}

static void screen_time_show(const struct tm * now)
{
    char text[8];

    shown_minute = now->tm_min;
    ui_format_clock(text, sizeof(text), now->tm_hour, now->tm_min);
    ui_alarm_screen_set_time(text, ui_format_meridiem(now->tm_hour));
}

static void sound_start(void)
{
    volume_set(RAMP_FROM_PERCENT);

    if(ringing_alarm.station[0] && ui_radio_feed_play_station(ringing_alarm.station)) {
        sound        = SOUND_STATION;
        stream_start = lv_tick_get();
        stream_heard = false;
        return;
    }

    if(ringing_alarm.station[0]) station_failed();
    else                         tone_start();
}

static void sound_stop(void)
{
    if(sound == SOUND_STATION) ui_radio_feed_stop();
    if(sound == SOUND_TONE) tone_player_stop();
    sound = SOUND_NONE;

    /*Back to the radio's own volume.*/
    audio_sink_set_volume(ui_radio_feed_get_volume());
}

static void tone_start(void)
{
    /*The output takes one writer at a time: the radio makes way.*/
    ui_radio_feed_stop();
    if(!tone_player_start(ringing_alarm.tone)) LV_LOG_WARN("alarm: the tone could not be started");
    sound = SOUND_TONE;
}

static void station_failed(void)
{
    const char * name = page_alarms_station_name(ringing_alarm.station);

    lv_snprintf(note, sizeof(note), "%s did not play, so %s is ringing instead", name ? name : "The station",
                page_alarms_tone_name(ringing_alarm.tone));
    LV_LOG_WARN("alarm: %s", note);
    ui_alarm_screen_set_note(note);
    ui_alarm_screen_set_listen(false);

    tone_start();
}

static void volume_set(int32_t percent)
{
    volume_now = percent;
    audio_sink_set_volume(percent);
}

static void snooze_pressed(void)
{
    const settings_t * s = settings_get();

    sound_stop();
    ui_alarm_screen_hide();

    ringing       = false;
    snoozed       = true;
    snoozed_alarm = ringing_alarm;
    snooze_start  = lv_tick_get();
    snooze_ms     = s->alarm_snooze_minutes * 60UL * 1000UL;

    LV_LOG_USER("alarm: snoozed for %u min", (unsigned)s->alarm_snooze_minutes);
    ui_clock_feed_refresh();
}

static void stop_pressed(void)
{
    ring_end();
}

/** Stop the alarm but leave its station playing, on the radio as if picked there. */
static void listen_pressed(void)
{
    if(sound == SOUND_STATION) {
        /*The radio carries on at the level the alarm had reached, and the
         *radio page's slider says so. Left playing: ring_end() stops nothing.*/
        ui_radio_feed_set_volume(volume_now);
        sound = SOUND_NONE;
    }

    ring_end();
}

static void preview_requested(int32_t tone)
{
    if(tone < 0) {
        preview_end();
        return;
    }

    /*A ringing alarm has the output.*/
    if(ringing) {
        page_alarms_preview_ended();
        return;
    }

    /*One writer at a time, as for an alarm: the radio makes way.*/
    if(previewing) tone_player_stop();
    ui_radio_feed_stop();
    audio_sink_set_volume(settings_get()->alarm_volume);

    if(!tone_player_start((uint8_t)tone)) {
        LV_LOG_WARN("alarm: the tone could not be previewed");
        audio_sink_set_volume(ui_radio_feed_get_volume());
        previewing = false;
        page_alarms_preview_ended();
        return;
    }

    previewing    = true;
    preview_start = lv_tick_get();
}

static void preview_end(void)
{
    if(!previewing) return;

    previewing = false;
    tone_player_stop();
    audio_sink_set_volume(ui_radio_feed_get_volume());
}

static void alarms_changed(const page_alarm_t alarms[], uint32_t count)
{
    alarms_store(alarms, count);
    ui_clock_feed_refresh();
}

/**
 * {"alarms": [{"name": "Wake up", "hour": 7, "minute": 0, "days": ["mon", ...],
 *              "enabled": true, "tone": "radar", "station": ""}]}
 *
 * A "snooze" key from before every alarm offered Snooze is ignored.
 */
static bool alarms_load(void)
{
    size_t len  = 0;
    char * json = file_read(UI_ALARMS_PATH, &len);
    if(!json) return false;

    cJSON * root = cJSON_ParseWithLength(json, len);
    free(json);

    const cJSON * list = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "alarms") : NULL;
    if(!cJSON_IsArray(list)) {
        LV_LOG_WARN("alarm: " UI_ALARMS_PATH " holds no alarm list");
        cJSON_Delete(root);
        return false;
    }

    page_alarm_t  alarms[PAGE_ALARMS_MAX];
    uint32_t      count = 0;
    const cJSON * item;

    cJSON_ArrayForEach(item, list) {
        if(count == PAGE_ALARMS_MAX) break;
        if(!cJSON_IsObject(item)) continue;

        const cJSON * name    = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON * hour    = cJSON_GetObjectItemCaseSensitive(item, "hour");
        const cJSON * minute  = cJSON_GetObjectItemCaseSensitive(item, "minute");
        const cJSON * days    = cJSON_GetObjectItemCaseSensitive(item, "days");
        const cJSON * enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
        const cJSON * tone    = cJSON_GetObjectItemCaseSensitive(item, "tone");
        const cJSON * station = cJSON_GetObjectItemCaseSensitive(item, "station");

        if(!cJSON_IsNumber(hour) || hour->valueint < 0 || hour->valueint > 23) continue;
        if(!cJSON_IsNumber(minute) || minute->valueint < 0 || minute->valueint > 59) continue;

        page_alarm_t * alarm = &alarms[count];
        memset(alarm, 0, sizeof(*alarm));

        alarm->hour    = (uint8_t)hour->valueint;
        alarm->minute  = (uint8_t)minute->valueint;
        alarm->enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);

        if(cJSON_IsString(name)) ui_format_text_copy(alarm->name, sizeof(alarm->name), name->valuestring);

        const cJSON * day;
        cJSON_ArrayForEach(day, days) {
            for(uint32_t d = 0; cJSON_IsString(day) && d < 7; d++) {
                if(same_text(day->valuestring, day_keys[d])) alarm->days |= (uint8_t)(1 << d);
            }
        }

        for(uint8_t t = 0; cJSON_IsString(tone) && t < PAGE_ALARM_TONE_COUNT; t++) {
            if(same_text(tone->valuestring, page_alarms_tone_name(t))) alarm->tone = t;
        }

        if(cJSON_IsString(station) && strlen(station->valuestring) < sizeof(alarm->station)) {
            lv_strlcpy(alarm->station, station->valuestring, sizeof(alarm->station));
        }

        count++;
    }

    cJSON_Delete(root);

    page_alarms_set_alarms(alarms, count);
    return true;
}

/** TODO: on the clock, to NVS or the SD card. */
static void alarms_store(const page_alarm_t alarms[], uint32_t count)
{
    cJSON * root = cJSON_CreateObject();
    cJSON * list = cJSON_AddArrayToObject(root, "alarms");

    for(uint32_t i = 0; i < count; i++) {
        const page_alarm_t * alarm = &alarms[i];
        cJSON *              item  = cJSON_CreateObject();
        char                 tone[16];

        cJSON_AddStringToObject(item, "name", alarm->name);
        cJSON_AddNumberToObject(item, "hour", alarm->hour);
        cJSON_AddNumberToObject(item, "minute", alarm->minute);

        cJSON * days = cJSON_AddArrayToObject(item, "days");
        for(uint32_t d = 0; d < 7; d++) {
            if(alarm->days & (1 << d)) cJSON_AddItemToArray(days, cJSON_CreateString(day_keys[d]));
        }

        cJSON_AddBoolToObject(item, "enabled", alarm->enabled);

        lv_strlcpy(tone, page_alarms_tone_name(alarm->tone), sizeof(tone));
        for(char * c = tone; *c; c++) *c = (char)tolower((unsigned char)*c);
        cJSON_AddStringToObject(item, "tone", tone);
        cJSON_AddStringToObject(item, "station", alarm->station);

        cJSON_AddItemToArray(list, item);
    }

    char * text = cJSON_Print(root);
    cJSON_Delete(root);
    if(!text) return;

    FILE * file = fopen(UI_ALARMS_PATH, "wb");
    if(file) {
        fwrite(text, 1, strlen(text), file);
        fclose(file);
    }
    else {
        LV_LOG_WARN("alarm: could not write " UI_ALARMS_PATH);
    }

    cJSON_free(text);
}

static char * file_read(const char * path, size_t * len)
{
    FILE * file = fopen(path, "rb");
    if(!file) return NULL;

    char * buf = malloc(ALARMS_MAX_BYTES);
    size_t n   = buf ? fread(buf, 1, ALARMS_MAX_BYTES, file) : 0;
    fclose(file);

    if(!buf || n == 0) {
        free(buf);
        return NULL;
    }

    *len = n;
    return buf;
}

static bool same_text(const char * a, const char * b)
{
    for(; *a && *b; a++, b++) {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return *a == *b;
}
