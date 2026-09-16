/**
 * @file radar_sensor_sim.c
 *
 * The simulator has no presence board. It reports whatever the window's keys
 * have put in front of the clock -- hal.c moves a person about with R, S and
 * A, and unplugs the board with X -- with a little noise on the movement, so
 * the wake algorithm is fed something as unsteady as a real module's frames.
 */

#if !defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "presence/radar_sensor.h"

#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

/** How much the made-up movement wanders, either way. */
#define NOISE 2.0f

/** A lit room, until the window says otherwise. */
#define DEFAULT_LUX 120.0f

/**********************
 *  STATIC VARIABLES
 **********************/

/*Written on the LVGL thread, read on radar_wake's. Each is set whole, and a
 *frame that reads one a moment late is no harm: the algorithm is looking at
 *how these move over seconds.*/
static volatile bool  connected = true;
static volatile bool  present;
static volatile float distance_cm = 300.0f;
static volatile float motion;
static volatile float lux = DEFAULT_LUX;

static bool open;

/** Noise of its own, so it does not follow the rest of the simulator's. */
static uint32_t seed = 0x5EED5EED;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static float noise(void);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool radar_sensor_open(void)
{
    open = connected;
    return open;
}

bool radar_sensor_read(radar_reading_t * out)
{
    if(!open || !connected) return false;

    float moving = motion + noise();
    if(moving < 0.0f) moving = 0.0f;

    out->present     = present;
    out->moving      = present && moving > 1.0f;
    out->distance_cm = distance_cm;
    out->motion      = present ? moving : 0.0f;

    return true;
}

void radar_sensor_close(void)
{
    open = false;
}

bool radar_sensor_light(float * lux_out)
{
    if(!connected) return false;

    *lux_out = lux;
    return true;
}

void radar_sensor_sim_set_target(bool value, float distance, float amount)
{
    present     = value;
    distance_cm = distance;
    motion      = amount;
}

void radar_sensor_sim_set_lux(float value)
{
    lux = value;
}

void radar_sensor_sim_set_connected(bool value)
{
    connected = value;
}

bool radar_sensor_sim_is_connected(void)
{
    return connected;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** @return   -NOISE..NOISE */
static float noise(void)
{
    seed = seed * 1103515245U + 12345U;
    return (float)((seed >> 16) & 0xFFFF) / 65535.0f * (2.0f * NOISE) - NOISE;
}

#endif /*!defined(ESP_PLATFORM)*/
