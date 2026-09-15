/**
 * @file smart_home_tile.c
 *
 * One square tile per device: the name along the top, the state in the
 * middle, and for some devices a short reading underneath.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/smart_home/smart_home_private.h"
#include "ui/ui_theme.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** The drawn curtain: a rod over a window, with a panel hanging from each end. */
#define CURTAIN_WIDTH   64
#define CURTAIN_HEIGHT  52
#define CURTAIN_ROD     4
#define CURTAIN_PANEL_MIN 5   /**< A fully open panel still shows, bunched at the side */

/** Thermostat - and +: square, and sized for a thumb now they have a row of
 *  their own. */
#define STEP_BUTTON_SIZE 44

/** U+E2CA, magic wand with sparkles: in ui_font_icons_48 for LED strips. */
#define GLYPH_EFFECTS "\xEE\x8B\x8A"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    lv_obj_t * icon;        /**< Glyph devices */
    lv_obj_t * curtain_l;   /**< Curtains */
    lv_obj_t * curtain_r;
    lv_obj_t * primary;     /**< Sensor reading, thermostat setpoint */
    lv_obj_t * secondary;   /**< Second sensor reading, room temperature or humidity */
    lv_obj_t * minus;       /**< Thermostat */
    lv_obj_t * plus;
    lv_obj_t * target;      /**< (De)humidifiers: the humidity they are working towards */
    lv_obj_t * detail;      /**< Brightness, speed, position... */
} tile_parts_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void       face_glyph_create(tile_parts_t * parts, lv_obj_t * face, const device_t * device);
static void       face_curtain_create(tile_parts_t * parts, lv_obj_t * face);
static void       face_sensor_create(tile_parts_t * parts, lv_obj_t * face);
static void       face_thermostat_create(tile_parts_t * parts, lv_obj_t * face, uint32_t index);
static lv_obj_t * step_button_create(lv_obj_t * parent, const char * symbol, lv_event_cb_t cb, uint32_t index);

static void refresh_glyph(tile_parts_t * parts, const device_t * device, char * detail, size_t size);
static void refresh_curtain(tile_parts_t * parts, const device_t * device, char * detail, size_t size);
static void refresh_sensor(tile_parts_t * parts, const device_t * device);
static void refresh_thermostat(tile_parts_t * parts, const device_t * device);

static bool         curtain_open_percent(const device_t * device, float * percent);
static const char * option_text(const device_t * device, device_attr_t attr);
static bool         format_reading(const device_t * device, device_attr_t attr, char * buf, size_t size);

static void tile_clicked(lv_event_t * e);
static void minus_clicked(lv_event_t * e);
static void plus_clicked(lv_event_t * e);
static void setpoint_step(uint32_t index, int direction);

/**********************
 *  STATIC VARIABLES
 **********************/

static tile_parts_t parts_table[DEVICE_MAX];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * sh_tile_create(lv_obj_t * parent, uint32_t index, int32_t size)
{
    const device_t * device = sh_device(index);
    tile_parts_t *   parts  = &parts_table[index];

    memset(parts, 0, sizeof(*parts));

    lv_obj_t * tile = ui_tile_create(parent);
    lv_obj_set_size(tile, size, size);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 2, LV_PART_MAIN);

    if(sh_device_tap(device) != SH_TAP_NONE) {
        lv_obj_set_style_bg_color(tile, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile, tile_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)index);
    }
    else {
        lv_obj_set_clickable(tile, false);
    }

    lv_obj_t * name = ui_label_create(tile, device->name, UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ui_label_single_line(name, UI_FONT_SM);

    /*Everything between the name and the detail line, centred in it.*/
    lv_obj_t * face = sh_box_create(tile);
    lv_obj_set_width(face, LV_PCT(100));
    lv_obj_set_flex_grow(face, 1);
    lv_obj_set_flex_flow(face, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(face, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(face, 2, LV_PART_MAIN);

    switch(device->type) {
        case DEVICE_TYPE_CURTAIN:    face_curtain_create(parts, face); break;
        case DEVICE_TYPE_SENSOR:     face_sensor_create(parts, face); break;
        case DEVICE_TYPE_THERMOSTAT: face_thermostat_create(parts, face, index); break;
        default:                     face_glyph_create(parts, face, device); break;
    }

    /*Always takes its line, even empty, so icons sit at the same height in
     *every tile whether or not they have a detail.*/
    parts->detail = ui_label_create(tile, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(parts->detail, LV_PCT(100));
    lv_obj_set_style_text_align(parts->detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ui_label_single_line(parts->detail, UI_FONT_XS);

    /*Except where the face already carries the readings -- (de)humidifiers,
     *and thermostats with their row of buttons -- and wants the line's height.*/
    if(parts->target || parts->minus) lv_obj_set_hidden(parts->detail, true);

    sh_tile_refresh(tile, index);

    return tile;
}

void sh_tile_refresh(lv_obj_t * tile, uint32_t index)
{
    LV_UNUSED(tile);

    const device_t * device = sh_device(index);
    tile_parts_t *   parts  = &parts_table[index];
    char             detail[40] = "";

    if(!device) return;

    switch(device->type) {
        case DEVICE_TYPE_CURTAIN:    refresh_curtain(parts, device, detail, sizeof(detail)); break;
        case DEVICE_TYPE_SENSOR:     refresh_sensor(parts, device); break;
        case DEVICE_TYPE_THERMOSTAT: refresh_thermostat(parts, device); break;
        default:                     refresh_glyph(parts, device, detail, sizeof(detail)); break;
    }

    lv_label_set_text(parts->detail, detail);
}

sh_tap_t sh_device_tap(const device_t * device)
{
    const device_channel_t * power = device_find_channel(device, DEVICE_ATTR_POWER);
    sh_tap_t toggle = device_channel_writable(power) ? SH_TAP_TOGGLE : SH_TAP_NONE;

    switch(device->type) {
        case DEVICE_TYPE_SENSOR:
        case DEVICE_TYPE_CONTACT:
        case DEVICE_TYPE_MOTION:
            return SH_TAP_NONE;

        case DEVICE_TYPE_CURTAIN:
        case DEVICE_TYPE_LOCK:
            return SH_TAP_PANEL;

        case DEVICE_TYPE_LIGHT:
        case DEVICE_TYPE_LED_STRIP:
            if(device_find_channel(device, DEVICE_ATTR_BRIGHTNESS) ||
               device_find_channel(device, DEVICE_ATTR_COLOR_TEMP) ||
               device_find_channel(device, DEVICE_ATTR_COLOR) ||
               device_find_channel(device, DEVICE_ATTR_EFFECT)) return SH_TAP_PANEL;
            return toggle;

        case DEVICE_TYPE_FAN:
        case DEVICE_TYPE_AIR_PURIFIER:
        case DEVICE_TYPE_HUMIDIFIER:
        case DEVICE_TYPE_DEHUMIDIFIER:
            if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_SPEED)) ||
               device_channel_writable(device_find_channel(device, DEVICE_ATTR_MODE)) ||
               device_channel_writable(device_find_channel(device, DEVICE_ATTR_TARGET_HUMIDITY))) {
                return SH_TAP_PANEL;
            }
            return toggle;

        case DEVICE_TYPE_THERMOSTAT:
            /*The setpoint is on the tile; the panel only adds mode and power.*/
            if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_MODE)) || power) {
                return SH_TAP_PANEL;
            }
            return SH_TAP_NONE;

        default:
            return toggle;
    }
}

bool sh_device_is_on(const device_t * device)
{
    float value;

    switch(device->type) {
        case DEVICE_TYPE_SENSOR:
            return true;

        case DEVICE_TYPE_LOCK:
            return device_value(device, DEVICE_ATTR_LOCK, &value) && value > 0.5f;

        case DEVICE_TYPE_CONTACT:
            return device_value(device, DEVICE_ATTR_CONTACT, &value) && value > 0.5f;

        case DEVICE_TYPE_MOTION:
            return device_value(device, DEVICE_ATTR_MOTION, &value) && value > 0.5f;

        case DEVICE_TYPE_CURTAIN:
            return curtain_open_percent(device, &value) && value > 0;

        case DEVICE_TYPE_THERMOSTAT: {
            if(device_value(device, DEVICE_ATTR_POWER, &value)) return value > 0.5f;
            const char * mode = option_text(device, DEVICE_ATTR_MODE);
            return !mode || lv_strcmp(mode, "off") != 0;
        }

        default:
            if(device_value(device, DEVICE_ATTR_POWER, &value)) return value > 0.5f;
            /*A light with only a brightness channel is on when it is lit.*/
            return device_value(device, DEVICE_ATTR_BRIGHTNESS, &value) && value > 0;
    }
}

lv_color_t sh_device_on_color(const device_t * device)
{
    uint32_t rgb;

    switch(device->type) {
        case DEVICE_TYPE_LIGHT:
        case DEVICE_TYPE_LED_STRIP:
            /*Whichever the light was set to last, a colour or a colour
             *temperature -- unless that is too dark to tell from off.*/
            if(device_light_color(device, &rgb)) {
                lv_color_t color = lv_color_hex(rgb);
                if(lv_color_brightness(color) > 60) return color;
            }
            return UI_COLOR_SUN;

        case DEVICE_TYPE_PLUG:
        case DEVICE_TYPE_AIR_PURIFIER:
        case DEVICE_TYPE_LOCK:
            return UI_COLOR_GOOD;

        case DEVICE_TYPE_HUMIDIFIER:
        case DEVICE_TYPE_DEHUMIDIFIER:
            return UI_COLOR_RAIN;

        case DEVICE_TYPE_HEATER:
        case DEVICE_TYPE_THERMOSTAT:
            return UI_COLOR_TEMP_WARM;

        case DEVICE_TYPE_CONTACT:
        case DEVICE_TYPE_MOTION:
            return UI_COLOR_WARN;

        case DEVICE_TYPE_SENSOR:
            return UI_COLOR_TEXT;

        default:
            return UI_COLOR_ACCENT;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void face_glyph_create(tile_parts_t * parts, lv_obj_t * face, const device_t * device)
{
    parts->icon = ui_label_create(face, "", UI_FONT_ICON_LG, UI_COLOR_DEVICE_OFF);

    bool humidity = device_find_channel(device, DEVICE_ATTR_HUMIDITY) != NULL;
    bool target   = device_find_channel(device, DEVICE_ATTR_TARGET_HUMIDITY) != NULL;
    if(!humidity && !target) return;

    /*(De)humidifiers stack the icon, the room's humidity and the target down
     *the face, spread evenly, with a gap under the name. They give up the
     *detail line for the room, see sh_tile_create().*/
    lv_obj_set_flex_align(face, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(face, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(face, 4, LV_PART_MAIN);

    if(humidity) parts->secondary = ui_label_create(face, "", UI_FONT_SM, UI_COLOR_TEXT);
    parts->target = ui_label_create(face, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);
}

static void face_curtain_create(tile_parts_t * parts, lv_obj_t * face)
{
    lv_obj_t * box = sh_box_create(face);
    lv_obj_set_size(box, CURTAIN_WIDTH, CURTAIN_HEIGHT);

    /*The window, seen between the panels.*/
    lv_obj_t * window = sh_box_create(box);
    lv_obj_set_size(window, CURTAIN_WIDTH - 12, CURTAIN_HEIGHT - CURTAIN_ROD - 6);
    lv_obj_align(window, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(window, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(window, UI_COLOR_DEVICE_OFF, LV_PART_MAIN);
    lv_obj_set_style_radius(window, 3, LV_PART_MAIN);

    lv_obj_t * rod = sh_box_create(box);
    lv_obj_set_size(rod, CURTAIN_WIDTH, CURTAIN_ROD);
    lv_obj_set_style_bg_opa(rod, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rod, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_radius(rod, 2, LV_PART_MAIN);

    for(int i = 0; i < 2; i++) {
        lv_obj_t * panel = sh_box_create(box);
        lv_obj_set_size(panel, CURTAIN_PANEL_MIN, CURTAIN_HEIGHT - CURTAIN_ROD - 2);
        lv_obj_align(panel, i == 0 ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT, 0, CURTAIN_ROD);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(panel, 2, LV_PART_MAIN);
        if(i == 0) parts->curtain_l = panel;
        else       parts->curtain_r = panel;
    }
}

static void face_sensor_create(tile_parts_t * parts, lv_obj_t * face)
{
    parts->primary   = ui_label_create(face, "--", UI_FONT_XL, UI_COLOR_TEXT);
    parts->secondary = ui_label_create(face, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
}

static void face_thermostat_create(tile_parts_t * parts, lv_obj_t * face, uint32_t index)
{
    /*The setpoint over the room temperature, with - and + on a row of their
     *own underneath. Either side of the setpoint they had to shrink to fit,
     *crowded the edges of the tile, and a pressed one was clipped by it.*/
    lv_obj_set_style_pad_row(face, 6, LV_PART_MAIN);

    parts->primary   = ui_label_create(face, "--", UI_FONT_LG, UI_COLOR_TEXT);
    parts->secondary = ui_label_create(face, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);

    lv_obj_t * row = sh_box_create(face);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, 4, LV_PART_MAIN);

    parts->minus = step_button_create(row, LV_SYMBOL_MINUS, minus_clicked, index);
    parts->plus  = step_button_create(row, LV_SYMBOL_PLUS, plus_clicked, index);
}

static lv_obj_t * step_button_create(lv_obj_t * parent, const char * symbol, lv_event_cb_t cb, uint32_t index)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, STEP_BUTTON_SIZE, STEP_BUTTON_SIZE);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_PRESSED);
    /*The theme swells a pressed button past its own box, and the row it sits
     *in is only as big as the buttons, so the swell was clipped. The colour
     *change is feedback enough.*/
    lv_obj_set_style_transform_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    /*A thumb is wider than the button; give the press a little slack, half
     *the gap, so the two buttons' areas meet without overlapping.*/
    lv_obj_set_ext_click_area(btn, UI_GAP / 2);

    lv_obj_t * label = ui_label_create(btn, symbol, UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)index);
    return btn;
}

static void refresh_glyph(tile_parts_t * parts, const device_t * device, char * detail, size_t size)
{
    bool         on    = sh_device_is_on(device);
    const char * glyph = UI_GLYPH_POWER;
    float        value;

    switch(device->type) {
        case DEVICE_TYPE_LIGHT:        glyph = UI_GLYPH_BULB; break;
        case DEVICE_TYPE_LED_STRIP:    glyph = GLYPH_EFFECTS; break;
        case DEVICE_TYPE_SWITCH:       glyph = UI_GLYPH_TOGGLE; break;
        case DEVICE_TYPE_PLUG:         glyph = UI_GLYPH_PLUG; break;
        case DEVICE_TYPE_FAN:          glyph = UI_GLYPH_FAN; break;
        case DEVICE_TYPE_AIR_PURIFIER: glyph = UI_GLYPH_WIND; break;
        case DEVICE_TYPE_HUMIDIFIER:   glyph = UI_GLYPH_WATER; break;
        case DEVICE_TYPE_DEHUMIDIFIER: glyph = UI_GLYPH_DROPLET; break;
        case DEVICE_TYPE_HEATER:       glyph = UI_GLYPH_FIRE; break;
        case DEVICE_TYPE_LOCK:         glyph = on ? UI_GLYPH_LOCK : UI_GLYPH_LOCK_OPEN; break;
        case DEVICE_TYPE_CONTACT:      glyph = on ? UI_GLYPH_DOOR_OPEN : UI_GLYPH_DOOR_CLOSED; break;
        case DEVICE_TYPE_MOTION:       glyph = UI_GLYPH_WALKING; break;
        default: break;
    }

    lv_color_t color = on ? sh_device_on_color(device) : UI_COLOR_DEVICE_OFF;

    /*An unlocked lock is a warning, not merely "off".*/
    if(device->type == DEVICE_TYPE_LOCK && !on && device_value(device, DEVICE_ATTR_LOCK, NULL)) {
        color = UI_COLOR_WARN;
    }

    lv_label_set_text(parts->icon, glyph);
    lv_obj_set_style_text_color(parts->icon, color, LV_PART_MAIN);

    /*Humidity and its target are readings, worth showing on or off. They live
     *in the face, and these tiles have no detail line.*/
    if(parts->target) {
        char text[24];

        if(parts->secondary) {
            if(!format_reading(device, DEVICE_ATTR_HUMIDITY, text, sizeof(text))) lv_strlcpy(text, "--", sizeof(text));
            lv_label_set_text(parts->secondary, text);
        }

        if(device_value(device, DEVICE_ATTR_TARGET_HUMIDITY, &value)) {
            lv_snprintf(text, sizeof(text), "Target %d%%", (int)(value + 0.5f));
        }
        else {
            text[0] = '\0';
        }
        lv_label_set_text(parts->target, text);
        return;
    }

    /*The rest only matter while on: a switched-off light's last brightness
     *is not news.*/
    if(!on) return;

    const char * effect = option_text(device, DEVICE_ATTR_EFFECT);
    const char * option = NULL;

    if(device_value(device, DEVICE_ATTR_BRIGHTNESS, &value)) {
        if(effect) lv_snprintf(detail, size, "%s " UI_BULLET " %d%%", effect, (int)(value + 0.5f));
        else       lv_snprintf(detail, size, "%d%%", (int)(value + 0.5f));
    }
    else if(effect) {
        lv_snprintf(detail, size, "%s", effect);
    }
    else if((option = option_text(device, DEVICE_ATTR_SPEED)) != NULL ||
            (option = option_text(device, DEVICE_ATTR_MODE)) != NULL) {
        lv_snprintf(detail, size, "%s", option);
        if(detail[0] >= 'a' && detail[0] <= 'z') detail[0] = (char)(detail[0] - 'a' + 'A');
    }
    else if(device_value(device, DEVICE_ATTR_SPEED, &value)) {
        lv_snprintf(detail, size, "%d%%", (int)(value + 0.5f));
    }
}

static void refresh_curtain(tile_parts_t * parts, const device_t * device, char * detail, size_t size)
{
    float open  = 0;
    bool  known = curtain_open_percent(device, &open);

    /*Each panel covers half the window when closed and bunches up at its
     *side when open.*/
    int32_t half  = CURTAIN_WIDTH / 2;
    int32_t width = CURTAIN_PANEL_MIN + (int32_t)((half - CURTAIN_PANEL_MIN) * (100.0f - open) / 100.0f);

    lv_color_t color = known ? UI_COLOR_ACCENT : UI_COLOR_DEVICE_OFF;

    lv_obj_set_width(parts->curtain_l, width);
    lv_obj_set_width(parts->curtain_r, width);
    lv_obj_set_style_bg_color(parts->curtain_l, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(parts->curtain_r, color, LV_PART_MAIN);

    if(device_value(device, DEVICE_ATTR_POSITION, &open)) {
        lv_snprintf(detail, size, "%d%%", (int)(open + 0.5f));
    }
}

static void refresh_sensor(tile_parts_t * parts, const device_t * device)
{
    /*The first reading the sensor has goes large, the next one under it.*/
    static const device_attr_t order[] = {DEVICE_ATTR_TEMPERATURE, DEVICE_ATTR_HUMIDITY, DEVICE_ATTR_CO2};

    char     text[32];
    uint32_t shown = 0;

    lv_label_set_text(parts->primary, "--");
    lv_label_set_text(parts->secondary, "");

    for(size_t i = 0; i < sizeof(order) / sizeof(order[0]) && shown < 2; i++) {
        if(!device_find_channel(device, order[i])) continue;

        if(!format_reading(device, order[i], text, sizeof(text))) lv_strlcpy(text, "--", sizeof(text));
        lv_label_set_text(shown == 0 ? parts->primary : parts->secondary, text);
        shown++;
    }
}

static void refresh_thermostat(tile_parts_t * parts, const device_t * device)
{
    const device_channel_t * target = device_find_channel(device, DEVICE_ATTR_TARGET_TEMPERATURE);
    bool                     on     = sh_device_is_on(device);
    char                     text[32];

    if(target && target->known) {
        sh_number_format(text, sizeof(text), target->value, target->step);
        lv_label_set_text_fmt(parts->primary, "%s" UI_DEG, text);
    }
    else {
        lv_label_set_text(parts->primary, "--");
    }
    lv_obj_set_style_text_color(parts->primary, on ? UI_COLOR_TEXT : UI_COLOR_DEVICE_OFF, LV_PART_MAIN);

    bool writable = device_channel_writable(target);
    lv_obj_set_hidden(parts->minus, !writable);
    lv_obj_set_hidden(parts->plus, !writable);

    if(format_reading(device, DEVICE_ATTR_TEMPERATURE, text, sizeof(text))) {
        lv_label_set_text_fmt(parts->secondary, "Now %s", text);
    }
    else {
        lv_label_set_text(parts->secondary, "");
    }
}

/** How open a curtain is, from its position, or failing that its last
 *  open/close command state. */
static bool curtain_open_percent(const device_t * device, float * percent)
{
    if(device_value(device, DEVICE_ATTR_POSITION, percent)) return true;

    const char * state = option_text(device, DEVICE_ATTR_COVER);
    if(!state) return false;

    if(lv_strcmp(state, "OPEN") == 0 || lv_strcmp(state, "open") == 0)        *percent = 100;
    else if(lv_strcmp(state, "CLOSE") == 0 || lv_strcmp(state, "close") == 0) *percent = 0;
    else return false;

    return true;
}

/** @return   the name of the option currently selected on an option channel, or NULL */
static const char * option_text(const device_t * device, device_attr_t attr)
{
    const device_channel_t * channel = device_find_channel(device, attr);

    if(!channel || !channel->known || device_channel_kind(channel) != DEVICE_VALUE_OPTION) return NULL;
    if((uint32_t)channel->value >= channel->option_count) return NULL;

    return channel->options[(uint32_t)channel->value];
}

/** "21.5°", "48%" or "612 ppm". @return false if there is no value yet. */
static bool format_reading(const device_t * device, device_attr_t attr, char * buf, size_t size)
{
    float value;
    if(!device_value(device, attr, &value)) return false;

    char number[16];

    switch(attr) {
        case DEVICE_ATTR_TEMPERATURE:
            sh_number_format(number, sizeof(number), value, 0.1f);
            lv_snprintf(buf, size, "%s" UI_DEG, number);
            break;
        case DEVICE_ATTR_HUMIDITY:
            lv_snprintf(buf, size, "%d%%", (int)(value + 0.5f));
            break;
        case DEVICE_ATTR_CO2:
            lv_snprintf(buf, size, "%d ppm", (int)(value + 0.5f));
            break;
        default:
            sh_number_format(buf, size, value, 1);
            break;
    }

    return true;
}

static void tile_clicked(lv_event_t * e)
{
    uint32_t         index  = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);
    const device_t * device = sh_device(index);
    if(!device) return;

    /*LVGL sends no CLICKED after a drag that scrolled the grid, so a scroll
     *that starts on a tile never toggles it.*/
    switch(sh_device_tap(device)) {
        case SH_TAP_TOGGLE:
            /*A request: the icon only changes once the device reports back.*/
            sh_command(index, DEVICE_ATTR_POWER, sh_device_is_on(device) ? 0.0f : 1.0f);
            break;
        case SH_TAP_PANEL:
            sh_modal_open(index);
            break;
        default:
            break;
    }
}

static void minus_clicked(lv_event_t * e)
{
    setpoint_step((uint32_t)(lv_uintptr_t)lv_event_get_user_data(e), -1);
}

static void plus_clicked(lv_event_t * e)
{
    setpoint_step((uint32_t)(lv_uintptr_t)lv_event_get_user_data(e), 1);
}

static void setpoint_step(uint32_t index, int direction)
{
    const device_t * device = sh_device(index);
    if(!device) return;

    const device_channel_t * target = device_find_channel(device, DEVICE_ATTR_TARGET_TEMPERATURE);
    if(!target) return;

    /*With nothing reported yet, start from the middle of the range.*/
    float from = target->known ? target->value : (target->min + target->max) / 2;
    sh_command(index, DEVICE_ATTR_TARGET_TEMPERATURE, from + (float)direction * target->step);
}
