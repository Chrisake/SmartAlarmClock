/**
 * @file ui_presence_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_presence_feed.h"
#include "ui/ui.h"
#include "ui/ui_status.h"
#include "ui/pages/settings/page_settings.h"
#include "presence/face_wake.h"
#include "presence/radar_wake.h"
#include "settings/settings.h"

/*********************
 *      DEFINES
 *********************/

/** How often the idle state and a wake are looked at. */
#define POLL_MS 100

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void poll(lv_timer_t * timer);
static void face_poll(const settings_t * s);
static void radar_poll(const settings_t * s);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint8_t running_fps;
static uint8_t running_frames;
static bool    failure_shown;

static uint8_t               running_sensitivity;
static settings_radar_dark_t running_dark;
static bool                  radar_started;
static bool                  radar_failure_shown;

/** What the settings page was last told, so it hears only of changes. */
static page_settings_radar_t shown_state = PAGE_SETTINGS_RADAR_UNKNOWN;
static bool                  shown_dark;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_presence_feed_init(void)
{
    lv_timer_create(poll, POLL_MS, NULL);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void poll(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    const settings_t * s = settings_get();

    face_poll(s);
    radar_poll(s);
}

/** The camera, which is only ever opened while the screen is idle. */
static void face_poll(const settings_t * s)
{
    if(!s->face_wake || !ui_is_idle()) {
        if(face_wake_is_running()) face_wake_stop();
        return;
    }

    if(!face_wake_is_running() || s->face_wake_fps != running_fps || s->face_wake_frames != running_frames) {
        running_fps    = s->face_wake_fps;
        running_frames = s->face_wake_frames;
        if(!face_wake_start(running_fps, running_frames) && !failure_shown) {
            failure_shown = true;
            ui_notice_show(UI_NOTICE_ERROR, "Face wake could not be started");
        }
    }

    if(face_wake_failed() && !failure_shown) {
        failure_shown = true;
        LV_LOG_WARN("face wake: the camera could not be started");
        ui_notice_show(UI_NOTICE_ERROR, "The camera could not be started for face wake");
    }

    if(face_wake_take()) {
        LV_LOG_USER("face wake: a face for %u frames, so the screen wakes", (unsigned)running_frames);
        ui_wake();
    }
}

/**
 * The radar, which keeps watching whether the screen is idle or not: whoever
 * is already in the room is then part of what it takes for granted by the time
 * the screen goes idle, and the settings page can say whether the sensor is
 * there. A wake it raises while the screen is in use is taken and dropped.
 */
static void radar_poll(const settings_t * s)
{
    if(!s->radar_wake) {
        if(radar_started) {
            radar_wake_stop();
            radar_started = false;
        }
        if(shown_state != PAGE_SETTINGS_RADAR_UNKNOWN) {
            shown_state = PAGE_SETTINGS_RADAR_UNKNOWN;
            page_settings_set_radar(shown_state, false);
        }
        return;
    }

    if(!radar_started || s->radar_sensitivity != running_sensitivity || s->radar_dark != running_dark) {
        running_sensitivity = s->radar_sensitivity;
        running_dark        = s->radar_dark;

        radar_wake_dark_t dark = running_dark == SETTINGS_RADAR_DARK_ON       ? RADAR_WAKE_DARK_ON
                                 : running_dark == SETTINGS_RADAR_DARK_OFF    ? RADAR_WAKE_DARK_OFF
                                                                             : RADAR_WAKE_DARK_REDUCED;

        if(radar_wake_start(running_sensitivity, dark)) {
            radar_started = true;
        }
        else if(!radar_failure_shown) {
            radar_failure_shown = true;
            ui_notice_show(UI_NOTICE_ERROR, "Movement wake could not be started");
        }
    }

    /*Whether the sensor is there, and whether the room is dark, for the
     *settings page; a sensor that is not there is worth saying once.*/
    page_settings_radar_t state = radar_wake_sensor() == RADAR_WAKE_SENSOR_READY   ? PAGE_SETTINGS_RADAR_READY
                                  : radar_wake_sensor() == RADAR_WAKE_SENSOR_MISSING ? PAGE_SETTINGS_RADAR_MISSING
                                                                                     : PAGE_SETTINGS_RADAR_UNKNOWN;
    bool dark = radar_wake_room_dark();

    if(state != shown_state || dark != shown_dark) {
        shown_state = state;
        shown_dark  = dark;
        page_settings_set_radar(state, dark);

        if(state == PAGE_SETTINGS_RADAR_MISSING && !radar_failure_shown) {
            radar_failure_shown = true;
            LV_LOG_WARN("movement wake: the radar does not answer");
            ui_notice_show(UI_NOTICE_ERROR, "No movement sensor found");
        }
    }

    if(radar_wake_take() && ui_is_idle()) {
        LV_LOG_USER("movement wake: something moved worth waking for, so the screen wakes");
        ui_wake();
    }
}
