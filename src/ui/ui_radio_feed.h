/**
 * @file ui_radio_feed.h
 *
 * Connects the radio page to the Radio Browser directory, the SD card and the
 * stream player.
 *
 * The saved stations live on the SD card (radio/radio_store.h). At start the
 * list is read back and shown at once; stations whose details or favicon are
 * missing are fetched from Radio Browser in the background and filled in as
 * they arrive. On the very first start there is no list, so a handful of
 * stations are saved by UUID and fetched the same way.
 *
 * The page's add dialog searches Radio Browser through here too, and a
 * station added from the results is written to the card along with its
 * favicon. Removing a station deletes its directory; removing the one that is
 * playing stops it.
 *
 * Playing goes through audio/radio_player.h, the same code in the simulator
 * as on the clock. What it is doing, and the stream's track titles, are polled
 * onto the page.
 */

#ifndef UI_RADIO_FEED_H
#define UI_RADIO_FEED_H

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
 * Load the saved stations and start feeding the radio page. Call once, after
 * ui_init() has built the pages.
 */
void ui_radio_feed_init(void);

/**
 * Tell the radio page again everything the feed last told it: the stations,
 * the one selected, what is playing and the volume. For after ui_rebuild(),
 * which builds the page afresh.
 */
void ui_radio_feed_republish(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_RADIO_FEED_H*/
