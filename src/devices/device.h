/**
 * @file device.h
 *
 * The smart home device model: what a device is, which attributes it has, and
 * how each attribute maps onto MQTT.
 *
 * Plain C with no LVGL, so the MQTT client, the configuration loader and the
 * UI can all share it. Devices are described by a JSON file (see
 * device_config.h and data/devices.json); device_hub.h keeps their live state.
 *
 * A device is a type plus a set of channels. A channel binds one attribute --
 * power, brightness, position, temperature... -- to a state topic it is read
 * from and, if it can be changed, a command topic it is written to. A channel
 * with a JSON key reads and writes that key inside a JSON object; one without
 * treats the whole payload as the value. That covers both the one-object-per-
 * device style (Zigbee2MQTT, Tasmota) and the topic-per-attribute style
 * (ESPHome, Shelly, WLED).
 */

#ifndef DEVICE_H
#define DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

#define DEVICE_MAX          32   /**< Devices in one configuration */
#define DEVICE_CHANNEL_MAX  6    /**< Attributes per device */
#define DEVICE_SCENE_MAX    6
#define DEVICE_OPTION_MAX   12   /**< Choices for an option attribute, e.g. effects */

#define DEVICE_ID_LEN       32
#define DEVICE_NAME_LEN     32
#define DEVICE_TOPIC_LEN    96
#define DEVICE_KEY_LEN      32
#define DEVICE_TAG_LEN      16
#define DEVICE_TEMPLATE_LEN 32
#define DEVICE_PAYLOAD_LEN  24
#define DEVICE_OPTION_LEN   16
#define DEVICE_VALUE_LEN    12
#define DEVICE_SCENE_PAYLOAD_LEN 128

/**********************
 *      TYPEDEFS
 **********************/

/** What a device is. Decides its icon, its colour when on, and whether a tap
 *  toggles it or opens its controls. Config name in brackets. */
typedef enum {
    DEVICE_TYPE_LIGHT,         /**< [light] */
    DEVICE_TYPE_LED_STRIP,     /**< [led_strip], also [wled]: a light with animated effects */
    DEVICE_TYPE_SWITCH,        /**< [switch] */
    DEVICE_TYPE_PLUG,          /**< [plug] */
    DEVICE_TYPE_FAN,           /**< [fan] */
    DEVICE_TYPE_AIR_PURIFIER,  /**< [air_purifier] */
    DEVICE_TYPE_HUMIDIFIER,    /**< [humidifier] */
    DEVICE_TYPE_DEHUMIDIFIER,  /**< [dehumidifier] */
    DEVICE_TYPE_HEATER,        /**< [heater] */
    DEVICE_TYPE_CURTAIN,       /**< [curtain] Curtains, blinds, shutters */
    DEVICE_TYPE_THERMOSTAT,    /**< [thermostat] */
    DEVICE_TYPE_LOCK,          /**< [lock] */
    DEVICE_TYPE_SENSOR,        /**< [sensor] Temperature, humidity, CO2 */
    DEVICE_TYPE_CONTACT,       /**< [contact] Door or window sensor */
    DEVICE_TYPE_MOTION,        /**< [motion] */
    DEVICE_TYPE_COUNT,
} device_type_t;

/** Attributes a channel can carry. Config name in brackets. */
typedef enum {
    DEVICE_ATTR_POWER,               /**< [power] on/off */
    DEVICE_ATTR_BRIGHTNESS,          /**< [brightness] percent */
    DEVICE_ATTR_COLOR_TEMP,          /**< [color_temp] raw, mireds by default */
    DEVICE_ATTR_COLOR,               /**< [color] 0xRRGGBB */
    DEVICE_ATTR_EFFECT,              /**< [effect] option, e.g. a WLED animation */
    DEVICE_ATTR_SPEED,               /**< [speed] percent, or an option if options are given */
    DEVICE_ATTR_MODE,                /**< [mode] option */
    DEVICE_ATTR_POSITION,            /**< [position] percent, 100 = fully open */
    DEVICE_ATTR_COVER,               /**< [cover] option: open / close / stop */
    DEVICE_ATTR_TARGET_TEMPERATURE,  /**< [target_temperature] number */
    DEVICE_ATTR_TEMPERATURE,         /**< [temperature] number, read-only */
    DEVICE_ATTR_TARGET_HUMIDITY,     /**< [target_humidity] number */
    DEVICE_ATTR_HUMIDITY,            /**< [humidity] number, read-only */
    DEVICE_ATTR_CO2,                 /**< [co2] number, read-only */
    DEVICE_ATTR_LOCK,                /**< [lock] true = locked */
    DEVICE_ATTR_CONTACT,             /**< [contact] true = open */
    DEVICE_ATTR_MOTION,              /**< [motion] true = motion detected */
    DEVICE_ATTR_COUNT,
} device_attr_t;

/** How a channel's value is represented, in UI units. */
typedef enum {
    DEVICE_VALUE_BOOL,     /**< 0 or 1 */
    DEVICE_VALUE_PERCENT,  /**< 0..100, scaled from the channel's raw min..max */
    DEVICE_VALUE_NUMBER,   /**< The raw number, e.g. 21.5 */
    DEVICE_VALUE_OPTION,   /**< Index into the channel's options */
    DEVICE_VALUE_COLOR,    /**< 0xRRGGBB, exactly representable in a float */
} device_value_kind_t;

/** How a colour is written into a JSON command. */
typedef enum {
    DEVICE_COLOR_HEX,      /**< "#RRGGBB" */
    DEVICE_COLOR_RGB,      /**< {"r":..,"g":..,"b":..} */
} device_color_format_t;

/** One attribute of one device, and its MQTT binding. */
typedef struct {
    device_attr_t attr;

    char state_topic[DEVICE_TOPIC_LEN];    /**< Read from; empty = command only, shown optimistically */
    char command_topic[DEVICE_TOPIC_LEN];  /**< Written to; empty = read-only */
    char key[DEVICE_KEY_LEN];              /**< JSON key, dotted for nesting; empty = plain payload */
    char tag[DEVICE_TAG_LEN];              /**< Read the value between <tag> and </tag>, e.g. WLED's XML */
    char command_template[DEVICE_TEMPLATE_LEN];  /**< Plain command with {} for the value, e.g. "FX={}" */

    char on[DEVICE_PAYLOAD_LEN];           /**< Payload meaning true, for bool attributes */
    char off[DEVICE_PAYLOAD_LEN];          /**< Payload meaning false */

    float min;                             /**< Raw range; percent attributes scale from it */
    float max;
    float step;                            /**< Increment for +/- controls */

    char    options[DEVICE_OPTION_MAX][DEVICE_OPTION_LEN];  /**< Names shown on the buttons */
    char    values[DEVICE_OPTION_MAX][DEVICE_VALUE_LEN];    /**< What the device calls each one; empty = its name */
    uint8_t option_count;

    device_color_format_t color_format;

    /*Live state, owned by device_hub.*/
    bool     known;                        /**< A value has arrived */
    float    value;                        /**< In UI units, see device_value_kind_t */
    uint32_t seq;                          /**< When it last changed, in the hub's message order */
} device_channel_t;

typedef struct {
    char          id[DEVICE_ID_LEN];
    char          name[DEVICE_NAME_LEN];
    char          room[DEVICE_NAME_LEN];   /**< Empty when not given */
    device_type_t type;

    device_channel_t channels[DEVICE_CHANNEL_MAX];
    uint8_t          channel_count;
} device_t;

/** A button that publishes a fixed payload. */
typedef struct {
    char name[DEVICE_NAME_LEN];
    char topic[DEVICE_TOPIC_LEN];
    char payload[DEVICE_SCENE_PAYLOAD_LEN];
} device_scene_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @param name   config name, e.g. "air_purifier"
 * @return       the type, or DEVICE_TYPE_COUNT if unknown
 */
device_type_t device_type_from_name(const char * name);

/**
 * @param name   config name, e.g. "target_temperature"
 * @return       the attribute, or DEVICE_ATTR_COUNT if unknown
 */
device_attr_t device_attr_from_name(const char * name);

/** @return   config name of an attribute */
const char * device_attr_name(device_attr_t attr);

/**
 * Fill a channel with the defaults for its attribute: payloads, range, step
 * and options. The configuration then overrides whatever it names.
 */
void device_channel_defaults(device_channel_t * channel, device_attr_t attr);

/** @return   how the channel's value is represented */
device_value_kind_t device_channel_kind(const device_channel_t * channel);

/** @return   true if the channel can be written to */
bool device_channel_writable(const device_channel_t * channel);

/** @return   what the device calls option `index`: its value, or failing that its name */
const char * device_option_value(const device_channel_t * channel, uint8_t index);

/**
 * @return   the device's channel for an attribute, or NULL if it has none
 */
const device_channel_t * device_find_channel(const device_t * device, device_attr_t attr);

/**
 * @return   true if the device's attribute has a known value; `out` receives it
 */
bool device_value(const device_t * device, device_attr_t attr, float * out);

/**
 * The colour a light is showing, from whichever of its colour and colour
 * temperature changed last. Colour temperature is mapped onto the same
 * cool-to-warm ramp its slider draws.
 * @param rgb   receives 0xRRGGBB
 * @return      false if the light has reported neither
 */
bool device_light_color(const device_t * device, uint32_t * rgb);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DEVICE_H*/
