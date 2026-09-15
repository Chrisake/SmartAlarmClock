/**
 * @file device_config.h
 *
 * Reads the device configuration: a JSON document listing the devices, their
 * MQTT channels, and the scene buttons.
 *
 *   {
 *     "devices": [
 *       {
 *         "name": "Bedside lamp", "type": "light", "room": "Bedroom",
 *         "state": "zigbee2mqtt/bedside", "command": "zigbee2mqtt/bedside/set",
 *         "channels": {
 *           "power":      { "key": "state" },
 *           "brightness": { "key": "brightness", "max": 254 }
 *         }
 *       },
 *       {
 *         "name": "Desk", "type": "plug",
 *         "channels": {
 *           "power": { "state": "shellies/desk/relay/0", "command": "shellies/desk/relay/0/command",
 *                      "on": "on", "off": "off" }
 *         }
 *       }
 *     ],
 *     "scenes": [
 *       { "name": "Good night", "topic": "home/scene", "payload": "night" }
 *     ]
 *   }
 *
 * Device fields: name and type are required; room, id, and default "state" /
 * "command" topics that every channel inherits are optional.
 *
 * Channel fields, all optional: state, command, key (dotted for nested JSON),
 * on, off, min, max, step, options (array of strings), format ("hex" or
 * "rgb"). A channel given as a bare string is just its state topic. Read-only
 * attributes -- temperature, humidity, co2, contact, motion -- never inherit
 * the device's command topic, and "command": "" makes any channel read-only.
 *
 * The parser only fills tables; it has no idea where the text came from. On
 * the clock it arrives as a retained MQTT message, in the simulator it is read
 * from data/devices.json.
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "devices/device.h"

#include <stddef.h>

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Parse a configuration document.
 *
 * Nothing is written to the output tables unless the whole document is valid,
 * so a broken upload leaves the previous configuration in place.
 *
 * @param json           the document; need not be NUL-terminated
 * @param len            its length in bytes
 * @param devices        receives up to DEVICE_MAX devices
 * @param device_count   receives the number of devices
 * @param scenes         receives up to DEVICE_SCENE_MAX scenes
 * @param scene_count    receives the number of scenes
 * @param error          receives a one-line description on failure; @nullable
 * @param error_len      size of `error`
 * @return               true on success
 */
bool device_config_parse(const char * json, size_t len,
                         device_t devices[], uint32_t * device_count,
                         device_scene_t scenes[], uint32_t * scene_count,
                         char * error, size_t error_len);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DEVICE_CONFIG_H*/
