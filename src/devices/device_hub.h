/**
 * @file device_hub.h
 *
 * Live state of the configured devices, and the translation between that
 * state and MQTT.
 *
 * The hub owns no connection. Whatever talks to the broker feeds every
 * received message to device_hub_handle_message() and publishes whatever the
 * hub hands to its publish callback; everything in between -- matching topics
 * to channels, digging values out of JSON, scaling ranges, formatting commands
 * -- happens here. That keeps it testable in the simulator, where a loopback
 * stands in for the broker (see ui_devices_feed.c).
 *
 * Not thread-safe. On the clock the MQTT client runs its own task, so marshal
 * messages onto the UI thread (or take lv_lock()) before calling in.
 */

#ifndef DEVICE_HUB_H
#define DEVICE_HUB_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "devices/device.h"

#include <stddef.h>

/**********************
 *      TYPEDEFS
 **********************/

/** Asked to publish a message. */
typedef void (*device_publish_cb_t)(const char * topic, const char * payload, void * user);

/** A device's state changed; read it back with device_hub_devices(). */
typedef void (*device_changed_cb_t)(uint32_t index, void * user);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Replace the configuration. All state starts unknown.
 *
 * On failure the previous configuration and its state are kept, and `error`
 * says what was wrong. After a successful load, resubscribe with
 * device_hub_subscriptions(): the topics will have changed.
 *
 * @param json        the configuration document, see device_config.h
 * @param len         its length in bytes
 * @param error       receives a one-line description on failure; @nullable
 * @param error_len   size of `error`
 * @return            true if the configuration was loaded
 */
bool device_hub_load(const char * json, size_t len, char * error, size_t error_len);

/** @return   number of devices */
uint32_t device_hub_count(void);

/** @return   the device table, device_hub_count() long */
const device_t * device_hub_devices(void);

/** @return   number of scenes */
uint32_t device_hub_scene_count(void);

/** @return   the scene table, device_hub_scene_count() long */
const device_scene_t * device_hub_scenes(void);

/**
 * List the distinct topics to subscribe to.
 * @param topics   receives pointers into the device table, valid until the next load
 * @param max      size of `topics`
 * @return         number of topics written
 */
uint32_t device_hub_subscriptions(const char * topics[], uint32_t max);

/**
 * Feed one received message. Every channel reading from `topic` takes its
 * value from it, and the changed callback fires once per device that changed.
 * @param topic     topic it arrived on
 * @param payload   payload bytes; need not be NUL-terminated
 * @param len       payload length
 */
void device_hub_handle_message(const char * topic, const char * payload, size_t len);

/**
 * Ask a device to change an attribute. Publishes the command; the new state
 * is only shown once the device reports it back, unless the channel has no
 * state topic, in which case it is assumed to have worked.
 *
 * One deliberate exception: opening or closing a curtain (a cover option named
 * "open" or "close") sets its position to 100 or 0 at once. The command says
 * where it is going, and many drivers report their position only on arrival,
 * or never. Stop changes nothing.
 *
 * @param index   device index
 * @param attr    attribute to change
 * @param value   in UI units: 0/1, percent, the raw number, an option index,
 *                or 0xRRGGBB for DEVICE_ATTR_COLOR
 * @return        false if the device has no writable channel for `attr`
 */
bool device_hub_set(uint32_t index, device_attr_t attr, float value);

/**
 * Publish a scene's payload.
 * @param index   scene index
 */
void device_hub_activate_scene(uint32_t index);

/**
 * @param cb     called for every message to publish; NULL to clear
 * @param user   passed back to `cb`
 */
void device_hub_set_publish_cb(device_publish_cb_t cb, void * user);

/**
 * @param cb     called when a device's state changes; NULL to clear
 * @param user   passed back to `cb`
 */
void device_hub_set_changed_cb(device_changed_cb_t cb, void * user);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DEVICE_HUB_H*/
