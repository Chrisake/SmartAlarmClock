/**
 * @file sensors.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensors.h"

#include <math.h>
#include <stddef.h>

/**********************
 *      TYPEDEFS
 **********************/

/** One band of the index: concentrations `c_low`..`c_high` map onto `i_low`..`i_high`. */
typedef struct {
    float c_low;
    float c_high;
    float i_low;
    float i_high;
} breakpoint_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static float band_index(const breakpoint_t table[], size_t count, float concentration);

/**********************
 *  STATIC VARIABLES
 **********************/

static const sensor_metric_info_t metrics[SENSOR_METRIC_COUNT] = {
    [SENSOR_PM1]         = {"pm1_0",       "PM1.0",       "\xC2\xB5g/m\xC2\xB3", "pm1",            1},
    [SENSOR_PM25]        = {"pm2_5",       "PM2.5",       "\xC2\xB5g/m\xC2\xB3", "pm25",           1},
    [SENSOR_PM4]         = {"pm4_0",       "PM4.0",       "\xC2\xB5g/m\xC2\xB3", NULL,             1},
    [SENSOR_PM10]        = {"pm10",        "PM10",        "\xC2\xB5g/m\xC2\xB3", "pm10",           1},
    [SENSOR_VOC]         = {"voc_index",   "VOC index",   NULL,                  NULL,             0},
    [SENSOR_NOX]         = {"nox_index",   "NOx index",   NULL,                  NULL,             0},
    [SENSOR_HCHO]        = {"hcho",        "Formaldehyde", "ppb",                NULL,             1},
    [SENSOR_CO2]         = {"co2",         "CO2",         "ppm",                 "carbon_dioxide", 0},
    [SENSOR_TEMPERATURE] = {"temperature", "Temperature", "\xC2\xB0" "C",        "temperature",    1},
    [SENSOR_HUMIDITY]    = {"humidity",    "Humidity",    "%",                   "humidity",       1},
    [SENSOR_AQI]         = {"aqi",         "Air quality index", NULL,            "aqi",            0},
};

static const breakpoint_t pm25_bands[] = {
    {0.0f,   9.0f,   0.0f,   50.0f},
    {9.1f,   35.4f,  51.0f,  100.0f},
    {35.5f,  55.4f,  101.0f, 150.0f},
    {55.5f,  125.4f, 151.0f, 200.0f},
    {125.5f, 225.4f, 201.0f, 300.0f},
    {225.5f, 325.4f, 301.0f, 500.0f},
};

static const breakpoint_t pm10_bands[] = {
    {0.0f,   54.0f,  0.0f,   50.0f},
    {55.0f,  154.0f, 51.0f,  100.0f},
    {155.0f, 254.0f, 101.0f, 150.0f},
    {255.0f, 354.0f, 151.0f, 200.0f},
    {355.0f, 424.0f, 201.0f, 300.0f},
    {425.0f, 604.0f, 301.0f, 500.0f},
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sensor_reading_clear(sensor_reading_t * reading)
{
    for(int i = 0; i < SENSOR_METRIC_COUNT; i++) reading->values[i] = NAN;
}

void sensor_reading_derive(sensor_reading_t * reading)
{
    reading->values[SENSOR_AQI] = sensor_aqi(reading->values[SENSOR_PM25], reading->values[SENSOR_PM10]);
}

const sensor_metric_info_t * sensor_metric_info(sensor_metric_t metric)
{
    return &metrics[metric < SENSOR_METRIC_COUNT ? metric : 0];
}

float sensor_aqi(float pm25, float pm10)
{
    float index = NAN;

    /*The EPA truncates before looking up: PM2.5 to a tenth, PM10 to a whole number.*/
    if(!isnan(pm25)) {
        index = band_index(pm25_bands, sizeof(pm25_bands) / sizeof(pm25_bands[0]), floorf(pm25 * 10.0f) / 10.0f);
    }
    if(!isnan(pm10)) {
        float other = band_index(pm10_bands, sizeof(pm10_bands) / sizeof(pm10_bands[0]), floorf(pm10));
        if(isnan(index) || other > index) index = other;
    }

    return isnan(index) ? NAN : roundf(index);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static float band_index(const breakpoint_t table[], size_t count, float concentration)
{
    if(concentration < 0.0f) concentration = 0.0f;

    for(size_t i = 0; i < count; i++) {
        const breakpoint_t * b = &table[i];
        if(concentration > b->c_high) continue;

        return (b->i_high - b->i_low) / (b->c_high - b->c_low) * (concentration - b->c_low) + b->i_low;
    }
    return 500.0f;
}
