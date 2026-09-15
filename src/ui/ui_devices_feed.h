/**
 * @file ui_devices_feed.h
 *
 * Connects the devices page to the device hub, and -- in the simulator --
 * stands in for the MQTT broker.
 *
 * It loads the configuration, hands the device table to the page, forwards
 * the page's requests to device_hub_set(), and redraws a tile whenever the
 * hub reports a change.
 *
 * The simulator has no broker, so this also plays one: the configuration
 * comes from data/devices.json, a few retained-style messages seed the example
 * devices' state, and every published command is echoed back on the state
 * topics that read from it after a short delay, the way a real device would
 * confirm it. Everything a real device reports is still parsed through the
 * same hub code.
 *
 * On the clock this is the seam an MQTT client replaces: subscribe to the
 * configuration topic and hand its payload to device_hub_load(); subscribe to
 * device_hub_subscriptions() and feed each message to
 * device_hub_handle_message(); publish what the hub's publish callback gives.
 */

#ifndef UI_DEVICES_FEED_H
#define UI_DEVICES_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"
#include "ui/pages/smart_home/page_smart_home.h"

/*********************
 *      DEFINES
 *********************/

/** Where the simulator reads the configuration, relative to the working
 *  directory (the project root when launched from VS Code). */
#define UI_DEVICES_CONFIG_PATH "data/devices.json"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Load the configuration and start feeding the devices page. Call once, after
 * ui_init() has built the pages.
 */
void ui_devices_feed_init(void);

/**
 * Load a new configuration, e.g. one just received on the configuration topic.
 * On failure the page shows why, and the previous devices stay.
 * @param json   the document
 * @param len    its length in bytes
 * @return       true if it was loaded
 */
bool ui_devices_feed_load(const char * json, size_t len);

/**
 * Tell the devices page again everything the feed last told it: the devices,
 * the scenes, the broker link and any notice. For after ui_rebuild(), which
 * builds the page afresh.
 */
void ui_devices_feed_republish(void);

/**
 * Show the broker connection on the devices page, and remember it for
 * ui_devices_feed_republish(). The settings feed calls this as the MQTT
 * connection comes and goes.
 * @param link     connection state
 * @param detail   e.g. the broker's host; NULL for the page's default wording
 */
void ui_devices_feed_set_link(page_devices_link_t link, const char * detail);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_DEVICES_FEED_H*/
