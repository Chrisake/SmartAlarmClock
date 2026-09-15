/**
 * @file sensor_hw_sim.c
 *
 * The simulator's sensors: a bedroom, made up from the time of day. CO2
 * builds overnight with the door shut, cooking at breakfast and dinner
 * raises the particles and gases, the temperature follows the afternoon sun,
 * and a slow random walk keeps every trace from looking drawn with a ruler.
 * The VOC and NOx indices and formaldehyde stay unknown for their first
 * seconds, as the real SEN69C's do while it warms up.
 */

#if !defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensor_hw.h"

#include <math.h>
#include <time.h>

/*********************
 *      DEFINES
 *********************/

#define TWO_PI 6.283185307179586

/** Seconds after start-up before the warming-up metrics report. */
#define WARMUP_GAS_S  30
#define WARMUP_HCHO_S 12

/**********************
 *  STATIC PROTOTYPES
 **********************/

static double noise(void);
static double wander(sensor_metric_t metric, double step, double limit);
static double peak(double hour, double centre, double width);

/**********************
 *  STATIC VARIABLES
 **********************/

static time_t   started;
static uint32_t random_state = 0x2545F491u;
static double   walk[SENSOR_METRIC_COUNT];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

uint32_t sensor_hw_read(sensor_reading_t * out)
{
    time_t now = time(NULL);
    if(!started) started = now;

    struct tm local;
#if defined(_MSC_VER)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    double hour    = local.tm_hour + local.tm_min / 60.0 + local.tm_sec / 3600.0;
    double seconds = (double)now;
    double cooking = peak(hour, 19.5, 0.5) + 0.6 * peak(hour, 8.0, 0.4);
    double asleep  = 0.5 + 0.5 * cos(TWO_PI * (hour - 3.0) / 24.0);
    double warm    = cos(TWO_PI * (hour - 15.0) / 24.0);
    bool   gas_up  = now - started >= WARMUP_GAS_S;
    bool   hcho_up = now - started >= WARMUP_HCHO_S;

    double pm25 = 5.5 + 2.0 * sin(TWO_PI * seconds / 5400.0) + 22.0 * cooking + wander(SENSOR_PM25, 0.15, 2.0);
    if(pm25 < 0.5) pm25 = 0.5;

    out->values[SENSOR_PM1]  = (float)(pm25 * 0.78);
    out->values[SENSOR_PM25] = (float)pm25;
    out->values[SENSOR_PM4]  = (float)(pm25 * 1.12);
    out->values[SENSOR_PM10] = (float)(pm25 * 1.3 + 1.5);

    out->values[SENSOR_CO2] = (float)(470.0 + 650.0 * asleep + 120.0 * cooking +
                                      40.0 * sin(TWO_PI * seconds / 1800.0) + wander(SENSOR_CO2, 4.0, 60.0));

    double voc = 90.0 + 25.0 * sin(TWO_PI * seconds / 7200.0) + 140.0 * cooking + wander(SENSOR_VOC, 1.5, 20.0);
    double nox = 1.0 + 18.0 * cooking + wander(SENSOR_NOX, 0.2, 2.0);
    out->values[SENSOR_VOC] = gas_up ? (float)(voc < 1.0 ? 1.0 : voc) : NAN;
    out->values[SENSOR_NOX] = gas_up ? (float)(nox < 1.0 ? 1.0 : nox) : NAN;

    double hcho = 16.0 + 5.0 * sin(TWO_PI * seconds / 10800.0) + 8.0 * cooking + wander(SENSOR_HCHO, 0.2, 3.0);
    out->values[SENSOR_HCHO] = hcho_up ? (float)(hcho < 0.0 ? 0.0 : hcho) : NAN;

    out->values[SENSOR_TEMPERATURE] = (float)(21.8 + 1.1 * warm + wander(SENSOR_TEMPERATURE, 0.02, 0.3));
    out->values[SENSOR_HUMIDITY]    = (float)(46.0 - 4.0 * warm + 3.0 * asleep + wander(SENSOR_HUMIDITY, 0.1, 2.0));

    return SENSOR_HW_AIR | SENSOR_HW_CLIMATE;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** xorshift32, as -1..1. */
static double noise(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (double)random_state / 2147483647.5 - 1.0;
}

/** A random walk per metric, held within +-limit. */
static double wander(sensor_metric_t metric, double step, double limit)
{
    walk[metric] += noise() * step;
    if(walk[metric] > limit) walk[metric] = limit;
    if(walk[metric] < -limit) walk[metric] = -limit;
    return walk[metric];
}

/** A bump centred on an hour of the day, 1 at its top. */
static double peak(double hour, double centre, double width)
{
    double d = (hour - centre) / width;
    return exp(-d * d);
}

#endif
