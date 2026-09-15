/**
 * @file mqtt_client.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "net/mqtt_client.h"

#include <stdio.h>

/**********************
 *  STATIC VARIABLES
 **********************/

static bool                       connected;
static mqtt_client_connected_cb_t connected_cb;
static void *                     connected_user;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void mqtt_client_set_connected(bool value)
{
    bool came_up = value && !connected;

    connected = value;
    if(came_up && connected_cb) connected_cb(connected_user);
}

bool mqtt_client_is_connected(void)
{
    return connected;
}

void mqtt_client_set_connected_cb(mqtt_client_connected_cb_t cb, void * user)
{
    connected_cb   = cb;
    connected_user = user;
}

bool mqtt_client_publish(const char * topic, const char * payload, bool retain)
{
    if(!connected || !topic || !topic[0]) return false;

    /* TODO: on the clock, esp_mqtt_client_enqueue(client, topic, payload, 0, 1, retain, true). */
    printf("MQTT publish%s %s %s\n", retain ? " (retained)" : "", topic, payload);
    return true;
}
