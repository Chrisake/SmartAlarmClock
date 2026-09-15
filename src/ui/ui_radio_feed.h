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
 *
 * The saved stations are also offered to the alarm editor, and an alarm plays
 * one through here.
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

/**
 * Play a saved station, as if it had been picked on the radio page: for an
 * alarm. How it goes is read with radio_player_get_status().
 * @param uuid   the station's UUID
 * @return       false if it is not saved, or its details have not arrived yet
 */
bool ui_radio_feed_play_station(const char * uuid);

/** Stop the radio, as the page's stop button does. */
void ui_radio_feed_stop(void);

/** @return   the radio's volume, percent: what the output returns to after an alarm */
int32_t ui_radio_feed_get_volume(void);

/**
 * Set the radio's volume, as the page's slider does, and move the slider: for
 * a station an alarm leaves playing at the level it rose to.
 * @param volume   percent, 0..100
 */
void ui_radio_feed_set_volume(int32_t volume);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_RADIO_FEED_H*/
