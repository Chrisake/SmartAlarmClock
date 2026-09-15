/**
 * @file ui_clock_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_clock_feed.h"
#include "ui/ui_alarm_feed.h"
#include "ui/ui_format.h"
#include "ui/pages/alarms/page_alarms.h"
#include "ui/pages/clock/page_clock.h"
#include "settings/clock_time.h"
#include "settings/settings.h"

/*********************
 *      DEFINES
 *********************/

/** How often the clock is read. The seconds readout wants one a second. */
#define TICK_PERIOD_MS 1000

#define MINUTES_PER_DAY (24 * 60)

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void tick_cb(lv_timer_t * timer);
static void push_time(const struct tm * now);
static void push_next_alarm(const struct tm * now);

/**********************
 *  STATIC VARIABLES
 **********************/

/*Buffers behind the strings handed to the clock page, which keeps the
 *pointers rather than copying.*/
static char time_text[8];
static char seconds_text[4];
static char date_text[40];
static char alarm_name[PAGE_ALARMS_NAME_LEN];
static char alarm_time[12];
static char alarm_when[20];

/*Used to notice a minute boundary; -1 forces the next tick to do the work.*/
static int last_minute = -1;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_clock_feed_init(void)
{
    lv_timer_create(tick_cb, TICK_PERIOD_MS, NULL);
    tick_cb(NULL);
}

void ui_clock_feed_refresh(void)
{
    /*Everything, not just what changed: after a rebuild the page is showing
     *its placeholders, and a change of clock, date or zone affects every
     *readout.*/
    last_minute = -1;
    tick_cb(NULL);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void tick_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    /*The device's time, in its zone -- set by hand or from the network.*/
    struct tm now;
    clock_time_now(&now);

    push_time(&now);

    /*The date and the next alarm only ever change on a minute boundary, and
     *recomputing the alarm walks every alarm over a week, so skip it the other
     *59 times.*/
    if(now.tm_min == last_minute) return;
    last_minute = now.tm_min;

    ui_format_date_long(date_text, sizeof(date_text), &now);
    page_clock_set_date(date_text);

    push_next_alarm(&now);
}

static void push_time(const struct tm * now)
{
    ui_format_clock(time_text, sizeof(time_text), now->tm_hour, now->tm_min);
    lv_snprintf(seconds_text, sizeof(seconds_text), "%02d", now->tm_sec);

    page_clock_set_time(time_text,
                        settings_get()->show_seconds ? seconds_text : NULL,
                        ui_format_meridiem(now->tm_hour));
}

/**
 * Minutes from now until an alarm next rings, and how many days ahead that is.
 *
 * @param alarm       the alarm to place
 * @param now_wday    today as a Monday-first index, matching page_alarm_days_t
 * @param now_minute  minutes elapsed today
 * @param days_ahead  receives 0 for today, 1 for tomorrow, and so on
 * @return            minutes until it rings
 */
static uint32_t minutes_until(const page_alarm_t * alarm, int now_wday, int now_minute,
                              int * days_ahead)
{
    int alarm_minute = alarm->hour * 60 + alarm->minute;

    /*No repeat days means it fires once, at the next occurrence of that time.*/
    if(alarm->days == 0) {
        *days_ahead = alarm_minute > now_minute ? 0 : 1;
        return (uint32_t)(*days_ahead * MINUTES_PER_DAY + alarm_minute - now_minute);
    }

    for(int offset = 0; offset < 7; offset++) {
        int weekday = (now_wday + offset) % 7;

        if((alarm->days & (1 << weekday)) == 0) continue;
        /*Today only counts if the time has not already gone by.*/
        if(offset == 0 && alarm_minute <= now_minute) continue;

        *days_ahead = offset;
        return (uint32_t)(offset * MINUTES_PER_DAY + alarm_minute - now_minute);
    }

    /*Every matching day this week is behind us, so it is a week out.*/
    *days_ahead = 7;
    return (uint32_t)(7 * MINUTES_PER_DAY + alarm_minute - now_minute);
}

static void push_next_alarm(const struct tm * now)
{
    /*A snoozed alarm is back within minutes: show it rather than the next.*/
    int snooze_hour, snooze_minute;
    if(ui_alarm_feed_snooze(alarm_name, sizeof(alarm_name), &snooze_hour, &snooze_minute)) {
        if(!alarm_name[0]) lv_strlcpy(alarm_name, "Alarm", sizeof(alarm_name));
        ui_format_time(alarm_time, sizeof(alarm_time), snooze_hour, snooze_minute);
        lv_strlcpy(alarm_when, "Snoozed", sizeof(alarm_when));
        page_clock_set_next_alarm(alarm_name, alarm_time, alarm_when, true);
        return;
    }

    uint32_t             count = 0;
    const page_alarm_t * alarms = page_alarms_get_alarms(&count);

    /*tm_wday counts from Sunday; page_alarm_days_t counts from Monday.*/
    int now_wday   = (now->tm_wday + 6) % 7;
    int now_minute = now->tm_hour * 60 + now->tm_min;

    const page_alarm_t * best = NULL;
    uint32_t             best_delta = 0;
    int                  best_days = 0;

    for(uint32_t i = 0; i < count; i++) {
        if(!alarms[i].enabled) continue;

        int      days  = 0;
        uint32_t delta = minutes_until(&alarms[i], now_wday, now_minute, &days);

        if(best == NULL || delta < best_delta) {
            best       = &alarms[i];
            best_delta = delta;
            best_days  = days;
        }
    }

    if(best == NULL) {
        page_clock_set_next_alarm(NULL, NULL, NULL, false);
        return;
    }

    lv_strlcpy(alarm_name, best->name[0] ? best->name : "Alarm", sizeof(alarm_name));
    ui_format_time(alarm_time, sizeof(alarm_time), best->hour, best->minute);

    if(best_days == 0)      lv_snprintf(alarm_when, sizeof(alarm_when), "Today");
    else if(best_days == 1) lv_snprintf(alarm_when, sizeof(alarm_when), "Tomorrow");
    else lv_snprintf(alarm_when, sizeof(alarm_when), "on %s", ui_format_weekday(now->tm_wday + best_days, false));

    page_clock_set_next_alarm(alarm_name, alarm_time, alarm_when, true);
}
