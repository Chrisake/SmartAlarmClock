/**
 * @file sensor_sampler.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensor_sampler.h"
#include "sensors/sensor_hw.h"
#include "os/os_port.h"

/*********************
 *      DEFINES
 *********************/

/** Room for the I2C driver's calls. */
#define STACK_SIZE (6 * 1024)

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void sampler_main(void * arg);

/**********************
 *  STATIC VARIABLES
 **********************/

static os_mutex_t *     lock;
static sensor_reading_t latest;
static uint32_t         latest_parts;
static uint32_t         count;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool sensor_sampler_start(void)
{
    if(lock) return true;

    lock = os_mutex_create();
    if(!lock) return false;

    sensor_reading_clear(&latest);
    return os_thread_start(sampler_main, NULL, STACK_SIZE);
}

uint32_t sensor_sampler_get(sensor_reading_t * out, uint32_t * parts)
{
    if(!lock) {
        sensor_reading_clear(out);
        if(parts) *parts = 0;
        return 0;
    }

    os_mutex_lock(lock);
    *out = latest;
    if(parts) *parts = latest_parts;
    uint32_t now = count;
    os_mutex_unlock(lock);

    return now;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void sampler_main(void * arg)
{
    (void)arg;

    for(;;) {
        sensor_reading_t reading;
        sensor_reading_clear(&reading);

        uint32_t parts = sensor_hw_read(&reading);
        sensor_reading_derive(&reading);

        os_mutex_lock(lock);
        latest       = reading;
        latest_parts = parts;
        count++;
        os_mutex_unlock(lock);

        os_sleep_ms(SENSOR_SAMPLE_MS);
    }
}
