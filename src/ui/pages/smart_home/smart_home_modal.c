/**
 * @file smart_home_modal.c
 *
 * The controls panel for devices with more than an on/off: it opens over a
 * dimmed page and holds one row per thing the device can set -- brightness,
 * colour temperature, colour, effect, speed, mode, target humidity or
 * temperature, curtain position, lock.
 *
 * Rows carry no captions. Each control says what it is by itself: the colour
 * temperature track is a warm-to-cool gradient, a brightness track fills in
 * yellow, and option buttons are labelled with the options. A light's panel
 * leads with a bulb in the colour the light was last set to, whether that
 * came from a swatch or from the colour temperature.
 *
 * Effects can run to a dozen, so they get a button naming the current one,
 * which opens a wheel picker over the panel: scroll until the one wanted sits
 * in the middle, then Apply, or close it with the x to leave things as they are.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/smart_home/smart_home_private.h"
#include "ui/ui_picker.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

#define PANEL_WIDTH       560
#define ROW_HEIGHT        UI_TOUCH_MIN
#define SWATCH_SIZE       40
#define STEP_BUTTON_SIZE  48
#define OPTION_HEIGHT     44

/** Beyond this many options the buttons wrap onto a grid of this many per row. */
#define OPTION_COLUMNS    4

/** Colour temperature track, cool end then warm end. device_light_color()
 *  maps colour temperature onto the same two colours. */
#define COLOR_TEMP_COOL   lv_color_hex(0xCFE3FF)
#define COLOR_TEMP_WARM   lv_color_hex(0xFFB45A)

/**********************
 *      TYPEDEFS
 **********************/

/** A slider and the reading beside it. */
typedef struct {
    lv_obj_t * slider;
    lv_obj_t * value;
} slider_row_t;

/** A row of buttons, one per option. */
typedef struct {
    lv_obj_t * buttons[DEVICE_OPTION_MAX];
    uint8_t    count;
} option_row_t;

/** A - value + stepper, with an optional current reading beneath. */
typedef struct {
    lv_obj_t * value;
    lv_obj_t * now;
} stepper_row_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * row_create(lv_obj_t * parent);
static lv_obj_t * backdrop_create(lv_opa_t opa, lv_event_cb_t clicked);
static void       slider_row_create(slider_row_t * row, lv_obj_t * parent, device_attr_t attr, lv_color_t color);
static void       option_row_create(option_row_t * row, lv_obj_t * parent, const device_channel_t * channel);
static void       stepper_row_create(stepper_row_t * row, lv_obj_t * parent, device_attr_t attr);
static void       swatches_create(lv_obj_t * parent);
static void       effect_row_create(lv_obj_t * parent);
static void       lock_row_create(lv_obj_t * parent);
static lv_obj_t * text_button_create(lv_obj_t * parent, const char * text);
static lv_obj_t * round_button_create(lv_obj_t * parent, const char * symbol, int32_t size);
static void       option_label(char * out, size_t size, const char * option);

static void slider_row_refresh(slider_row_t * row, const device_t * device, device_attr_t attr);
static void option_row_refresh(option_row_t * row, const device_t * device, device_attr_t attr);
static void stepper_row_refresh(stepper_row_t * row, const device_t * device, device_attr_t attr,
                                device_attr_t now_attr);

static void picker_open(void);
static void picker_close(void);

static void backdrop_clicked(lv_event_t * e);
static void close_clicked(lv_event_t * e);
static void power_changed(lv_event_t * e);
static void slider_moved(lv_event_t * e);
static void slider_released(lv_event_t * e);
static void option_clicked(lv_event_t * e);
static void swatch_clicked(lv_event_t * e);
static void effect_clicked(lv_event_t * e);
static void step_clicked(lv_event_t * e);
static void lock_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Preset colours, warm white first. */
static const uint32_t swatch_colors[] = {
    0xFFD7A0, 0xFF3B30, 0xFF9500, 0xFFD60A, 0x34C759, 0x32D2F5, 0x0A84FF, 0xBF5AF2,
};
#define SWATCH_COUNT (sizeof(swatch_colors) / sizeof(swatch_colors[0]))

static struct {
    lv_obj_t *    backdrop;   /**< NULL when closed */
    uint32_t      index;
    lv_obj_t *    bulb;       /**< Lights only */
    lv_obj_t *    power;
    slider_row_t  brightness;
    slider_row_t  color_temp;
    slider_row_t  speed;
    slider_row_t  position;
    lv_obj_t *    effect_label;
    option_row_t  speed_options;
    option_row_t  mode;
    stepper_row_t target_humidity;
    stepper_row_t target_temperature;
    lv_obj_t *    lock_button;
    lv_obj_t *    lock_icon;
    lv_obj_t *    lock_text;
} modal;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sh_modal_open(uint32_t index)
{
    const device_t * device = sh_device(index);
    if(!device) return;

    sh_modal_close();
    lv_memzero(&modal, sizeof(modal));
    modal.index = index;

    /*Dims the page and catches a tap outside the panel, which closes it.*/
    modal.backdrop = backdrop_create(LV_OPA_60, backdrop_clicked);

    lv_obj_t * panel = ui_card_create(modal.backdrop);
    lv_obj_set_size(panel, PANEL_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, lv_display_get_vertical_resolution(NULL) - 2 * UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, UI_PAD + 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(panel, true);
    lv_obj_center(panel);

    /*[bulb] Name ........ [power] [x]*/
    lv_obj_t * head = row_create(panel);

    if(device->type == DEVICE_TYPE_LIGHT || device->type == DEVICE_TYPE_LED_STRIP) {
        modal.bulb = ui_label_create(head, UI_SYMBOL_DEVICES, UI_FONT_ICON, UI_COLOR_DEVICE_OFF);
    }

    lv_obj_t * name = ui_label_create(head, device->name, UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(name, 1);
    ui_label_single_line(name, UI_FONT_MD);

    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_POWER))) {
        modal.power = lv_switch_create(head);
        lv_obj_set_size(modal.power, 64, 34);
        lv_obj_set_style_bg_color(modal.power, UI_COLOR_BORDER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(modal.power, UI_COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(modal.power, power_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }

    lv_obj_t * close = round_button_create(head, LV_SYMBOL_CLOSE, 40);
    lv_obj_set_style_bg_color(close, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);

    /*One row per writable channel, in a fixed order that reads from the most
     *used control down.*/
    const device_channel_t * channel;

    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_BRIGHTNESS))) {
        slider_row_create(&modal.brightness, panel, DEVICE_ATTR_BRIGHTNESS, UI_COLOR_SUN);
    }
    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_COLOR_TEMP))) {
        slider_row_create(&modal.color_temp, panel, DEVICE_ATTR_COLOR_TEMP, UI_COLOR_SUN);
    }
    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_COLOR))) {
        swatches_create(panel);
    }
    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_EFFECT))) {
        effect_row_create(panel);
    }

    channel = device_find_channel(device, DEVICE_ATTR_SPEED);
    if(device_channel_writable(channel)) {
        if(device_channel_kind(channel) == DEVICE_VALUE_OPTION) option_row_create(&modal.speed_options, panel, channel);
        else slider_row_create(&modal.speed, panel, DEVICE_ATTR_SPEED, UI_COLOR_ACCENT);
    }

    channel = device_find_channel(device, DEVICE_ATTR_MODE);
    if(device_channel_writable(channel)) option_row_create(&modal.mode, panel, channel);

    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_TARGET_TEMPERATURE))) {
        stepper_row_create(&modal.target_temperature, panel, DEVICE_ATTR_TARGET_TEMPERATURE);
    }
    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_TARGET_HUMIDITY))) {
        stepper_row_create(&modal.target_humidity, panel, DEVICE_ATTR_TARGET_HUMIDITY);
    }

    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_POSITION))) {
        slider_row_create(&modal.position, panel, DEVICE_ATTR_POSITION, UI_COLOR_ACCENT);
    }

    /*Open, Close and Stop are actions rather than a setting, so unlike the
     *other option rows this one is never refreshed to show a selection. The
     *position slider above says where the curtain actually is.*/
    option_row_t cover = {0};
    channel = device_find_channel(device, DEVICE_ATTR_COVER);
    if(device_channel_writable(channel)) option_row_create(&cover, panel, channel);

    if(device_channel_writable(device_find_channel(device, DEVICE_ATTR_LOCK))) lock_row_create(panel);

    sh_modal_refresh(index);
}

void sh_modal_close(void)
{
    picker_close();

    if(!modal.backdrop) return;

    /*Usually called from an event on one of the panel's own children, which
     *must not be deleted from under itself.*/
    lv_obj_delete_async(modal.backdrop);
    modal.backdrop = NULL;
}

void sh_modal_refresh(uint32_t index)
{
    if(!modal.backdrop || index != modal.index) return;

    const device_t * device = sh_device(index);
    if(!device) return;

    bool on = sh_device_is_on(device);

    if(modal.bulb) {
        lv_obj_set_style_text_color(modal.bulb, on ? sh_device_on_color(device) : UI_COLOR_DEVICE_OFF, LV_PART_MAIN);
    }

    if(modal.power) {
        if(on) lv_obj_add_state(modal.power, LV_STATE_CHECKED);
        else   lv_obj_remove_state(modal.power, LV_STATE_CHECKED);
    }

    slider_row_refresh(&modal.brightness, device, DEVICE_ATTR_BRIGHTNESS);
    slider_row_refresh(&modal.color_temp, device, DEVICE_ATTR_COLOR_TEMP);
    slider_row_refresh(&modal.speed, device, DEVICE_ATTR_SPEED);
    slider_row_refresh(&modal.position, device, DEVICE_ATTR_POSITION);

    option_row_refresh(&modal.speed_options, device, DEVICE_ATTR_SPEED);
    option_row_refresh(&modal.mode, device, DEVICE_ATTR_MODE);

    stepper_row_refresh(&modal.target_temperature, device, DEVICE_ATTR_TARGET_TEMPERATURE, DEVICE_ATTR_TEMPERATURE);
    stepper_row_refresh(&modal.target_humidity, device, DEVICE_ATTR_TARGET_HUMIDITY, DEVICE_ATTR_HUMIDITY);

    if(modal.effect_label) {
        const device_channel_t * effect = device_find_channel(device, DEVICE_ATTR_EFFECT);
        char                     text[DEVICE_OPTION_LEN] = "--";
        if(effect && effect->known && (uint32_t)effect->value < effect->option_count) {
            option_label(text, sizeof(text), effect->options[(uint32_t)effect->value]);
        }
        lv_label_set_text(modal.effect_label, text);
    }

    if(modal.lock_button) {
        float locked;
        bool  is_locked = device_value(device, DEVICE_ATTR_LOCK, &locked) && locked > 0.5f;

        /*The button names what it will do, not the current state.*/
        lv_label_set_text(modal.lock_icon, is_locked ? UI_GLYPH_LOCK_OPEN : UI_GLYPH_LOCK);
        lv_label_set_text(modal.lock_text, is_locked ? "Unlock" : "Lock");
        lv_obj_set_style_bg_color(modal.lock_button, is_locked ? UI_COLOR_WARN : UI_COLOR_GOOD, LV_PART_MAIN);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * row_create(lv_obj_t * parent)
{
    lv_obj_t * row = sh_box_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_min_height(row, ROW_HEIGHT, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

/** A full-screen dimming layer on the top layer that reports taps on itself. */
static lv_obj_t * backdrop_create(lv_opa_t opa, lv_event_cb_t clicked)
{
    lv_obj_t * backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop, opa, LV_PART_MAIN);
    lv_obj_add_event_cb(backdrop, clicked, LV_EVENT_CLICKED, NULL);
    return backdrop;
}

static void slider_row_create(slider_row_t * row, lv_obj_t * parent, device_attr_t attr, lv_color_t color)
{
    const device_channel_t * channel = device_find_channel(sh_device(modal.index), attr);
    lv_obj_t *               box     = row_create(parent);

    /*The track runs to the edge of the row, so the row has to leave room for
     *the knob where it overhangs the ends -- at the row's edge, and before
     *the reading, which the knob would otherwise cover at 100.*/
    lv_obj_set_style_pad_hor(box, UI_SLIDER_KNOB_OVERHANG, LV_PART_MAIN);
    lv_obj_set_style_pad_column(box, UI_SLIDER_KNOB_OVERHANG, LV_PART_MAIN);

    row->slider = ui_slider_create(box, color);
    lv_obj_set_flex_grow(row->slider, 1);

    if(attr == DEVICE_ATTR_COLOR_TEMP) {
        /*Raw mireds, low to high: the track itself shows cool to warm, and
         *there is no fill, since neither end is "more".*/
        lv_slider_set_range(row->slider, (int32_t)channel->min, (int32_t)channel->max);
        lv_obj_set_style_bg_color(row->slider, COLOR_TEMP_COOL, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(row->slider, COLOR_TEMP_WARM, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(row->slider, LV_GRAD_DIR_HOR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    }
    else {
        row->value = ui_label_create(box, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
        lv_obj_set_width(row->value, 48);
        lv_obj_set_style_text_align(row->value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }

    lv_obj_add_event_cb(row->slider, slider_moved, LV_EVENT_VALUE_CHANGED, row);
    lv_obj_add_event_cb(row->slider, slider_released, LV_EVENT_RELEASED, (void *)(lv_uintptr_t)attr);
}

static void option_row_create(option_row_t * row, lv_obj_t * parent, const device_channel_t * channel)
{
    lv_obj_t * box  = row_create(parent);
    bool       grid = channel->option_count > OPTION_COLUMNS;

    lv_obj_set_style_pad_column(box, UI_GAP / 2, LV_PART_MAIN);

    /*A long list of modes wraps into rows of equal buttons rather than
     *squeezing into one.*/
    if(grid) {
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(box, UI_GAP / 2, LV_PART_MAIN);
    }

    row->count = channel->option_count;

    for(uint8_t i = 0; i < channel->option_count; i++) {
        char text[DEVICE_OPTION_LEN];
        option_label(text, sizeof(text), channel->options[i]);

        lv_obj_t * btn = text_button_create(box, text);
        if(grid) lv_obj_set_width(btn, LV_PCT(24));
        else     lv_obj_set_flex_grow(btn, 1);

        /*Index and attribute packed into the user data.*/
        lv_uintptr_t data = ((lv_uintptr_t)channel->attr << 8) | i;
        lv_obj_add_event_cb(btn, option_clicked, LV_EVENT_CLICKED, (void *)data);
        row->buttons[i] = btn;
    }
}

static void stepper_row_create(stepper_row_t * row, lv_obj_t * parent, device_attr_t attr)
{
    lv_obj_t * box = row_create(parent);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, UI_GAP * 2, LV_PART_MAIN);

    lv_obj_t * minus = round_button_create(box, LV_SYMBOL_MINUS, STEP_BUTTON_SIZE);
    lv_obj_add_event_cb(minus, step_clicked, LV_EVENT_CLICKED, (void *)(((lv_uintptr_t)attr << 1) | 0));

    lv_obj_t * middle = sh_box_create(box);
    lv_obj_set_width(middle, 140);
    lv_obj_set_flex_flow(middle, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    row->value = ui_label_create(middle, "--", UI_FONT_XL, UI_COLOR_TEXT);
    row->now   = ui_label_create(middle, "", UI_FONT_XS, UI_COLOR_TEXT_DIM);

    lv_obj_t * plus = round_button_create(box, LV_SYMBOL_PLUS, STEP_BUTTON_SIZE);
    lv_obj_add_event_cb(plus, step_clicked, LV_EVENT_CLICKED, (void *)(((lv_uintptr_t)attr << 1) | 1));
}

static void swatches_create(lv_obj_t * parent)
{
    lv_obj_t * box = row_create(parent);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /*Plain buttons, not a selection: tapping one sets the colour, and the
     *bulb in the header shows what the light is actually showing.*/
    for(uint32_t i = 0; i < SWATCH_COUNT; i++) {
        lv_obj_t * swatch = lv_obj_create(box);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_size(swatch, SWATCH_SIZE, SWATCH_SIZE);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(swatch_colors[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_60, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(swatch, 6);
        lv_obj_set_clickable(swatch, true);
        lv_obj_add_event_cb(swatch, swatch_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
    }
}

/** A full-width button naming the current effect, which opens the picker. */
static void effect_row_create(lv_obj_t * parent)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), OPTION_HEIGHT + 4);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, UI_PAD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(btn, effect_clicked, LV_EVENT_CLICKED, NULL);

    modal.effect_label = ui_label_create(btn, "--", UI_FONT_SM, UI_COLOR_TEXT);
    lv_obj_set_flex_grow(modal.effect_label, 1);
    ui_label_single_line(modal.effect_label, UI_FONT_SM);

    ui_label_create(btn, LV_SYMBOL_DOWN, UI_FONT_XS, UI_COLOR_TEXT_DIM);
}

static void lock_row_create(lv_obj_t * parent)
{
    lv_obj_t * box = row_create(parent);

    modal.lock_button = lv_button_create(box);
    lv_obj_set_size(modal.lock_button, LV_PCT(100), ROW_HEIGHT + 8);
    lv_obj_set_style_radius(modal.lock_button, UI_RADIUS - 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(modal.lock_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(modal.lock_button, lock_clicked, LV_EVENT_CLICKED, NULL);

    /*Glyph and word as two labels: the glyph comes from the icon font, which
     *has the padlocks at this size, and the word reads better in the body
     *font than in the icon font's fallback.*/
    lv_obj_set_flex_flow(modal.lock_button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modal.lock_button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modal.lock_button, UI_GAP, LV_PART_MAIN);

    modal.lock_icon = ui_label_create(modal.lock_button, UI_GLYPH_LOCK, UI_FONT_ICON, lv_color_black());
    modal.lock_text = ui_label_create(modal.lock_button, "", UI_FONT_MD, lv_color_black());
}

static lv_obj_t * text_button_create(lv_obj_t * parent, const char * text)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_height(btn, OPTION_HEIGHT);
    lv_obj_set_style_pad_hor(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BORDER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(label, LV_PCT(100));
    ui_label_single_line(label, UI_FONT_SM);
    lv_obj_center(label);

    return btn;
}

static lv_obj_t * round_button_create(lv_obj_t * parent, const char * symbol, int32_t size)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t * label = ui_label_create(btn, symbol, UI_FONT_MD, UI_COLOR_TEXT);
    lv_obj_center(label);

    return btn;
}

/** Device spellings made readable: "auto" and "OPEN" become "Auto" and
 *  "Open"; names already in mixed case, like "Fire 2012", are left alone. */
static void option_label(char * out, size_t size, const char * option)
{
    lv_strlcpy(out, option, size);

    bool has_lower = false, has_upper = false;
    for(const char * c = out; *c; c++) {
        has_lower |= (*c >= 'a' && *c <= 'z');
        has_upper |= (*c >= 'A' && *c <= 'Z');
    }
    if(has_lower && has_upper) return;

    for(char * c = out; *c; c++) {
        if(c == out && *c >= 'a' && *c <= 'z')      *c = (char)(*c - 'a' + 'A');
        else if(c != out && *c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    }
}

static void slider_row_refresh(slider_row_t * row, const device_t * device, device_attr_t attr)
{
    float value;

    if(!row->slider || !device_value(device, attr, &value)) return;

    /*Never yank the knob out from under a finger. Once it is let go, the next
     *report puts it wherever the device really is.*/
    if(lv_slider_is_dragged(row->slider)) return;

    lv_slider_set_value(row->slider, (int32_t)(value + 0.5f), LV_ANIM_OFF);
    if(row->value) lv_label_set_text_fmt(row->value, "%d%%", (int)(value + 0.5f));
}

static void option_row_refresh(option_row_t * row, const device_t * device, device_attr_t attr)
{
    float value;
    bool  known = device_value(device, attr, &value);

    for(uint8_t i = 0; i < row->count; i++) {
        if(known && (uint32_t)value == i) lv_obj_add_state(row->buttons[i], LV_STATE_CHECKED);
        else                              lv_obj_remove_state(row->buttons[i], LV_STATE_CHECKED);
    }
}

static void stepper_row_refresh(stepper_row_t * row, const device_t * device, device_attr_t attr,
                                device_attr_t now_attr)
{
    if(!row->value) return;

    const device_channel_t * channel = device_find_channel(device, attr);
    const char *             unit    = attr == DEVICE_ATTR_TARGET_HUMIDITY ? "%" : UI_DEG;
    char                     text[16];

    if(channel && channel->known) {
        sh_number_format(text, sizeof(text), channel->value, channel->step);
        lv_label_set_text_fmt(row->value, "%s%s", text, unit);
    }

    float now;
    if(device_value(device, now_attr, &now)) {
        sh_number_format(text, sizeof(text), now, attr == DEVICE_ATTR_TARGET_HUMIDITY ? 1 : 0.1f);
        lv_label_set_text_fmt(row->now, "Now %s%s", text, unit);
    }
}

/** Apply on the effect picker. The device index travels as the user data. */
static void effect_applied(const uint32_t selected[], uint32_t count, void * user)
{
    LV_UNUSED(count);
    sh_command((uint32_t)(lv_uintptr_t)user, DEVICE_ATTR_EFFECT, (float)selected[0]);
}

/**
 * The effect picker: the shared wheel picker, opened on the effect running
 * now. Nothing is sent until Apply, so scrolling past effects does not flash
 * them on the light.
 */
static void picker_open(void)
{
    const device_t *         device  = sh_device(modal.index);
    const device_channel_t * channel = device ? device_find_channel(device, DEVICE_ATTR_EFFECT) : NULL;
    if(!channel || channel->option_count == 0) return;

    /*The wheel wants its options as one newline-separated string, sized for
     *every option at full length plus its newline, so it cannot overflow.*/
    char   options[DEVICE_OPTION_MAX * (DEVICE_OPTION_LEN + 1)] = "";
    size_t used = 0;
    for(uint8_t i = 0; i < channel->option_count; i++) {
        char text[DEVICE_OPTION_LEN];
        option_label(text, sizeof(text), channel->options[i]);
        used += lv_snprintf(options + used, sizeof(options) - used, "%s%s", i > 0 ? "\n" : "", text);
    }

    bool               current = channel->known && (uint32_t)channel->value < channel->option_count;
    ui_picker_column_t column  = {options, current ? (uint32_t)channel->value : 0, false, 0};

    ui_picker_open(&column, 1, effect_applied, (void *)(lv_uintptr_t)modal.index);
}

static void picker_close(void)
{
    ui_picker_close();
}

static void backdrop_clicked(lv_event_t * e)
{
    /*Only a tap on the backdrop itself, not one that started on the panel.*/
    if(lv_event_get_target(e) == modal.backdrop) sh_modal_close();
}

static void close_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    sh_modal_close();
}

static void power_changed(lv_event_t * e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    sh_command(modal.index, DEVICE_ATTR_POWER, on ? 1.0f : 0.0f);
}

static void slider_moved(lv_event_t * e)
{
    slider_row_t * row = lv_event_get_user_data(e);
    if(row->value) lv_label_set_text_fmt(row->value, "%d%%", (int)lv_slider_get_value(row->slider));
}

static void slider_released(lv_event_t * e)
{
    /*Sent on release rather than while dragging, so a slide is one command
     *instead of a burst of them.*/
    device_attr_t attr = (device_attr_t)(lv_uintptr_t)lv_event_get_user_data(e);
    sh_command(modal.index, attr, (float)lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void option_clicked(lv_event_t * e)
{
    lv_uintptr_t data = (lv_uintptr_t)lv_event_get_user_data(e);
    sh_command(modal.index, (device_attr_t)(data >> 8), (float)(data & 0xFF));
}

static void swatch_clicked(lv_event_t * e)
{
    uint32_t i = (uint32_t)(lv_uintptr_t)lv_event_get_user_data(e);
    sh_command(modal.index, DEVICE_ATTR_COLOR, (float)swatch_colors[i]);
}

static void effect_clicked(lv_event_t * e)
{
    LV_UNUSED(e);
    picker_open();
}

static void step_clicked(lv_event_t * e)
{
    lv_uintptr_t  data = (lv_uintptr_t)lv_event_get_user_data(e);
    device_attr_t attr = (device_attr_t)(data >> 1);
    float         sign = (data & 1) ? 1.0f : -1.0f;

    const device_t *         device  = sh_device(modal.index);
    const device_channel_t * channel = device ? device_find_channel(device, attr) : NULL;
    if(!channel) return;

    float from = channel->known ? channel->value : (channel->min + channel->max) / 2;
    sh_command(modal.index, attr, from + sign * channel->step);
}

static void lock_clicked(lv_event_t * e)
{
    LV_UNUSED(e);

    const device_t * device = sh_device(modal.index);
    float            locked;
    bool             is_locked = device && device_value(device, DEVICE_ATTR_LOCK, &locked) && locked > 0.5f;

    sh_command(modal.index, DEVICE_ATTR_LOCK, is_locked ? 0.0f : 1.0f);
}