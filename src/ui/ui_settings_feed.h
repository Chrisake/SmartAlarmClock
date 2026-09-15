/**
 * @file ui_settings_feed.h
 *
 * Loads, stores and applies the device settings, and answers the settings
 * page -- in the simulator, with a pretend Wi-Fi radio and broker.
 *
 * It owns everything the settings change: the palette (and the rebuild a
 * change of theme needs), the idle timeout, the screen brightness, the clock
 * format. It keeps what it last told the settings page -- networks, Wi-Fi and
 * broker status -- so a rebuilt page can be told again.
 *
 * In the simulator the settings live in data/settings.json, brightness is
 * shown by dimming the window, a scan finds a fixed list of networks after a
 * second, and connecting "works" for any open network or any password of
 * eight characters or more. On the clock, these are the seams for NVS, the
 * backlight PWM, esp_wifi and esp-mqtt; each is marked TODO.
 */

#ifndef UI_SETTINGS_FEED_H
#define UI_SETTINGS_FEED_H

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

/** Where the simulator keeps the settings, relative to the working directory. */
#define UI_SETTINGS_PATH "data/settings.json"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Read the stored settings and set the palette from them. Call before the UI
 * is built, so it is built in the right colours.
 */
void ui_settings_feed_load(void);

/**
 * Apply the rest of the settings, fill the settings page and start answering
 * it. Call once the UI is built.
 */
void ui_settings_feed_init(void);

/**
 * Tell the settings page again everything the feed last told it. For after
 * ui_rebuild().
 */
void ui_settings_feed_republish(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_SETTINGS_FEED_H*/
