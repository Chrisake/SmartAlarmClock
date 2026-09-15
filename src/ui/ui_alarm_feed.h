/**
 * @file ui_alarm_feed.h
 *
 * Keeps the alarms, and rings them.
 *
 * The alarms page edits the list; this stores it -- in the simulator, in
 * data/alarms.json; TODO: on the clock, NVS or the SD card -- and reads it back
 * at start. Four times a second it looks at the clock, and when an enabled
 * alarm's minute comes round on one of its days it rings. An alarm with no
 * days rings at the next occurrence of its time, then switches itself off.
 *
 * Ringing puts up ui_alarm_screen and plays the alarm's station through the
 * radio feed, or its tone through tone_player. A station that has not started
 * within a few seconds, or drops, gives way to the tone, and the screen says
 * so. The volume rises from a whisper to the settings' alarm volume over the
 * ramp time, and an alarm nobody answers stops after a quarter of an hour.
 *
 * Snooze silences it for the settings' snooze time and then rings it again;
 * the clock page's chip shows it snoozed meanwhile. Stop ends it. Stop &
 * Listen, offered while a station is what plays, ends it but leaves the
 * station on, on the radio.
 *
 * The alarm editor's play button previews a tone through here too, at the
 * alarm volume, for a few seconds.
 */

#ifndef UI_ALARM_FEED_H
#define UI_ALARM_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/*********************
 *      DEFINES
 *********************/

/** Where the simulator keeps the alarms, relative to the working directory. */
#define UI_ALARMS_PATH "data/alarms.json"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Load the stored alarms into the alarms page and start watching the clock.
 * Call once, after ui_init() has built the pages.
 */
void ui_alarm_feed_init(void);

/**
 * The alarm snoozed, if there is one.
 * @param name        receives its label
 * @param name_size   size of `name`
 * @param hour        receives when it rings again, 0..23
 * @param minute      and the minute
 * @return            false if no alarm is snoozed
 */
bool ui_alarm_feed_snooze(char * name, size_t name_size, int * hour, int * minute);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_ALARM_FEED_H*/
