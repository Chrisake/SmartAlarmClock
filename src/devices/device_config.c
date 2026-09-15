/**
 * @file device_config.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "devices/device_config.h"

#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**********************
 *      TYPEDEFS
 **********************/

/** Where an error happened, for the message. */
typedef struct {
    char * buf;
    size_t len;
    int    device;        /**< Index, or -1 outside the device list */
    const char * name;    /**< Device name once known */
} error_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool fail(error_ctx_t * ctx, const char * fmt, ...);
static bool copy_text(error_ctx_t * ctx, char * dst, size_t size, const cJSON * item, const char * what);
static bool copy_scalar(error_ctx_t * ctx, char * dst, size_t size, const cJSON * item, const char * what);
static bool device_parse(error_ctx_t * ctx, device_t * device, const cJSON * json);
static bool channel_parse(error_ctx_t * ctx, device_channel_t * channel, device_attr_t attr,
                          const cJSON * json, const char * state, const char * command);
static bool scene_parse(error_ctx_t * ctx, device_scene_t * scene, const cJSON * json, int index);
static bool attr_read_only(device_attr_t attr);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool device_config_parse(const char * json, size_t len,
                         device_t devices[], uint32_t * device_count,
                         device_scene_t scenes[], uint32_t * scene_count,
                         char * error, size_t error_len)
{
    error_ctx_t ctx = {error, error_len, -1, NULL};
    if(error && error_len) error[0] = '\0';

    cJSON * root = cJSON_ParseWithLength(json, len);
    if(!root) {
        /*cJSON points at where it gave up; a few characters of context is
         *usually enough to find the stray comma.*/
        const char * at = cJSON_GetErrorPtr();
        return fail(&ctx, "not valid JSON near \"%.20s\"", at ? at : "");
    }

    /*Parse into scratch tables so a bad document changes nothing. They are
     *too big for a task stack, so they come from the heap.*/
    device_t *       dev_tmp   = calloc(DEVICE_MAX, sizeof(device_t));
    device_scene_t * scene_tmp = calloc(DEVICE_SCENE_MAX, sizeof(device_scene_t));
    uint32_t         dev_n     = 0;
    uint32_t         scene_n   = 0;
    bool             ok        = dev_tmp && scene_tmp;

    if(!ok) fail(&ctx, "out of memory");

    const cJSON * list = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if(ok && !cJSON_IsArray(list)) ok = fail(&ctx, "missing \"devices\" array");

    const cJSON * item;
    if(ok) {
        cJSON_ArrayForEach(item, list) {
            if(dev_n >= DEVICE_MAX) {
                ok = fail(&ctx, "more than %d devices", DEVICE_MAX);
                break;
            }
            ctx.device = (int)dev_n;
            ctx.name   = NULL;
            if(!device_parse(&ctx, &dev_tmp[dev_n], item)) {
                ok = false;
                break;
            }
            dev_n++;
        }
    }

    ctx.device = -1;

    const cJSON * scene_list = cJSON_GetObjectItemCaseSensitive(root, "scenes");
    if(ok && scene_list && !cJSON_IsArray(scene_list)) ok = fail(&ctx, "\"scenes\" must be an array");

    if(ok && scene_list) {
        cJSON_ArrayForEach(item, scene_list) {
            if(scene_n >= DEVICE_SCENE_MAX) {
                ok = fail(&ctx, "more than %d scenes", DEVICE_SCENE_MAX);
                break;
            }
            if(!scene_parse(&ctx, &scene_tmp[scene_n], item, (int)scene_n)) {
                ok = false;
                break;
            }
            scene_n++;
        }
    }

    if(ok) {
        memcpy(devices, dev_tmp, dev_n * sizeof(device_t));
        memcpy(scenes, scene_tmp, scene_n * sizeof(device_scene_t));
        *device_count = dev_n;
        *scene_count  = scene_n;
    }

    free(dev_tmp);
    free(scene_tmp);
    cJSON_Delete(root);

    return ok;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Record an error, prefixed with the device it concerns. Always false. */
static bool fail(error_ctx_t * ctx, const char * fmt, ...)
{
    if(!ctx->buf || ctx->len == 0) return false;

    int used = 0;
    if(ctx->device >= 0) {
        used = ctx->name ? snprintf(ctx->buf, ctx->len, "device %d (%s): ", ctx->device + 1, ctx->name)
                         : snprintf(ctx->buf, ctx->len, "device %d: ", ctx->device + 1);
        if(used < 0 || (size_t)used >= ctx->len) return false;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->buf + used, ctx->len - (size_t)used, fmt, args);
    va_end(args);

    return false;
}

/** Copy a string field, refusing rather than truncating: a clipped topic
 *  would silently never match. */
static bool copy_text(error_ctx_t * ctx, char * dst, size_t size, const cJSON * item, const char * what)
{
    if(!cJSON_IsString(item)) return fail(ctx, "\"%s\" must be a string", what);
    if(strlen(item->valuestring) >= size) {
        return fail(ctx, "\"%s\" is longer than %d characters", what, (int)size - 1);
    }
    strcpy(dst, item->valuestring);
    return true;
}

/** Like copy_text(), but also accepts a bool or number and stores its text,
 *  so "on": true and "on": 1 work as well as "on": "ON". */
static bool copy_scalar(error_ctx_t * ctx, char * dst, size_t size, const cJSON * item, const char * what)
{
    if(cJSON_IsBool(item)) {
        snprintf(dst, size, "%s", cJSON_IsTrue(item) ? "true" : "false");
        return true;
    }
    if(cJSON_IsNumber(item)) {
        snprintf(dst, size, "%g", item->valuedouble);
        return true;
    }
    return copy_text(ctx, dst, size, item, what);
}

static bool device_parse(error_ctx_t * ctx, device_t * device, const cJSON * json)
{
    if(!cJSON_IsObject(json)) return fail(ctx, "must be an object");

    memset(device, 0, sizeof(*device));

    const cJSON * name = cJSON_GetObjectItemCaseSensitive(json, "name");
    if(!name) return fail(ctx, "missing \"name\"");
    if(!copy_text(ctx, device->name, sizeof(device->name), name, "name")) return false;
    ctx->name = device->name;

    const cJSON * type = cJSON_GetObjectItemCaseSensitive(json, "type");
    if(!cJSON_IsString(type)) return fail(ctx, "missing \"type\"");
    device->type = device_type_from_name(type->valuestring);
    if(device->type == DEVICE_TYPE_COUNT) return fail(ctx, "unknown type \"%s\"", type->valuestring);

    const cJSON * id = cJSON_GetObjectItemCaseSensitive(json, "id");
    if(id) {
        if(!copy_text(ctx, device->id, sizeof(device->id), id, "id")) return false;
    }
    else {
        snprintf(device->id, sizeof(device->id), "%s", device->name);
    }

    const cJSON * room = cJSON_GetObjectItemCaseSensitive(json, "room");
    if(room && !copy_text(ctx, device->room, sizeof(device->room), room, "room")) return false;

    /*Device-wide topics every channel inherits unless it names its own.*/
    char state[DEVICE_TOPIC_LEN]   = "";
    char command[DEVICE_TOPIC_LEN] = "";

    const cJSON * item = cJSON_GetObjectItemCaseSensitive(json, "state");
    if(item && !copy_text(ctx, state, sizeof(state), item, "state")) return false;
    item = cJSON_GetObjectItemCaseSensitive(json, "command");
    if(item && !copy_text(ctx, command, sizeof(command), item, "command")) return false;

    const cJSON * channels = cJSON_GetObjectItemCaseSensitive(json, "channels");
    if(!cJSON_IsObject(channels)) return fail(ctx, "missing \"channels\" object");

    cJSON_ArrayForEach(item, channels) {
        device_attr_t attr = device_attr_from_name(item->string);
        if(attr == DEVICE_ATTR_COUNT) return fail(ctx, "unknown channel \"%s\"", item->string);
        if(device_find_channel(device, attr)) return fail(ctx, "channel \"%s\" given twice", item->string);
        if(device->channel_count >= DEVICE_CHANNEL_MAX) {
            return fail(ctx, "more than %d channels", DEVICE_CHANNEL_MAX);
        }

        device_channel_t * channel = &device->channels[device->channel_count];
        if(!channel_parse(ctx, channel, attr, item, state, command)) return false;
        device->channel_count++;
    }

    if(device->channel_count == 0) return fail(ctx, "has no channels");

    return true;
}

static bool channel_parse(error_ctx_t * ctx, device_channel_t * channel, device_attr_t attr,
                          const cJSON * json, const char * state, const char * command)
{
    device_channel_defaults(channel, attr);

    const char * what = device_attr_name(attr);

    strcpy(channel->state_topic, state);
    if(!attr_read_only(attr)) strcpy(channel->command_topic, command);

    /*Shorthand: a bare string is the state topic of a read-only channel.*/
    if(cJSON_IsString(json)) {
        channel->command_topic[0] = '\0';
        return copy_text(ctx, channel->state_topic, sizeof(channel->state_topic), json, what);
    }
    if(!cJSON_IsObject(json)) return fail(ctx, "channel \"%s\" must be an object or a topic", what);

    const cJSON * item;

    if((item = cJSON_GetObjectItemCaseSensitive(json, "state")) != NULL &&
       !copy_text(ctx, channel->state_topic, sizeof(channel->state_topic), item, "state")) return false;

    if((item = cJSON_GetObjectItemCaseSensitive(json, "command")) != NULL) {
        if(attr_read_only(attr)) return fail(ctx, "\"%s\" is read-only and cannot have a command", what);
        if(!copy_text(ctx, channel->command_topic, sizeof(channel->command_topic), item, "command")) return false;
    }

    if((item = cJSON_GetObjectItemCaseSensitive(json, "key")) != NULL &&
       !copy_text(ctx, channel->key, sizeof(channel->key), item, "key")) return false;

    if((item = cJSON_GetObjectItemCaseSensitive(json, "on")) != NULL &&
       !copy_scalar(ctx, channel->on, sizeof(channel->on), item, "on")) return false;

    if((item = cJSON_GetObjectItemCaseSensitive(json, "off")) != NULL &&
       !copy_scalar(ctx, channel->off, sizeof(channel->off), item, "off")) return false;

    static const char * const numbers[] = {"min", "max", "step"};
    float * targets[] = {&channel->min, &channel->max, &channel->step};

    for(size_t i = 0; i < 3; i++) {
        item = cJSON_GetObjectItemCaseSensitive(json, numbers[i]);
        if(!item) continue;
        if(!cJSON_IsNumber(item)) return fail(ctx, "\"%s\" of \"%s\" must be a number", numbers[i], what);
        *targets[i] = (float)item->valuedouble;
    }
    if(channel->max <= channel->min) return fail(ctx, "\"%s\" needs max greater than min", what);
    if(channel->step <= 0) return fail(ctx, "\"%s\" needs a positive step", what);

    if((item = cJSON_GetObjectItemCaseSensitive(json, "options")) != NULL) {
        if(!cJSON_IsArray(item)) return fail(ctx, "\"options\" of \"%s\" must be an array", what);

        channel->option_count = 0;
        const cJSON * option;
        cJSON_ArrayForEach(option, item) {
            if(channel->option_count >= DEVICE_OPTION_MAX) {
                return fail(ctx, "\"%s\" has more than %d options", what, DEVICE_OPTION_MAX);
            }
            if(!copy_scalar(ctx, channel->options[channel->option_count], DEVICE_OPTION_LEN, option,
                            "options")) return false;
            channel->option_count++;
        }
    }

    /*What the device calls each option, when that is not its name -- WLED
     *numbers its effects, for instance.*/
    if((item = cJSON_GetObjectItemCaseSensitive(json, "values")) != NULL) {
        if(!cJSON_IsArray(item)) return fail(ctx, "\"values\" of \"%s\" must be an array", what);
        if(cJSON_GetArraySize(item) != channel->option_count) {
            return fail(ctx, "\"%s\" needs one value per option", what);
        }

        uint8_t       i = 0;
        const cJSON * value;
        cJSON_ArrayForEach(value, item) {
            if(!copy_scalar(ctx, channel->values[i++], DEVICE_VALUE_LEN, value, "values")) return false;
        }
    }

    if((item = cJSON_GetObjectItemCaseSensitive(json, "format")) != NULL) {
        if(cJSON_IsString(item) && strcmp(item->valuestring, "rgb") == 0) channel->color_format = DEVICE_COLOR_RGB;
        else if(cJSON_IsString(item) && strcmp(item->valuestring, "hex") == 0) channel->color_format = DEVICE_COLOR_HEX;
        else return fail(ctx, "\"format\" of \"%s\" must be \"hex\" or \"rgb\"", what);
    }

    if((item = cJSON_GetObjectItemCaseSensitive(json, "template")) != NULL) {
        if(!copy_text(ctx, channel->command_template, sizeof(channel->command_template), item, "template")) {
            return false;
        }
        if(!strstr(channel->command_template, "{}")) return fail(ctx, "\"template\" of \"%s\" needs a {}", what);
    }

    if((item = cJSON_GetObjectItemCaseSensitive(json, "tag")) != NULL &&
       !copy_text(ctx, channel->tag, sizeof(channel->tag), item, "tag")) return false;

    if(channel->tag[0] && channel->key[0]) return fail(ctx, "\"%s\" cannot have both a key and a tag", what);

    if(channel->state_topic[0] == '\0' && channel->command_topic[0] == '\0') {
        return fail(ctx, "channel \"%s\" has no state or command topic", what);
    }

    if((attr == DEVICE_ATTR_MODE || attr == DEVICE_ATTR_EFFECT) && channel->option_count == 0) {
        return fail(ctx, "channel \"%s\" needs \"options\"", what);
    }

    return true;
}

static bool scene_parse(error_ctx_t * ctx, device_scene_t * scene, const cJSON * json, int index)
{
    memset(scene, 0, sizeof(*scene));

    if(!cJSON_IsObject(json)) return fail(ctx, "scene %d must be an object", index + 1);

    const cJSON * item = cJSON_GetObjectItemCaseSensitive(json, "name");
    if(!item) return fail(ctx, "scene %d is missing \"name\"", index + 1);
    if(!copy_text(ctx, scene->name, sizeof(scene->name), item, "name")) return false;

    item = cJSON_GetObjectItemCaseSensitive(json, "topic");
    if(!item) return fail(ctx, "scene \"%s\" is missing \"topic\"", scene->name);
    if(!copy_text(ctx, scene->topic, sizeof(scene->topic), item, "topic")) return false;

    /*The payload may be written as JSON rather than as a string of JSON.*/
    item = cJSON_GetObjectItemCaseSensitive(json, "payload");
    if(cJSON_IsObject(item) || cJSON_IsArray(item)) {
        char * text = cJSON_PrintUnformatted(item);
        bool fits = text && strlen(text) < sizeof(scene->payload);
        if(fits) strcpy(scene->payload, text);
        cJSON_free(text);
        if(!fits) return fail(ctx, "payload of scene \"%s\" is too long", scene->name);
    }
    else if(item && !copy_scalar(ctx, scene->payload, sizeof(scene->payload), item, "payload")) {
        return false;
    }

    return true;
}

/** Attributes that only ever report. */
static bool attr_read_only(device_attr_t attr)
{
    switch(attr) {
        case DEVICE_ATTR_TEMPERATURE:
        case DEVICE_ATTR_HUMIDITY:
        case DEVICE_ATTR_CO2:
        case DEVICE_ATTR_CONTACT:
        case DEVICE_ATTR_MOTION:
            return true;
        default:
            return false;
    }
}
