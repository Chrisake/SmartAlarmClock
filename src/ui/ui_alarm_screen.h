/**
 * @file ui_alarm_screen.h
 *
 * What the clock shows while an alarm rings: the time, the alarm's name and
 * what is playing, and two large targets for a sleepy hand, Snooze and Stop.
 *
 *   +------------------------------------------------------------+
 *   |                         ( bell )                           |
 *   |                          7:00 AM                           |
 *   |                          Wake up                           |
 *   |                     Weekdays  .  FIP                       |
 *   |                                                            |
 *   |           [    Snooze    ]      [     Stop     ]           |
 *   |           [    9 min     ]      [              ]           |
 *   +------------------------------------------------------------+
 *
 * It covers the whole panel, navigation rail and all, on the top layer, so
 * nothing underneath can be touched by accident while it is up.
 *
 * A view only: ui_alarm_feed decides when it shows, what it says and what the
 * buttons do.
 */

#ifndef UI_ALARM_SCREEN_H
#define UI_ALARM_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    const char * name;            /**< e.g. "Wake up" */
    const char * detail;          /**< Under the name, e.g. "Weekdays  .  FIP" */
    bool         snooze;          /**< Offer Snooze */
    uint8_t      snooze_minutes;  /**< Shown under Snooze */
} ui_alarm_screen_info_t;

typedef void (*ui_alarm_screen_cb_t)(void);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Show the screen, replacing one already up.
 * @param info     what it says; copied
 * @param snooze   called when Snooze is tapped
 * @param stop     called when Stop is tapped
 */
void ui_alarm_screen_show(const ui_alarm_screen_info_t * info, ui_alarm_screen_cb_t snooze, ui_alarm_screen_cb_t stop);

/**
 * Set the clock readout.
 * @param time       e.g. "7:00"
 * @param meridiem   e.g. "AM", or NULL on the 24-hour clock
 */
void ui_alarm_screen_set_time(const char * time, const char * meridiem);

/**
 * A line under the detail, for what went wrong: the station would not play,
 * so the tone is ringing instead.
 * @param note   the line, or NULL to hide it
 */
void ui_alarm_screen_set_note(const char * note);

/** Take the screen down. Safe from its own buttons' callbacks. */
void ui_alarm_screen_hide(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_ALARM_SCREEN_H*/
