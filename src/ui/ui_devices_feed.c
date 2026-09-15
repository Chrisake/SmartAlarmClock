/**
 * @file ui_devices_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_devices_feed.h"
#include "devices/device_hub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** How long the pretend devices take to confirm a command. */
#define LOOPBACK_DELAY_MS 150

/** Largest configuration file the simulator reads. */
#define CONFIG_MAX_BYTES (256 * 1024)

/**********************
 *      TYPEDEFS
 **********************/

/** A message waiting to be "received". */
typedef struct {
    char * topic;
    char * payload;
} pending_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static char * file_read(const char * path, size_t * len);
static void   seed_state(void);
static void   notice_show(const char * text);

static void publish(const char * topic, const char * payload, void * user);
static bool echo_payload(const device_channel_t * channel, const char * payload, char * out, size_t size);
static void deliver_later(const char * topic, const char * payload);
static void deliver_cb(lv_timer_t * timer);

static void device_changed(uint32_t index, void * user);
static void command_requested(uint32_t index, device_attr_t attr, float value);
static void scene_requested(uint32_t index);

/**********************
 *  STATIC VARIABLES
 **********************/

/*The state the example devices in data/devices.json report when the
 *simulator starts, as their retained messages would. Topics that no configured
 *channel reads are simply ignored, so editing the file does no harm.*/
static const struct {
    const char * topic;
    const char * payload;
} seed_messages[] = {
    {"zigbee2mqtt/bedside_lamp",
     "{\"state\":\"ON\",\"brightness\":153,\"color_temp\":370,\"color\":{\"r\":255,\"g\":215,\"b\":160}}"},
    {"esphome/bedroom/light/ceiling/state", "OFF"},
    {"zigbee2mqtt/bedroom_trv",
     "{\"current_heating_setpoint\":21.5,\"local_temperature\":20.4,\"system_mode\":\"heat\"}"},
    {"zigbee2mqtt/bedroom_curtains", "{\"position\":40,\"state\":\"OPEN\"}"},
    {"stat/dehumidifier/POWER", "ON"},
    {"stat/dehumidifier/TARGET", "50"},
    {"tele/dehumidifier/SENSOR", "{\"AM2301\":{\"Temperature\":22.1,\"Humidity\":56.3}}"},
    {"zigbee2mqtt/bedroom_sensor", "{\"temperature\":21.8,\"humidity\":47}"},
    {"shellies/tv-plug/relay/0", "on"},
    {"esphome/led_strip/state", "{\"state\":\"ON\",\"brightness\":200,\"color\":{\"r\":191,\"g\":90,\"b\":242}}"},
    {"wled/tv/g", "170"},
    {"wled/tv/c", "#FF9500"},
    {"wled/tv/v", "<?xml version=\"1.0\" ?><vs><ac>170</ac><fx>9</fx></vs>"},
    {"zigbee2mqtt/air_purifier", "{\"fan_state\":\"ON\",\"fan_mode\":\"auto\"}"},
    {"home/living_room/fan", "{\"state\":\"OFF\",\"speed\":\"medium\"}"},
    {"stat/kettle/POWER", "OFF"},
    {"zigbee2mqtt/kitchen_air", "{\"co2\":612,\"temperature\":23.4}"},
    {"stat/humidifier/POWER", "OFF"},
    {"zigbee2mqtt/front_door_lock", "{\"state\":\"LOCK\"}"},
    {"zigbee2mqtt/front_door_contact", "{\"contact\":true}"},
    {"zigbee2mqtt/hall_motion", "{\"occupancy\":false}"},
    {"shellies/heater/relay/0", "off"},
};

/*What the page was last told, kept so ui_devices_feed_republish() can tell a
 *rebuilt page the same.*/
static page_devices_link_t link_state = PAGE_DEVICES_LINK_OFFLINE;
static char                link_detail[64];
static char                notice[200];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_devices_feed_init(void)
{
    device_hub_set_publish_cb(publish, NULL);
    device_hub_set_changed_cb(device_changed, NULL);
    page_smart_home_set_command_cb(command_requested);
    page_smart_home_set_scene_cb(scene_requested);

    /*Until the settings feed reports a broker, the loopback below is the
     *broker. TODO: on the clock, CONNECTING until the MQTT client is up, then
     *load from the retained configuration topic instead of a file.*/
    ui_devices_feed_set_link(PAGE_DEVICES_LINK_ONLINE, "Simulated broker");

    size_t len  = 0;
    char * json = file_read(UI_DEVICES_CONFIG_PATH, &len);

    if(!json) {
        notice_show("No device configuration. Put one in " UI_DEVICES_CONFIG_PATH ".");
        return;
    }

    if(ui_devices_feed_load(json, len)) seed_state();
    free(json);
}

bool ui_devices_feed_load(const char * json, size_t len)
{
    char error[160];

    if(!device_hub_load(json, len, error, sizeof(error))) {
        char message[200];
        lv_snprintf(message, sizeof(message), "Device configuration not loaded: %s", error);
        LV_LOG_WARN("%s", message);
        notice_show(message);
        return false;
    }

    notice_show(NULL);
    page_smart_home_set_devices(device_hub_devices(), device_hub_count());
    page_smart_home_set_scenes(device_hub_scenes(), device_hub_scene_count());

    /* TODO: resubscribe the MQTT client to device_hub_subscriptions(). */
    return true;
}

void ui_devices_feed_republish(void)
{
    page_smart_home_set_devices(device_hub_devices(), device_hub_count());
    page_smart_home_set_scenes(device_hub_scenes(), device_hub_scene_count());
    page_smart_home_set_notice(notice[0] ? notice : NULL);
    page_smart_home_set_link(link_state, link_detail[0] ? link_detail : NULL);
}

void ui_devices_feed_set_link(page_devices_link_t link, const char * detail)
{
    link_state = link;
    lv_strlcpy(link_detail, detail ? detail : "", sizeof(link_detail));
    page_smart_home_set_link(link, detail);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void notice_show(const char * text)
{
    lv_strlcpy(notice, text ? text : "", sizeof(notice));
    page_smart_home_set_notice(text);
}

static char * file_read(const char * path, size_t * len)
{
    FILE * file = fopen(path, "rb");
    if(!file) return NULL;

    char * buf = malloc(CONFIG_MAX_BYTES);
    size_t n   = buf ? fread(buf, 1, CONFIG_MAX_BYTES, file) : 0;
    fclose(file);

    if(!buf || n == 0) {
        free(buf);
        return NULL;
    }

    *len = n;
    return buf;
}

static void seed_state(void)
{
    for(size_t i = 0; i < sizeof(seed_messages) / sizeof(seed_messages[0]); i++) {
        device_hub_handle_message(seed_messages[i].topic, seed_messages[i].payload,
                                  strlen(seed_messages[i].payload));
    }
}

/**
 * The pretend broker. Logs the message, then plays each device commanded
 * through this topic, the way the simplest real drivers behave: it confirms
 * by echoing the command back on the state topic the channel reads. A curtain
 * therefore reports nothing while moving and nothing on Stop -- its position
 * only changes when told one, or when the hub assumes an end for Open/Close.
 */
static void publish(const char * topic, const char * payload, void * user)
{
    LV_UNUSED(user);
    LV_LOG_USER("MQTT publish %s %s", topic, payload);

    const device_t * devices = device_hub_devices();

    for(uint32_t d = 0; d < device_hub_count(); d++) {
        /*One echo per state topic per device: several channels commonly share
         *both topics, and the payload is the same for all of them.*/
        const char * echoed[DEVICE_CHANNEL_MAX];
        uint32_t     echoed_count = 0;

        for(uint8_t c = 0; c < devices[d].channel_count; c++) {
            const device_channel_t * channel = &devices[d].channels[c];
            if(strcmp(channel->command_topic, topic) != 0 || channel->state_topic[0] == '\0') continue;

            bool seen = false;
            for(uint32_t i = 0; i < echoed_count && !seen; i++) seen = strcmp(echoed[i], channel->state_topic) == 0;
            if(seen) continue;

            char reply[DEVICE_TOPIC_LEN * 2];
            if(!echo_payload(channel, payload, reply, sizeof(reply))) continue;

            echoed[echoed_count++] = channel->state_topic;
            deliver_later(channel->state_topic, reply);
        }
    }
}

/**
 * What a device would report after receiving `payload` for this channel.
 * Usually the command itself; for a templated command read back through a
 * tag -- WLED's "FX=9" answered in its XML status as <fx>9</fx> -- the value
 * is pulled out of the template and wrapped in the tag.
 */
static bool echo_payload(const device_channel_t * channel, const char * payload, char * out, size_t size)
{
    const char * placeholder = channel->command_template[0] ? strstr(channel->command_template, "{}") : NULL;

    if(!channel->tag[0] || !placeholder) {
        lv_snprintf(out, size, "%s", payload);
        return true;
    }

    size_t head = (size_t)(placeholder - channel->command_template);
    size_t tail = strlen(placeholder + 2);
    size_t len  = strlen(payload);

    if(len < head + tail || strncmp(payload, channel->command_template, head) != 0 ||
       strcmp(payload + len - tail, placeholder + 2) != 0) return false;

    lv_snprintf(out, size, "<%s>%.*s</%s>", channel->tag, (int)(len - head - tail), payload + head, channel->tag);
    return true;
}

static void deliver_later(const char * topic, const char * payload)
{
    pending_t * msg = malloc(sizeof(pending_t));
    if(!msg) return;

    size_t topic_len   = strlen(topic) + 1;
    size_t payload_len = strlen(payload) + 1;

    msg->topic   = malloc(topic_len);
    msg->payload = malloc(payload_len);
    if(!msg->topic || !msg->payload) {
        free(msg->topic);
        free(msg->payload);
        free(msg);
        return;
    }
    memcpy(msg->topic, topic, topic_len);
    memcpy(msg->payload, payload, payload_len);

    lv_timer_t * timer = lv_timer_create(deliver_cb, LOOPBACK_DELAY_MS, msg);
    lv_timer_set_repeat_count(timer, 1);
}

static void deliver_cb(lv_timer_t * timer)
{
    pending_t * msg = lv_timer_get_user_data(timer);

    device_hub_handle_message(msg->topic, msg->payload, strlen(msg->payload));

    free(msg->topic);
    free(msg->payload);
    free(msg);
}

static void device_changed(uint32_t index, void * user)
{
    LV_UNUSED(user);
    page_smart_home_update_device(index);
}

static void command_requested(uint32_t index, device_attr_t attr, float value)
{
    if(!device_hub_set(index, attr, value)) {
        LV_LOG_WARN("device %u cannot set %s", (unsigned)index, device_attr_name(attr));
    }
}

static void scene_requested(uint32_t index)
{
    device_hub_activate_scene(index);
}
