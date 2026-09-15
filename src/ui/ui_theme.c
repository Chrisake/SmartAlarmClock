/**
 * @file ui_theme.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_theme.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    uint32_t bg, rail, card, card_alt, border, track, text, text_dim, device_off, cloud, cloud_dark, snow, moon;
} palette_hex_t;

/**********************
 *  STATIC VARIABLES
 **********************/

/*The dark theme is the one the UI was designed in. The light one keeps the
 *same steps between surfaces, inverted, and darkens the weather icons' greys
 *and whites so they still stand off a white card.*/
static const palette_hex_t hex_dark = {
    .bg = 0x0E1116, .rail = 0x11161D, .card = 0x171C23, .card_alt = 0x1F262F, .border = 0x2B333D,
    .track = 0x4A5563, .text = 0xE8EDF3, .text_dim = 0x94A3B4, .device_off = 0x5A6573,
    .cloud = 0xB8C2CE, .cloud_dark = 0x7C8898, .snow = 0xF2F6FA, .moon = 0xDCE3EC,
};

static const palette_hex_t hex_light = {
    .bg = 0xE9EDF2, .rail = 0xDDE3EA, .card = 0xFFFFFF, .card_alt = 0xF0F3F7, .border = 0xD3DAE2,
    .track = 0xC3CBD5, .text = 0x18202A, .text_dim = 0x5E6A78, .device_off = 0xA3ADBA,
    .cloud = 0x9AA6B4, .cloud_dark = 0x6B7787, .snow = 0x8FB8E0, .moon = 0x8C98A8,
};

/*In SETTINGS_ACCENT_COUNT order: settings store the index. Picked to read on
 *both the dark and the light surfaces.*/
static const struct {
    const char * name;
    uint32_t     hex;
} accents[UI_ACCENT_COUNT] = {
    {"Blue",   0x4C9AFF},
    {"Teal",   0x14B8A6},
    {"Green",  0x30B350},
    {"Amber",  0xF0A020},
    {"Orange", 0xFF7A3D},
    {"Red",    0xF0453A},
    {"Pink",   0xEC5FA0},
    {"Purple", 0xA35BE0},
};

/**********************
 *  GLOBAL VARIABLES
 **********************/

ui_palette_t ui_palette;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_theme_apply(lv_obj_t * screen)
{
    lv_obj_set_style_bg_color(screen, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(screen, UI_FONT_MD, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(screen, false);
}

lv_obj_t * ui_card_create(lv_obj_t * parent)
{
    lv_obj_t * card = lv_obj_create(parent);

    lv_obj_set_style_bg_color(card, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, UI_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, UI_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(card, UI_GAP, LV_PART_MAIN);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(card, false);

    return card;
}

lv_obj_t * ui_tile_create(lv_obj_t * parent)
{
    lv_obj_t * tile = ui_card_create(parent);

    lv_obj_set_style_bg_color(tile, UI_COLOR_CARD_ALT, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, UI_RADIUS - 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, UI_GAP, LV_PART_MAIN);

    return tile;
}

lv_obj_t * ui_card_title(lv_obj_t * card, const char * text)
{
    lv_obj_t * label = ui_label_create(card, text, UI_FONT_XS, UI_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    return label;
}

lv_obj_t * ui_label_create(lv_obj_t * parent, const char * text, const lv_font_t * font, lv_color_t color)
{
    lv_obj_t * label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);

    return label;
}

void ui_label_single_line(lv_obj_t * label, const lv_font_t * font)
{
    lv_obj_set_height(label, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

lv_obj_t * ui_slider_create(lv_obj_t * parent, lv_color_t indicator)
{
    lv_obj_t * slider = lv_slider_create(parent);

    lv_obj_set_height(slider, UI_SLIDER_HEIGHT);
    lv_slider_set_range(slider, 0, 100);
    /*The theme draws the track translucent; the token is meant as drawn.*/
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, UI_SLIDER_KNOB_PAD, LV_PART_KNOB);
    /*The default theme swells a pressed knob by a DPI-scaled amount. Pin it,
     *so the room reserved below is exactly what the knob needs.*/
    lv_obj_set_style_transform_width(slider, UI_SLIDER_KNOB_GROW, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(slider, UI_SLIDER_KNOB_GROW, LV_PART_KNOB | LV_STATE_PRESSED);
    /*A fingertip lands well off a 14 px track.*/
    lv_obj_set_ext_click_area(slider, UI_SLIDER_KNOB_PAD + 6);

    /*Reserve the knob's overhang above and below the track in the layout, so
     *whatever row the slider sits in grows tall enough to hold the whole knob.
     *Overflow-visible is not enough: LVGL still clips such a child to its
     *parent's drawing area, which for a plain row is its box.
     *
     *Vertical only. Flex does not take a growing child's side margins out of
     *the width it hands out, so side margins push the slider's neighbours out
     *of the row. The ends are the parent's job, see the header.*/
    lv_obj_set_style_margin_ver(slider, UI_SLIDER_KNOB_ROOM, LV_PART_MAIN);

    return slider;
}

lv_color_t ui_aqi_color(int32_t aqi)
{
    if(aqi <= 50)  return UI_COLOR_GOOD;
    if(aqi <= 100) return UI_COLOR_WARN;
    if(aqi <= 150) return lv_color_hex(0xFF8C42);
    if(aqi <= 200) return UI_COLOR_BAD;
    if(aqi <= 300) return UI_COLOR_SEVERE;
    return lv_color_hex(0x8B1A1A);
}

const char * ui_aqi_band(int32_t aqi)
{
    if(aqi <= 50)  return "Good";
    if(aqi <= 100) return "Moderate";
    if(aqi <= 150) return "Unhealthy for sensitive groups";
    if(aqi <= 200) return "Unhealthy";
    if(aqi <= 300) return "Very unhealthy";
    return "Hazardous";
}

void ui_theme_set(ui_theme_mode_t mode, uint32_t accent)
{
    const palette_hex_t * hex = (mode == UI_THEME_LIGHT) ? &hex_light : &hex_dark;

    ui_palette.bg         = lv_color_hex(hex->bg);
    ui_palette.rail       = lv_color_hex(hex->rail);
    ui_palette.card       = lv_color_hex(hex->card);
    ui_palette.card_alt   = lv_color_hex(hex->card_alt);
    ui_palette.border     = lv_color_hex(hex->border);
    ui_palette.track      = lv_color_hex(hex->track);
    ui_palette.text       = lv_color_hex(hex->text);
    ui_palette.text_dim   = lv_color_hex(hex->text_dim);
    ui_palette.device_off = lv_color_hex(hex->device_off);
    ui_palette.cloud      = lv_color_hex(hex->cloud);
    ui_palette.cloud_dark = lv_color_hex(hex->cloud_dark);
    ui_palette.snow       = lv_color_hex(hex->snow);
    ui_palette.moon       = lv_color_hex(hex->moon);
    ui_palette.accent     = ui_accent_color(accent);

    /*LVGL's own theme still dresses whatever no page restyles -- keyboard
     *keys, dropdown lists, a text area's cursor -- so it follows along.*/
    lv_display_t * disp = lv_display_get_default();
    if(disp) {
        lv_theme_t * theme = lv_theme_default_init(disp, ui_palette.accent, lv_palette_main(LV_PALETTE_RED),
                                                   mode == UI_THEME_DARK, LV_FONT_DEFAULT);
        lv_display_set_theme(disp, theme);
    }
}

lv_color_t ui_accent_color(uint32_t index)
{
    return lv_color_hex(accents[index < UI_ACCENT_COUNT ? index : 0].hex);
}

const char * ui_accent_name(uint32_t index)
{
    return accents[index < UI_ACCENT_COUNT ? index : 0].name;
}
