/**
 * @file ui_clock_feed.h
 *
 * Drives the clock page from the real time and the alarm list.
 *
 * This is the one place that reads the clock, so it owns everything that has
 * to change as time passes: the time and date on the home page, and which
 * alarm is shown as the next to ring. It recomputes the next alarm on every
 * minute boundary and whenever the alarms page reports an edit.
 *
 * It is deliberately separate from the pages, which stay views: the feed reads
 * the alarm list through the alarms page's public getter and pushes the result
 * into the clock page through its setters. When a real application layer
 * arrives -- with persistence, an NTP client and something that actually
 * sounds the alarm -- this is the seam it replaces.
 */

#ifndef UI_CLOCK_FEED_H
#define UI_CLOCK_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Start feeding the clock page. Call once, after ui_init() has built the
 * pages; it pushes an initial update immediately so nothing shows placeholder
 * text, then keeps itself running on a timer.
 */
void ui_clock_feed_init(void);

/**
 * Push the time, the date and the next alarm to the clock page right away.
 *
 * Runs whenever the alarms page reports a change. Call it too after anything
 * else that changes what the clock shows: alarms changed without going
 * through the page, a change of clock format, or a ui_rebuild(), which leaves
 * the new clock page showing placeholders.
 */
void ui_clock_feed_refresh(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_CLOCK_FEED_H*/
