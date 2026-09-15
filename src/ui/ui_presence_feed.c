/**
 * @file ui_presence_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_presence_feed.h"
#include "ui/ui.h"
#include "ui/ui_status.h"
#include "presence/face_wake.h"
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

/**********************
 *  STATIC VARIABLES
 **********************/

static uint8_t running_fps;
static uint8_t running_frames;
static bool    failure_shown;

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
