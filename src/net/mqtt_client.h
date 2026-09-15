/**
 * @file mqtt_client.h
 *
 * The clock's link to the MQTT broker, as the rest of the code sees it:
 * whether it is up, and publishing to it.
 *
 * In the simulator there is no broker. Whether the link is up follows the
 * settings feed's pretend connection, and a publish is only logged. On the
 * clock this is the seam for esp-mqtt. TODO: start the client from the
 * settings, report its connected and disconnected events through
 * mqtt_client_set_connected(), and publish with esp_mqtt_client_enqueue().
 * The device hub's commands still go through the loopback in
 * ui_devices_feed.c, and move here along with the real client.
 *
 * Plain C with no LVGL. Call from the LVGL thread.
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>

/**********************
 *      TYPEDEFS
 **********************/

typedef void (*mqtt_client_connected_cb_t)(void * user);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Report the link going up or down. Coming up fires the connected callback.
 * @param connected   true once the broker has accepted the connection
 */
void mqtt_client_set_connected(bool connected);

/** @return   true while the broker connection is up */
bool mqtt_client_is_connected(void);

/**
 * @param cb     called every time the link comes up -- the moment to publish
 *               retained announcements; NULL to clear
 * @param user   passed back to `cb`
 */
void mqtt_client_set_connected_cb(mqtt_client_connected_cb_t cb, void * user);

/**
 * Publish a message. Nothing is kept for later: while the link is down the
 * message is dropped.
 * @param topic     the topic
 * @param payload   NUL-terminated payload
 * @param retain    ask the broker to keep it for later subscribers
 * @return          false if the link is down
 */
bool mqtt_client_publish(const char * topic, const char * payload, bool retain);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*MQTT_CLIENT_H*/
