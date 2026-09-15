/**
 * @file device.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "devices/device.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** Ends of the colour temperature ramp, cool (low mireds) then warm. The
 *  panel's slider track is drawn between the same two colours. */
#define COLOR_TEMP_COOL_RGB 0xCFE3FF
#define COLOR_TEMP_WARM_RGB 0xFFB45A

/**********************
 *  STATIC VARIABLES
 **********************/

static const char * const type_names[DEVICE_TYPE_COUNT] = {
    [DEVICE_TYPE_LIGHT]        = "light",
    [DEVICE_TYPE_LED_STRIP]    = "led_strip",
    [DEVICE_TYPE_SWITCH]       = "switch",
    [DEVICE_TYPE_PLUG]         = "plug",
    [DEVICE_TYPE_FAN]          = "fan",
    [DEVICE_TYPE_AIR_PURIFIER] = "air_purifier",
    [DEVICE_TYPE_HUMIDIFIER]   = "humidifier",
    [DEVICE_TYPE_DEHUMIDIFIER] = "dehumidifier",
    [DEVICE_TYPE_HEATER]       = "heater",
    [DEVICE_TYPE_CURTAIN]      = "curtain",
    [DEVICE_TYPE_THERMOSTAT]   = "thermostat",
    [DEVICE_TYPE_LOCK]         = "lock",
    [DEVICE_TYPE_SENSOR]       = "sensor",
    [DEVICE_TYPE_CONTACT]      = "contact",
    [DEVICE_TYPE_MOTION]       = "motion",
};

static const char * const attr_names[DEVICE_ATTR_COUNT] = {
    [DEVICE_ATTR_POWER]              = "power",
    [DEVICE_ATTR_BRIGHTNESS]         = "brightness",
    [DEVICE_ATTR_COLOR_TEMP]         = "color_temp",
    [DEVICE_ATTR_COLOR]              = "color",
    [DEVICE_ATTR_EFFECT]             = "effect",
    [DEVICE_ATTR_SPEED]              = "speed",
    [DEVICE_ATTR_MODE]               = "mode",
    [DEVICE_ATTR_POSITION]           = "position",
    [DEVICE_ATTR_COVER]              = "cover",
    [DEVICE_ATTR_TARGET_TEMPERATURE] = "target_temperature",
    [DEVICE_ATTR_TEMPERATURE]        = "temperature",
    [DEVICE_ATTR_TARGET_HUMIDITY]    = "target_humidity",
    [DEVICE_ATTR_HUMIDITY]           = "humidity",
    [DEVICE_ATTR_CO2]                = "co2",
    [DEVICE_ATTR_LOCK]               = "lock",
    [DEVICE_ATTR_CONTACT]            = "contact",
    [DEVICE_ATTR_MOTION]             = "motion",
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

device_type_t device_type_from_name(const char * name)
{
    if(!name) return DEVICE_TYPE_COUNT;

    /*The name people will look for first.*/
    if(strcmp(name, "wled") == 0) return DEVICE_TYPE_LED_STRIP;

    for(int i = 0; i < DEVICE_TYPE_COUNT; i++) {
        if(strcmp(name, type_names[i]) == 0) return (device_type_t)i;
    }
    return DEVICE_TYPE_COUNT;
}

device_attr_t device_attr_from_name(const char * name)
{
    for(int i = 0; i < DEVICE_ATTR_COUNT; i++) {
        if(name && strcmp(name, attr_names[i]) == 0) return (device_attr_t)i;
    }
    return DEVICE_ATTR_COUNT;
}

const char * device_attr_name(device_attr_t attr)
{
    return attr < DEVICE_ATTR_COUNT ? attr_names[attr] : "?";
}

void device_channel_defaults(device_channel_t * channel, device_attr_t attr)
{
    memset(channel, 0, sizeof(*channel));

    channel->attr = attr;
    channel->min  = 0;
    channel->max  = 100;
    channel->step = 1;

    /*Payloads follow the most common convention for each attribute, which is
     *also what Zigbee2MQTT and Tasmota use.*/
    switch(attr) {
        case DEVICE_ATTR_POWER:
        case DEVICE_ATTR_MOTION:
            strcpy(channel->on, "ON");
            strcpy(channel->off, "OFF");
            break;

        case DEVICE_ATTR_LOCK:
            strcpy(channel->on, "LOCK");
            strcpy(channel->off, "UNLOCK");
            break;

        case DEVICE_ATTR_CONTACT:
            strcpy(channel->on, "OPEN");
            strcpy(channel->off, "CLOSED");
            break;

        case DEVICE_ATTR_COLOR_TEMP:
            /*Mireds: 153 is cool daylight (6500 K), 500 warm (2000 K).*/
            channel->min = 153;
            channel->max = 500;
            break;

        case DEVICE_ATTR_COVER:
            strcpy(channel->options[0], "OPEN");
            strcpy(channel->options[1], "CLOSE");
            strcpy(channel->options[2], "STOP");
            channel->option_count = 3;
            break;

        case DEVICE_ATTR_TARGET_TEMPERATURE:
            channel->min  = 5;
            channel->max  = 35;
            channel->step = 0.5f;
            break;

        case DEVICE_ATTR_TARGET_HUMIDITY:
            channel->min  = 30;
            channel->max  = 80;
            channel->step = 5;
            break;

        case DEVICE_ATTR_CO2:
            channel->max = 5000;
            break;

        default:
            break;
    }
}

device_value_kind_t device_channel_kind(const device_channel_t * channel)
{
    switch(channel->attr) {
        case DEVICE_ATTR_POWER:
        case DEVICE_ATTR_LOCK:
        case DEVICE_ATTR_CONTACT:
        case DEVICE_ATTR_MOTION:
            return DEVICE_VALUE_BOOL;

        case DEVICE_ATTR_BRIGHTNESS:
        case DEVICE_ATTR_POSITION:
            return DEVICE_VALUE_PERCENT;

        case DEVICE_ATTR_SPEED:
            /*Presets when named, a percentage otherwise.*/
            return channel->option_count > 0 ? DEVICE_VALUE_OPTION : DEVICE_VALUE_PERCENT;

        case DEVICE_ATTR_MODE:
        case DEVICE_ATTR_COVER:
        case DEVICE_ATTR_EFFECT:
            return DEVICE_VALUE_OPTION;

        case DEVICE_ATTR_COLOR:
            return DEVICE_VALUE_COLOR;

        default:
            return DEVICE_VALUE_NUMBER;
    }
}

bool device_channel_writable(const device_channel_t * channel)
{
    return channel && channel->command_topic[0] != '\0';
}

const char * device_option_value(const device_channel_t * channel, uint8_t index)
{
    if(index >= channel->option_count) return "";
    return channel->values[index][0] ? channel->values[index] : channel->options[index];
}

const device_channel_t * device_find_channel(const device_t * device, device_attr_t attr)
{
    for(uint8_t i = 0; i < device->channel_count; i++) {
        if(device->channels[i].attr == attr) return &device->channels[i];
    }
    return NULL;
}

bool device_value(const device_t * device, device_attr_t attr, float * out)
{
    const device_channel_t * channel = device_find_channel(device, attr);

    if(!channel || !channel->known) return false;
    if(out) *out = channel->value;
    return true;
}

bool device_light_color(const device_t * device, uint32_t * rgb)
{
    const device_channel_t * color = device_find_channel(device, DEVICE_ATTR_COLOR);
    const device_channel_t * temp  = device_find_channel(device, DEVICE_ATTR_COLOR_TEMP);

    bool has_color = color && color->known;
    bool has_temp  = temp && temp->known;

    if(has_color && (!has_temp || color->seq >= temp->seq)) {
        *rgb = (uint32_t)color->value & 0xFFFFFF;
        return true;
    }

    if(!has_temp) return false;

    /*Blend channel by channel along the ramp.*/
    float t = (temp->value - temp->min) / (temp->max - temp->min);
    if(t < 0) t = 0;
    if(t > 1) t = 1;

    uint32_t out = 0;
    for(int shift = 16; shift >= 0; shift -= 8) {
        float cool = (float)((COLOR_TEMP_COOL_RGB >> shift) & 0xFF);
        float warm = (float)((COLOR_TEMP_WARM_RGB >> shift) & 0xFF);
        out |= (uint32_t)(cool + (warm - cool) * t + 0.5f) << shift;
    }

    *rgb = out;
    return true;
}
