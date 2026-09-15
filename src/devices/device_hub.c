/**
 * @file device_hub.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "devices/device_hub.h"
#include "devices/device_config.h"

#include "cJSON.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool          channel_take_item(device_channel_t * channel, const cJSON * item);
static bool          channel_take_text(device_channel_t * channel, const char * text);
static bool          channel_store(device_channel_t * channel, float value);
static bool          channel_store_raw(device_channel_t * channel, double raw);
static bool          color_parse(const char * text, uint32_t * rgb);
static const cJSON * json_lookup(const cJSON * doc, const char * key);
static bool          tag_extract(const char * text, const char * tag, char * out, size_t size);
static cJSON *       json_scalar(const char * text);
static char *        command_build(const device_channel_t * channel, float value);
static char *        text_dup(const char * text);
static bool          text_equals(const char * a, const char * b);
static void          number_format(char * buf, size_t size, double value);

/**********************
 *  STATIC VARIABLES
 **********************/

static device_t       devices[DEVICE_MAX];
static uint32_t       device_count;
static device_scene_t scenes[DEVICE_SCENE_MAX];
static uint32_t       scene_count;

/** Stamped on a channel each time its value changes, so the UI can tell
 *  which of two attributes the user set last. */
static uint32_t change_seq;

static device_publish_cb_t publish_cb;
static void *              publish_user;
static device_changed_cb_t changed_cb;
static void *              changed_user;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool device_hub_load(const char * json, size_t len, char * error, size_t error_len)
{
    /*The parser leaves the tables alone on failure, and a fresh parse has
     *every `known` flag cleared.*/
    return device_config_parse(json, len, devices, &device_count, scenes, &scene_count, error, error_len);
}

uint32_t device_hub_count(void)
{
    return device_count;
}

const device_t * device_hub_devices(void)
{
    return devices;
}

uint32_t device_hub_scene_count(void)
{
    return scene_count;
}

const device_scene_t * device_hub_scenes(void)
{
    return scenes;
}

uint32_t device_hub_subscriptions(const char * topics[], uint32_t max)
{
    uint32_t n = 0;

    for(uint32_t d = 0; d < device_count; d++) {
        for(uint8_t c = 0; c < devices[d].channel_count; c++) {
            const char * topic = devices[d].channels[c].state_topic;
            if(topic[0] == '\0') continue;

            /*Zigbee2MQTT devices put every channel on one topic.*/
            bool seen = false;
            for(uint32_t i = 0; i < n && !seen; i++) seen = strcmp(topics[i], topic) == 0;
            if(seen) continue;

            if(n >= max) return n;
            topics[n++] = topic;
        }
    }

    return n;
}

void device_hub_handle_message(const char * topic, const char * payload, size_t len)
{
    char * text = malloc(len + 1);
    if(!text) return;
    memcpy(text, payload, len);
    text[len] = '\0';

    /*Parsed at most once, and only if some channel on this topic wants a key.*/
    cJSON * doc       = NULL;
    bool    doc_tried = false;

    for(uint32_t d = 0; d < device_count; d++) {
        /*Reported, not just changed: a device that reports the value it
         *already had still has to pull a control the user moved back.*/
        bool reported = false;

        for(uint8_t c = 0; c < devices[d].channel_count; c++) {
            device_channel_t * channel = &devices[d].channels[c];
            if(strcmp(channel->state_topic, topic) != 0) continue;

            if(channel->key[0]) {
                if(!doc_tried) {
                    doc       = cJSON_ParseWithLength(text, len);
                    doc_tried = true;
                }

                /*A partial update that leaves this key out says nothing about it.*/
                const cJSON * item = json_lookup(doc, channel->key);
                if(!item) continue;
                channel_take_item(channel, item);
            }
            else if(channel->tag[0]) {
                char value[DEVICE_TOPIC_LEN];
                if(!tag_extract(text, channel->tag, value, sizeof(value))) continue;
                channel_take_text(channel, value);
            }
            else {
                channel_take_text(channel, text);
            }

            reported = true;
        }

        if(reported && changed_cb) changed_cb(d, changed_user);
    }

    cJSON_Delete(doc);
    free(text);
}

bool device_hub_set(uint32_t index, device_attr_t attr, float value)
{
    if(index >= device_count) return false;

    device_channel_t * channel = (device_channel_t *)device_find_channel(&devices[index], attr);
    if(!device_channel_writable(channel)) return false;

    /*Keep requests inside what the channel can represent.*/
    switch(device_channel_kind(channel)) {
        case DEVICE_VALUE_BOOL:    value = value >= 0.5f ? 1.0f : 0.0f; break;
        case DEVICE_VALUE_PERCENT: value = value < 0 ? 0 : (value > 100 ? 100 : value); break;
        case DEVICE_VALUE_NUMBER:  value = value < channel->min ? channel->min
                                           : (value > channel->max ? channel->max : value); break;
        case DEVICE_VALUE_OPTION:
            if(value < 0 || (uint32_t)value >= channel->option_count) return false;
            break;
        case DEVICE_VALUE_COLOR:   break;
    }

    char * payload = command_build(channel, value);
    if(!payload) return false;

    if(publish_cb) publish_cb(channel->command_topic, payload, publish_user);
    free(payload);

    /*Nothing will ever report back, so show the request as the state.*/
    if(channel->state_topic[0] == '\0' && channel_store(channel, value) && changed_cb) {
        changed_cb(index, changed_user);
    }

    /*Open and Close say where a curtain is going. Plenty of drivers only
     *report their position once there, or never, so the position goes to that
     *end straight away; one that reports while moving still moves it through
     *its reports. Stop assumes nothing -- where it stopped is unknowable
     *unless the driver says.*/
    if(attr == DEVICE_ATTR_COVER) {
        device_channel_t * position = (device_channel_t *)device_find_channel(&devices[index], DEVICE_ATTR_POSITION);
        const char *       name     = channel->options[(uint8_t)value];
        const char *       sent     = device_option_value(channel, (uint8_t)value);
        float              end      = -1;

        if(text_equals(name, "open") || text_equals(sent, "open")) end = 100;
        else if(text_equals(name, "close") || text_equals(sent, "close") ||
                text_equals(name, "closed") || text_equals(sent, "closed")) end = 0;

        if(position && end >= 0 && channel_store(position, end) && changed_cb) changed_cb(index, changed_user);
    }

    return true;
}

void device_hub_activate_scene(uint32_t index)
{
    if(index >= scene_count || !publish_cb) return;
    publish_cb(scenes[index].topic, scenes[index].payload, publish_user);
}

void device_hub_set_publish_cb(device_publish_cb_t cb, void * user)
{
    publish_cb   = cb;
    publish_user = user;
}

void device_hub_set_changed_cb(device_changed_cb_t cb, void * user)
{
    changed_cb   = cb;
    changed_user = user;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Take a value found under a channel's JSON key. */
static bool channel_take_item(device_channel_t * channel, const cJSON * item)
{
    device_value_kind_t kind = device_channel_kind(channel);

    if(kind == DEVICE_VALUE_COLOR && cJSON_IsObject(item)) {
        const cJSON * hex = cJSON_GetObjectItemCaseSensitive(item, "hex");
        if(cJSON_IsString(hex)) return channel_take_text(channel, hex->valuestring);

        const cJSON * r = cJSON_GetObjectItemCaseSensitive(item, "r");
        const cJSON * g = cJSON_GetObjectItemCaseSensitive(item, "g");
        const cJSON * b = cJSON_GetObjectItemCaseSensitive(item, "b");
        if(!cJSON_IsNumber(r) || !cJSON_IsNumber(g) || !cJSON_IsNumber(b)) return false;

        uint32_t rgb = ((uint32_t)r->valueint & 0xFF) << 16 | ((uint32_t)g->valueint & 0xFF) << 8 |
                       ((uint32_t)b->valueint & 0xFF);
        return channel_store(channel, (float)rgb);
    }

    if(cJSON_IsNumber(item) && (kind == DEVICE_VALUE_PERCENT || kind == DEVICE_VALUE_NUMBER)) {
        return channel_store_raw(channel, item->valuedouble);
    }

    /*Everything else is compared as text, so "on": true in the config matches
     *a JSON true as well as the string "true".*/
    char buf[DEVICE_TOPIC_LEN];
    if(cJSON_IsString(item))      snprintf(buf, sizeof(buf), "%s", item->valuestring);
    else if(cJSON_IsBool(item))   snprintf(buf, sizeof(buf), "%s", cJSON_IsTrue(item) ? "true" : "false");
    else if(cJSON_IsNumber(item)) number_format(buf, sizeof(buf), item->valuedouble);
    else return false;

    return channel_take_text(channel, buf);
}

/** Take a plain-text value. */
static bool channel_take_text(device_channel_t * channel, const char * text)
{
    while(isspace((unsigned char)*text)) text++;

    char buf[DEVICE_TOPIC_LEN];
    snprintf(buf, sizeof(buf), "%s", text);
    for(size_t n = strlen(buf); n > 0 && isspace((unsigned char)buf[n - 1]); n--) buf[n - 1] = '\0';

    switch(device_channel_kind(channel)) {
        case DEVICE_VALUE_BOOL: {
            if(text_equals(buf, channel->on))  return channel_store(channel, 1);
            if(text_equals(buf, channel->off)) return channel_store(channel, 0);
            /*Devices that were not configured exactly still usually say one
             *of these.*/
            if(text_equals(buf, "true") || text_equals(buf, "on")) return channel_store(channel, 1);
            if(text_equals(buf, "false") || text_equals(buf, "off")) return channel_store(channel, 0);
            /*Or report a level, as WLED does: anything but 0 is on.*/
            char * end;
            double level = strtod(buf, &end);
            if(end != buf && *end == '\0') return channel_store(channel, level != 0 ? 1.0f : 0.0f);
            return false;
        }

        case DEVICE_VALUE_PERCENT:
        case DEVICE_VALUE_NUMBER: {
            char * end;
            double raw = strtod(buf, &end);
            if(end == buf) return false;
            return channel_store_raw(channel, raw);
        }

        case DEVICE_VALUE_OPTION:
            for(uint8_t i = 0; i < channel->option_count; i++) {
                if(text_equals(buf, device_option_value(channel, i)) || text_equals(buf, channel->options[i])) {
                    return channel_store(channel, (float)i);
                }
            }
            return false;

        case DEVICE_VALUE_COLOR: {
            uint32_t rgb;
            return color_parse(buf, &rgb) && channel_store(channel, (float)rgb);
        }
    }

    return false;
}

/** Store a value in UI units. @return true if it differs from the last one. */
static bool channel_store(device_channel_t * channel, float value)
{
    if(channel->known && channel->value == value) return false;

    channel->known = true;
    channel->value = value;
    channel->seq   = ++change_seq;
    return true;
}

/** Store a raw number, scaling it to a percentage if the channel is one. */
static bool channel_store_raw(device_channel_t * channel, double raw)
{
    if(device_channel_kind(channel) != DEVICE_VALUE_PERCENT) return channel_store(channel, (float)raw);

    double percent = (raw - channel->min) * 100.0 / (channel->max - channel->min);
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;

    return channel_store(channel, (float)percent);
}

/** "#RRGGBB", "RRGGBB" or "r,g,b". */
static bool color_parse(const char * text, uint32_t * rgb)
{
    int r, g, b;
    if(sscanf(text, "%d,%d,%d", &r, &g, &b) == 3) {
        *rgb = ((uint32_t)r & 0xFF) << 16 | ((uint32_t)g & 0xFF) << 8 | ((uint32_t)b & 0xFF);
        return true;
    }

    if(*text == '#') text++;
    if(strlen(text) != 6) return false;

    char * end;
    unsigned long value = strtoul(text, &end, 16);
    if(*end != '\0') return false;

    *rgb = (uint32_t)value;
    return true;
}

/** Find a dotted key such as "color.hex" in a JSON document. */
static const cJSON * json_lookup(const cJSON * doc, const char * key)
{
    const cJSON * node = doc;
    char          part[DEVICE_KEY_LEN];

    while(node && *key) {
        const char * dot = strchr(key, '.');
        size_t       n   = dot ? (size_t)(dot - key) : strlen(key);
        if(n >= sizeof(part)) return NULL;

        memcpy(part, key, n);
        part[n] = '\0';

        node = cJSON_IsObject(node) ? cJSON_GetObjectItemCaseSensitive(node, part) : NULL;
        key += dot ? n + 1 : n;
    }

    return node;
}

/** Copy out the text of the first <tag>...</tag>. Enough for WLED's status
 *  XML; not an XML parser. */
static bool tag_extract(const char * text, const char * tag, char * out, size_t size)
{
    char open[DEVICE_TAG_LEN + 3];
    char close[DEVICE_TAG_LEN + 4];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);

    const char * start = strstr(text, open);
    if(!start) return false;
    start += strlen(open);

    const char * end = strstr(start, close);
    if(!end || (size_t)(end - start) >= size) return false;

    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return true;
}

/** The JSON form of a payload text: a bool or number where the text is one,
 *  a string otherwise. */
static cJSON * json_scalar(const char * text)
{
    if(strcmp(text, "true") == 0)  return cJSON_CreateTrue();
    if(strcmp(text, "false") == 0) return cJSON_CreateFalse();

    char * end;
    double number = strtod(text, &end);
    if(end != text && *end == '\0') return cJSON_CreateNumber(number);

    return cJSON_CreateString(text);
}

/** Format the command for a value. The caller frees the result. */
static char * command_build(const device_channel_t * channel, float value)
{
    char    text[DEVICE_TOPIC_LEN] = "";
    cJSON * item = NULL;

    switch(device_channel_kind(channel)) {
        case DEVICE_VALUE_BOOL: {
            const char * payload = value >= 0.5f ? channel->on : channel->off;
            snprintf(text, sizeof(text), "%s", payload);
            item = json_scalar(payload);
            break;
        }

        case DEVICE_VALUE_PERCENT:
        case DEVICE_VALUE_NUMBER: {
            double raw = value;
            if(device_channel_kind(channel) == DEVICE_VALUE_PERCENT) {
                raw = channel->min + value * (channel->max - channel->min) / 100.0;
            }
            raw = floor(raw / channel->step + 0.5) * channel->step;
            number_format(text, sizeof(text), raw);
            item = cJSON_CreateNumber(raw);
            break;
        }

        case DEVICE_VALUE_OPTION:
            snprintf(text, sizeof(text), "%s", device_option_value(channel, (uint8_t)value));
            /*A numbered option -- a WLED effect id -- goes out as a number.*/
            item = json_scalar(text);
            break;

        case DEVICE_VALUE_COLOR: {
            uint32_t rgb = (uint32_t)value & 0xFFFFFF;
            uint32_t r = rgb >> 16, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

            if(channel->color_format == DEVICE_COLOR_RGB) {
                snprintf(text, sizeof(text), "%u,%u,%u", (unsigned)r, (unsigned)g, (unsigned)b);
                item = cJSON_CreateObject();
                cJSON_AddNumberToObject(item, "r", r);
                cJSON_AddNumberToObject(item, "g", g);
                cJSON_AddNumberToObject(item, "b", b);
            }
            else {
                snprintf(text, sizeof(text), "#%06X", (unsigned)rgb);
                item = cJSON_CreateString(text);
            }
            break;
        }
    }

    if(channel->key[0] == '\0') {
        cJSON_Delete(item);

        const char * placeholder = channel->command_template[0] ? strstr(channel->command_template, "{}") : NULL;
        if(!placeholder) return text_dup(text);

        /*"FX={}" with 9 becomes "FX=9".*/
        char   filled[DEVICE_TEMPLATE_LEN + DEVICE_TOPIC_LEN];
        size_t head = (size_t)(placeholder - channel->command_template);
        snprintf(filled, sizeof(filled), "%.*s%s%s", (int)head, channel->command_template, text, placeholder + 2);
        return text_dup(filled);
    }

    /*Wrap the value in objects for each part of the key:
     *"color.hex" becomes {"color":{"hex":...}}.*/
    cJSON *      root = cJSON_CreateObject();
    cJSON *      node = root;
    const char * key  = channel->key;
    char         part[DEVICE_KEY_LEN];

    for(;;) {
        const char * dot = strchr(key, '.');
        size_t       n   = dot ? (size_t)(dot - key) : strlen(key);
        memcpy(part, key, n);
        part[n] = '\0';

        if(!dot) {
            cJSON_AddItemToObject(node, part, item);
            break;
        }

        cJSON * child = cJSON_CreateObject();
        cJSON_AddItemToObject(node, part, child);
        node = child;
        key  = dot + 1;
    }

    char * payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    /*cJSON allocates with the same malloc/free unless hooks are installed.*/
    return payload;
}

static char * text_dup(const char * text)
{
    size_t n   = strlen(text) + 1;
    char * dup = malloc(n);
    if(dup) memcpy(dup, text, n);
    return dup;
}

static bool text_equals(const char * a, const char * b)
{
    if(*b == '\0') return false;

    for(; *a && *b; a++, b++) {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return *a == *b;
}

/** Whole numbers without a decimal point, so "brightness": 128 not 128.0. */
static void number_format(char * buf, size_t size, double value)
{
    if(value == floor(value) && fabs(value) < 1e9) snprintf(buf, size, "%d", (int)value);
    else                                           snprintf(buf, size, "%g", value);
}
