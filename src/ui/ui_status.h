/**
 * @file ui_status.h
 *
 * How the UI says it has nothing to show yet, that fetching it failed, or
 * that something went wrong elsewhere.
 *
 * A status cover sits over a card, a section or a whole page, hiding the
 * empty widgets under it: a spinner while the first data is on its way, or a
 * warning, a line saying what went wrong and -- where there is something to
 * retry -- a Try again button. Once data has arrived the cover goes, and
 * stays gone: a later refresh that fails leaves the old data up and says so
 * some other way, rather than taking it away.
 *
 * A refresh control is the small glyph beside "Updated 10:15" that fetches
 * again, and turns into a spinner while it does.
 *
 * A notice is a banner across the top of the screen for a failure away from
 * the page it concerns: the forecast could not be refreshed, a sensor
 * stopped answering. It goes by itself after a few seconds, or at a tap.
 */

#ifndef UI_STATUS_H
#define UI_STATUS_H

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

typedef enum {
    UI_STATUS_LOADING,   /**< Nothing to show yet, and it is on its way */
    UI_STATUS_READY,     /**< Showing data: no cover */
    UI_STATUS_FAILED,    /**< Nothing to show, and getting it failed */
} ui_status_state_t;

typedef enum {
    UI_NOTICE_INFO,
    UI_NOTICE_ERROR,
} ui_notice_level_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Cover a parent. The cover keeps to the parent's size and corners, is kept
 * out of its layout, and starts out LOADING. Taps on it go no further than
 * the parent: a section that opens a page still does.
 *
 * Create it after the parent's padding and border are set.
 *
 * @param parent    what to cover
 * @param surface   the parent's background colour, e.g. UI_COLOR_CARD
 * @param retry     called by Try again, shown when FAILED; NULL for no button
 * @param user      passed to `retry`
 * @return          the cover
 */
lv_obj_t * ui_status_create(lv_obj_t * parent, lv_color_t surface, lv_event_cb_t retry, void * user);

/**
 * @param status    from ui_status_create(); NULL does nothing
 * @param state     what to show
 * @param message   why, when FAILED; NULL for none
 */
void ui_status_set(lv_obj_t * status, ui_status_state_t state, const char * message);

/**
 * A refresh glyph as small as the text it sits beside, but a fingertip to hit.
 * @param parent    normally a row of text
 * @param clicked   called on a tap
 * @param user      passed to `clicked`
 * @return          the control
 */
lv_obj_t * ui_refresh_create(lv_obj_t * parent, lv_event_cb_t clicked, void * user);

/**
 * @param refresh   from ui_refresh_create()
 * @param busy      true shows a spinner in place of the glyph, which cannot be tapped meanwhile
 */
void ui_refresh_set_busy(lv_obj_t * refresh, bool busy);

/**
 * Show a notice, replacing any already up.
 * @param level   error or information; picks the glyph and its colour
 * @param text    one line; copied
 */
void ui_notice_show(ui_notice_level_t level, const char * text);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_STATUS_H*/
