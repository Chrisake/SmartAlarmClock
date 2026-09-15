/**
 * @file page_radio.h
 *
 * Internet radio page: the saved stations, what is playing, transport and
 * volume, and a dialog to find and add stations.
 *
 * Layout:
 *
 *   +--------------------------+--------------------------------+
 *   |                          |  [+]                     Edit  |
 *   |   artwork                |  [logo] Station name           |
 *   |   station / track        |         Country - Language ... |
 *   |   [prev] [play] [next]   |  [logo] ...                    |
 *   |   volume ----o-------    |  (scroll)                      |
 *   +--------------------------+--------------------------------+
 *
 * + opens a dialog that searches the station directory by name, tag or
 * country, and adds stations from the results. Edit gives every station a
 * remove button and a handle to drag it up or down the list; Done puts them
 * away again.
 *
 * The page reports intent through the callbacks below and never touches the
 * audio pipeline, the directory or the SD card itself; the radio feed pushes
 * state back through the setters. That split keeps playback working even when
 * this page is hidden.
 */

#ifndef PAGE_RADIO_H
#define PAGE_RADIO_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_page.h"

/*********************
 *      DEFINES
 *********************/

/** Stations held in the list. */
#define PAGE_RADIO_STATION_MAX 32

/** Results a search shows. */
#define PAGE_RADIO_RESULT_MAX 30

/**********************
 *      TYPEDEFS
 **********************/

/** What the stream is doing right now. */
typedef enum {
    PAGE_RADIO_STATE_STOPPED,
    PAGE_RADIO_STATE_BUFFERING,
    PAGE_RADIO_STATE_PLAYING,
    PAGE_RADIO_STATE_ERROR,
} page_radio_state_t;

/** What a search in the add dialog matches. */
typedef enum {
    PAGE_RADIO_SEARCH_NAME,
    PAGE_RADIO_SEARCH_TAG,
    PAGE_RADIO_SEARCH_COUNTRY,
    PAGE_RADIO_SEARCH_COUNT,
} page_radio_search_field_t;

/**
 * One station, in the list or among the search results.
 *
 * Country, language and genre make up the subtitle, joined by bullets in that
 * order; leave out whatever is not known. Nothing is copied: the strings and
 * the favicon must outlive the row.
 */
typedef struct {
    const char *           name;      /**< e.g. "BBC Radio 6 Music" */
    const char *           country;   /**< e.g. "United Kingdom"; @nullable */
    const char *           language;  /**< e.g. "English"; @nullable */
    const char *           genre;     /**< Genre or kind of station, e.g. "Alternative"; @nullable */
    const lv_image_dsc_t * favicon;   /**< A square picture; NULL for the default icon */
} page_radio_station_t;

/** The user picked a station, by tapping it or with previous/next. */
typedef void (*page_radio_station_cb_t)(uint32_t index);

/** The user tapped play/pause. @param play true to start. */
typedef void (*page_radio_play_cb_t)(bool play);

/** The user dragged the volume slider. @param volume 0..100. */
typedef void (*page_radio_volume_cb_t)(int32_t volume);

/**
 * The user removed a station. The page has already taken it out of its list,
 * and cleared the selection if it was the selected one.
 */
typedef void (*page_radio_remove_cb_t)(uint32_t index);

/** The user dragged a station from `from` to `to`. The page's list already shows it there. */
typedef void (*page_radio_move_cb_t)(uint32_t from, uint32_t to);

/** The user searched the directory. */
typedef void (*page_radio_search_cb_t)(page_radio_search_field_t field, const char * query);

/** The user tapped + on search result `index`. */
typedef void (*page_radio_add_cb_t)(uint32_t index);

/** The user tapped the tick on search result `index`, a saved station: take it off the list. */
typedef void (*page_radio_result_remove_cb_t)(uint32_t index);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_radio_desc(void);

/**
 * Replace the station list. Clears the selection.
 * @param stations   array of stations; see page_radio_station_t for lifetimes
 * @param count      number of entries, clamped to PAGE_RADIO_STATION_MAX
 */
void page_radio_set_stations(const page_radio_station_t stations[], uint32_t count);

/**
 * Redraw one station, e.g. when its details or favicon arrive.
 * @param index     index into the station list
 * @param station   the station's new details
 */
void page_radio_update_station(uint32_t index, const page_radio_station_t * station);

/**
 * Mark which station is selected, highlighting it in the list.
 * @param index   index into the station list
 */
void page_radio_set_current_station(uint32_t index);

/** Select no station. */
void page_radio_clear_current_station(void);

/**
 * Update the now-playing card.
 * @param station   station name; NULL when no station is selected
 * @param track     track or programme metadata from the stream; @nullable
 * @param art       the station's favicon; NULL for the default artwork
 */
void page_radio_set_now_playing(const char * station, const char * track, const lv_image_dsc_t * art);

/**
 * Reflect the transport state: swaps the play/pause glyph and the status line.
 * @param state    what the stream is doing
 * @param detail   status text, e.g. "Reconnecting..." or an error message; @nullable
 */
void page_radio_set_state(page_radio_state_t state, const char * detail);

/**
 * Move the volume slider without firing the volume callback.
 * Use this to reflect volume changed elsewhere (a knob, Home Assistant).
 * @param volume   0..100
 */
void page_radio_set_volume(int32_t volume);

/**
 * Show search results in the add dialog. Does nothing if it is closed.
 * @param results   array of stations, or NULL with a count of 0 to clear
 * @param added     whether each is already saved, which shows a tick instead of +; @nullable
 * @param count     number of results, clamped to PAGE_RADIO_RESULT_MAX
 */
void page_radio_search_set_results(const page_radio_station_t results[], const bool added[], uint32_t count);

/**
 * Show or clear the tick on one search result.
 * @param index   index into the results
 * @param added   whether the station is saved
 */
void page_radio_search_set_added(uint32_t index, bool added);

/**
 * Redraw one search result, e.g. when its favicon arrives.
 * @param index     index into the results
 * @param station   the result's new details
 */
void page_radio_search_update_result(uint32_t index, const page_radio_station_t * station);

/**
 * A line above the results, e.g. "Searching..." or "No stations found".
 * @param text   the text, or NULL to hide the line
 */
void page_radio_search_set_status(const char * text);

/** @return   true while the add dialog is open */
bool page_radio_search_is_open(void);

/**
 * Register the station-selected callback.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_station_cb(page_radio_station_cb_t cb);

/**
 * Register the play/pause callback.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_play_cb(page_radio_play_cb_t cb);

/**
 * Register the volume callback.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_volume_cb(page_radio_volume_cb_t cb);

/**
 * Register the station-removed callback.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_remove_cb(page_radio_remove_cb_t cb);

/**
 * Register the station-moved callback.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_move_cb(page_radio_move_cb_t cb);

/**
 * Register the search callback.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_search_cb(page_radio_search_cb_t cb);

/**
 * Register the add-result callback.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_add_cb(page_radio_add_cb_t cb);

/**
 * Register the callback for a tap on a search result's tick. It removes the
 * station at once, without the confirmation the list's remove button asks.
 * @param cb   callback, or NULL to clear
 */
void page_radio_set_result_remove_cb(page_radio_result_remove_cb_t cb);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_RADIO_H*/
