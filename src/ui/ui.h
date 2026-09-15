/**
 * @file ui.h
 *
 * Entry point for the Smart Alarm Clock user interface.
 *
 * The shell owns a navigation rail on the left and a content area on the
 * right. Every page is built once at init and then shown or hidden; see
 * ui_page.h for the page contract.
 */

#ifndef UI_H
#define UI_H

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

/** Pages, in navigation-rail order. */
typedef enum {
    UI_PAGE_CLOCK,        /**< Home: time, date, weather, calendar, air summary */
    UI_PAGE_ALARMS,       /**< Alarm list and editor */
    UI_PAGE_WEATHER,      /**< Current conditions, next 20 hours, next 7 days */
    UI_PAGE_AIR_QUALITY,  /**< Sensor history, analysis and forecast */
    UI_PAGE_RADIO,        /**< Internet radio: stations, transport, volume */
    UI_PAGE_SMART_HOME,   /**< Devices: MQTT device tiles, rooms and scenes */
    UI_PAGE_SETTINGS,     /**< Wi-Fi, MQTT and device settings */
    UI_PAGE_COUNT,
} ui_page_id_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Build the whole UI on the active screen and show the clock page.
 * Call once, after lv_init() and display setup.
 */
void ui_init(void);

/**
 * Switch to a page.
 * Fires on_hide() for the outgoing page and on_show() for the incoming one.
 * Switching to the page already shown does nothing.
 * @param id   page to show
 */
void ui_navigate(ui_page_id_t id);

/**
 * @return   the page currently shown
 */
ui_page_id_t ui_current_page(void);

/**
 * Hide or show the shell's chrome -- currently just the navigation rail.
 *
 * Used by the clock page's ambient state, which wants the whole panel to
 * itself. The rail keeps its place in the layout and is only faded out, so
 * nothing else on screen shifts when it comes and goes.
 *
 * @param hidden   true to fade the chrome away, false to bring it back
 */
void ui_set_chrome_hidden(bool hidden);

/**
 * Rebuild every page in the current palette -- after a change of theme or
 * accent colour, since widgets take their colours when they are built.
 *
 * Stays on the page that was showing and keeps the user's alarms; the feeds
 * then tell the new pages what they had told the old ones. Must not run
 * inside an event of an object it deletes: from an event handler, defer it
 * with lv_async_call().
 */
void ui_rebuild(void);

/**
 * Set how long the panel may go untouched before it falls back to the
 * ambient clock face.
 * @param ms   the timeout; 0 never falls back
 */
void ui_set_idle_timeout(uint32_t ms);

/**
 * Choose what idle is: the ambient clock face, or the screen off. Switched on
 * while the screen is off, the ambient face comes back.
 * @param always_on   true for the ambient face
 */
void ui_set_always_on(bool always_on);

/**
 * Wake the screen, as a touch does: on if it is off, and the clock page out of
 * its ambient face. For an alarm, and for a face in front of the camera.
 */
void ui_wake(void);

/** @return   true while the screen is off: idle, without always-on display */
bool ui_is_screen_off(void);

/** @return   true while idle: the ambient clock face up, or the screen off */
bool ui_is_idle(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_H*/
