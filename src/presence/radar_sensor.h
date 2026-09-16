/**
 * @file radar_sensor.h
 *
 * The presence board in front of the clock: a 24 GHz mmWave radar module that
 * says whether anyone is in the room, how far off they are and how much they
 * are moving, and beside it an ambient light sensor, so movement wake can tell
 * a dark room from a lit one.
 *
 * Two implementations, picked at build time: radar_sensor_esp.c talks to an
 * HLK-LD2410 over a UART and to the light sensor over I2C; radar_sensor_sim.c
 * has neither and makes up a room the simulator's keys move people around in.
 *
 * A reading is what the module reports about the nearest person, not about the
 * room as a whole: radar cannot count people, only see the loudest reflection.
 *
 * Blocking: a reading waits for the module's next frame, some tens of
 * milliseconds, so these are only ever called from radar_wake's thread.
 */

#ifndef RADAR_SENSOR_H
#define RADAR_SENSOR_H

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

/** What the module sees at one moment. */
typedef struct {
    bool  present;       /**< Someone is there, moving or still */
    bool  moving;        /**< ...and moving, rather than only breathing */
    float distance_cm;   /**< How far off the nearest one is; ignored when nobody is there */
    float motion;        /**< 0..100, how strong the movement is; 0 while nobody moves */
} radar_reading_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Start the module, if it is not running already.
 * @return   false if it is not there or does not answer
 */
bool radar_sensor_open(void);

/**
 * Wait for the next frame and read it.
 * @param out   receives what the module sees
 * @return      false if no frame came, which after a few tries means the
 *              module has stopped answering
 */
bool radar_sensor_read(radar_reading_t * out);

/** Stop the module. Safe when not open. */
void radar_sensor_close(void);

/**
 * Read the light sensor beside the radar. Works whether or not the radar is
 * open, and a board without one simply has no reading.
 * @param lux   receives the light falling on the clock's face
 * @return      false if there is no light sensor, or it did not answer
 */
bool radar_sensor_light(float * lux);

#if !defined(ESP_PLATFORM)
/**
 * The simulator's stand-in for the radar: where the person in front of the
 * clock is and how much they are moving.
 * @param present      whether anyone is there at all
 * @param distance_cm  how far off they are
 * @param motion       0..100, how much they are moving
 */
void radar_sensor_sim_set_target(bool present, float distance_cm, float motion);

/** The simulator's stand-in for the light sensor. */
void radar_sensor_sim_set_lux(float lux);

/** Unplug the presence board, or plug it back in, to try what happens without it. */
void radar_sensor_sim_set_connected(bool connected);

/** @return   whether the simulated board is plugged in */
bool radar_sensor_sim_is_connected(void);
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*RADAR_SENSOR_H*/
