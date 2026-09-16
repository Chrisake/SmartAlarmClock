/**
 * @file radar_wake.h
 *
 * Wakes the screen for someone coming to the clock. While started, a thread of
 * its own reads the mmWave radar of radar_sensor.h ten times a second and
 * decides what is worth waking for. The UI takes a wake with radar_wake_take().
 *
 * What counts is a change, not a person:
 *
 *   - someone arriving in a room that has been empty,
 *   - someone coming closer -- the distance falling by a good stretch,
 *   - movement well above what has been going on lately.
 *
 * The last is what keeps a sleeping or sitting person from holding the screen
 * awake all evening. However much they move is learnt as the room's own
 * level, over some ten seconds, and only movement above that counts. Someone
 * asleep in front of the clock soon reads as part of the furniture; when they
 * get up, the movement stands out from that level at once. The learning stops
 * while something stands out, so a real event is never learnt away.
 *
 * Sensitivity, 5 to 100, sets how much of a change all three take, and how far
 * off it still counts. In a dark room -- the light sensor beside the radar
 * says -- the wake can be left alone, made less sensitive, or stopped, since
 * the one thing moving in a dark bedroom is usually asleep.
 *
 * It keeps running while the screen is awake, so that whoever is already in
 * the room is part of the level by the time the screen goes idle, and so the
 * settings page can say whether the sensor is there. A wake raised while the
 * screen is in use is simply never taken.
 *
 * Plain C with no LVGL; every call returns at once.
 */

#ifndef RADAR_WAKE_H
#define RADAR_WAKE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

/**********************
 *      TYPEDEFS
 **********************/

/** What the wake does once the room is dark. */
typedef enum {
    RADAR_WAKE_DARK_ON,        /**< Nothing changes */
    RADAR_WAKE_DARK_REDUCED,   /**< Half the sensitivity: nearer or stronger movement only */
    RADAR_WAKE_DARK_OFF,       /**< Nothing wakes the screen while it is dark */
} radar_wake_dark_t;

/** Whether the presence board is there and answering. */
typedef enum {
    RADAR_WAKE_SENSOR_UNKNOWN,   /**< Not looked at yet */
    RADAR_WAKE_SENSOR_READY,     /**< Frames are arriving */
    RADAR_WAKE_SENSOR_MISSING,   /**< It did not start, or has stopped answering */
} radar_wake_sensor_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Start watching, or carry on with new settings if already watching.
 * @param sensitivity   5..100; how little of a change wakes the screen
 * @param dark          what to do once the room is dark
 * @return              false if the thread could not be started
 */
bool radar_wake_start(uint8_t sensitivity, radar_wake_dark_t dark);

/** Stop watching and let go of the sensor. A wake not yet taken is dropped. */
void radar_wake_stop(void);

/** @return   true while started */
bool radar_wake_is_running(void);

/** @return   true, once, when something worth waking for has happened since the last call */
bool radar_wake_take(void);

/** @return   whether the sensor is there and answering, as far as is known */
radar_wake_sensor_t radar_wake_sensor(void);

/** @return   true while the light sensor says the room is dark */
bool radar_wake_room_dark(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*RADAR_WAKE_H*/
