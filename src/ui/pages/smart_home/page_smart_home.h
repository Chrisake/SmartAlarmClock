/**
 * @file page_smart_home.h
 *
 * Devices page: one square tile per configured device, grouped by room, with
 * scene buttons underneath.
 *
 * Layout:
 *
 *   +------------------------------------------------------------+
 *   |  (wifi) broker status          [All][Bedroom][Kitchen]...  |
 *   +------------------------------------------------------------+
 *   |  +--------+ +--------+ +--------+ +--------+ +--------+    |
 *   |  | Lamp   | | TV     | | Thermo | | Climate| | Door   |    |
 *   |  |  bulb  | |  plug  | |  21.5  | |  21.5  | |  lock  |    |  (scrolls)
 *   |  |  60%   | |        | | [-][+] | |  48 %  | |        |    |
 *   |  +--------+ +--------+ +--------+ +--------+ +--------+    |
 *   +------------------------------------------------------------+
 *   |  [Morning]        [Movie]        [Good night]              |
 *   +------------------------------------------------------------+
 *
 * Each tile carries the device's name on top and its state in the middle: an
 * icon coloured when on and grey when off, a drawn curtain that opens and
 * closes with its position, or the readings of a sensor or thermostat.
 *
 * What a tap does depends on the device. Plain on/off devices -- plugs,
 * switches, heaters, lights with no other channel -- toggle. Devices with more
 * to set -- dimmable or colour lights, fans with speeds, purifiers with modes,
 * curtains, locks -- open a panel with their controls. A thermostat steps its
 * setpoint with the - and + on the tile itself. Sensors do nothing.
 *
 * The page renders the device table it is given and never changes it. Taps
 * come out through the command callback as requests; the new state only
 * appears once it is reported back through page_smart_home_update_device().
 */

#ifndef PAGE_SMART_HOME_H
#define PAGE_SMART_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_page.h"
#include "devices/device.h"

/**********************
 *      TYPEDEFS
 **********************/

/** Connection to the MQTT broker. */
typedef enum {
    PAGE_DEVICES_LINK_OFFLINE,
    PAGE_DEVICES_LINK_CONNECTING,
    PAGE_DEVICES_LINK_ONLINE,
} page_devices_link_t;

/**
 * The user asked a device to change.
 * @param index   device index in the table given to page_smart_home_set_devices()
 * @param attr    attribute to change
 * @param value   in UI units, as device_hub_set() takes them
 */
typedef void (*page_devices_command_cb_t)(uint32_t index, device_attr_t attr, float value);

/** A scene button was tapped. */
typedef void (*page_devices_scene_cb_t)(uint32_t index);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_smart_home_desc(void);

/**
 * Rebuild the tiles and the room chips. Rooms are listed in the order their
 * first device appears; devices without a room only show under "All".
 * @param devices   the device table; kept, so it must outlive the page's use
 *                  of it -- normally device_hub_devices()
 * @param count     number of devices
 */
void page_smart_home_set_devices(const device_t devices[], uint32_t count);

/**
 * Redraw one device's tile, and its panel if that is open, from the table.
 * @param index   device index
 */
void page_smart_home_update_device(uint32_t index);

/**
 * Replace the scene buttons.
 * @param scenes   scene table; names are copied
 * @param count    number of scenes, clamped to DEVICE_SCENE_MAX
 */
void page_smart_home_set_scenes(const device_scene_t scenes[], uint32_t count);

/**
 * Show the broker connection.
 * @param link     connection state
 * @param detail   e.g. the broker's host name; NULL for a default per state
 */
void page_smart_home_set_link(page_devices_link_t link, const char * detail);

/**
 * Show a message in place of the tiles, e.g. why the configuration could not
 * be loaded. It is also shown when there are no devices at all.
 * @param text   the message, or NULL to clear it
 */
void page_smart_home_set_notice(const char * text);

/** @param cb   called when the user changes a device; NULL to clear */
void page_smart_home_set_command_cb(page_devices_command_cb_t cb);

/** @param cb   called when the user taps a scene; NULL to clear */
void page_smart_home_set_scene_cb(page_devices_scene_cb_t cb);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_SMART_HOME_H*/
