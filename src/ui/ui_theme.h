/**
 * @file ui_theme.h
 *
 * Design tokens and small widget factories shared by every page.
 *
 * Pages should build from these rather than hard-coding colours, fonts or
 * spacing, so the whole UI can be re-skinned from one place.
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/*********************
 *      DEFINES
 *********************/

/* --- Colours -----------------------------------------------------------
 * Dark by default: this thing sits on a bedside table and has to be
 * readable at 3am without lighting up the room. A light theme and a choice
 * of accent colours come from the settings page.
 *
 * The surface, text and accent tokens therefore read from `ui_palette`,
 * which ui_theme_set() fills in. Widgets take their colours when they are
 * built, so a change of theme only shows once the UI is rebuilt -- see
 * ui_rebuild(). The status and identity colours further down are the same
 * in both themes. */

/** Number of accent colours on offer; ui_accent_color() gives them. */
#define UI_ACCENT_COUNT 8

typedef enum {
    UI_THEME_DARK,
    UI_THEME_LIGHT,
} ui_theme_mode_t;

/** The colours that change with the theme. */
typedef struct {
    lv_color_t bg;
    lv_color_t rail;
    lv_color_t card;
    lv_color_t card_alt;
    lv_color_t border;
    lv_color_t track;
    lv_color_t text;
    lv_color_t text_dim;
    lv_color_t accent;
    lv_color_t device_off;
    lv_color_t cloud;
    lv_color_t cloud_dark;
    lv_color_t snow;
    lv_color_t moon;
} ui_palette_t;

extern ui_palette_t ui_palette;

#define UI_COLOR_BG          (ui_palette.bg)        /**< Screen background */
#define UI_COLOR_RAIL        (ui_palette.rail)      /**< Navigation rail */
#define UI_COLOR_CARD        (ui_palette.card)      /**< Card surface */
#define UI_COLOR_CARD_ALT    (ui_palette.card_alt)  /**< Nested surface, tiles */
#define UI_COLOR_BORDER      (ui_palette.border)
#define UI_COLOR_TRACK       (ui_palette.track)     /**< Unfilled slider/arc track */
#define UI_COLOR_TEXT        (ui_palette.text)      /**< Primary text */
#define UI_COLOR_TEXT_DIM    (ui_palette.text_dim)  /**< Labels, captions, units */
#define UI_COLOR_ACCENT      (ui_palette.accent)
#define UI_COLOR_GOOD        lv_color_hex(0x35C759)
#define UI_COLOR_WARN        lv_color_hex(0xFFB020)
#define UI_COLOR_BAD         lv_color_hex(0xFF453A)
#define UI_COLOR_SEVERE      lv_color_hex(0xAF52DE)

/* --- Calendar colours ---------------------------------------------------
 * A calendar entry carries no label: its colour is the only thing that says
 * which calendar it came from. Keep these four clearly distinguishable from
 * each other, and bright enough to read as a 4 px bar on the dark surface. */
#define UI_COLOR_CAL_HOLIDAY   lv_color_hex(0x35C759)  /**< National holidays */
#define UI_COLOR_CAL_OCCASION  lv_color_hex(0xFFB020)  /**< Birthdays, anniversaries */
#define UI_COLOR_CAL_PERSONAL  lv_color_hex(0x4C9AFF)  /**< Personal */
#define UI_COLOR_CAL_WORK      lv_color_hex(0xAF52DE)  /**< Work meetings */

/* --- Precipitation ------------------------------------------------------
 * The MinuteCast chart is drawn in one colour. Height already carries the
 * intensity, and the Light/Heavy scale labels it, so colour-coding on top of
 * that was only noise. */
#define UI_COLOR_RAIN lv_color_hex(0x4C9AFF)

/* --- Weather ------------------------------------------------------------
 * The drawn condition icons (ui_weather_icon.c), and the two ends of the
 * weekly forecast's temperature range bars. Rain drops use UI_COLOR_RAIN. */
#define UI_COLOR_SUN         lv_color_hex(0xFFC53D)
#define UI_COLOR_MOON        (ui_palette.moon)        /**< Follows the theme, to show on its cards */
#define UI_COLOR_CLOUD       (ui_palette.cloud)
#define UI_COLOR_CLOUD_DARK  (ui_palette.cloud_dark)  /**< Rain, storm, the back of an overcast sky */
#define UI_COLOR_SNOW        (ui_palette.snow)
#define UI_COLOR_TEMP_COOL   lv_color_hex(0x4C9AFF)
#define UI_COLOR_TEMP_WARM   lv_color_hex(0xFF8C42)

/* --- Air quality traces -------------------------------------------------
 * One colour per metric on the air page's history chart; the metric's tile
 * wears the same colour as its legend. The four particle sizes are one blue
 * ramp, light to dark from PM1.0 to PM10, so they read as a family. The rest
 * take categorical hues in a fixed order that clears colour-blind separation
 * between neighbours on the card surface. Up to ten traces can be on screen
 * at once, which no palette keeps apart by hue alone -- the tile outline is
 * what tells them apart, so keep that pairing if these change.
 *
 * These are identity colours, not status: the tile values keep the
 * good/warn/bad colours above. */
#define UI_COLOR_AQ_PM1       lv_color_hex(0x9EC5F4)
#define UI_COLOR_AQ_PM25      lv_color_hex(0x5598E7)
#define UI_COLOR_AQ_PM4       lv_color_hex(0x2A78D6)
#define UI_COLOR_AQ_PM10      lv_color_hex(0x1C5CAB)
#define UI_COLOR_AQ_CO2       lv_color_hex(0xD95926)
#define UI_COLOR_AQ_VOC       lv_color_hex(0x199E70)
#define UI_COLOR_AQ_NOX       lv_color_hex(0xC98500)
#define UI_COLOR_AQ_HCHO      lv_color_hex(0xD55181)
#define UI_COLOR_AQ_TEMP      lv_color_hex(0x008300)
#define UI_COLOR_AQ_HUMIDITY  lv_color_hex(0x9085E9)

/* --- Type scale --------------------------------------------------------
 * Only sizes enabled in lv_conf.h may be used here.
 *
 * UI_FONT_CLOCK is the largest built-in Montserrat (48 px). On a 7" panel
 * viewed from across a room a bigger face is better; to go larger, generate
 * one with the LVGL font converter, declare it via LV_FONT_CUSTOM_DECLARE in
 * lv_conf.h, and point UI_FONT_CLOCK at it -- nothing else has to change. */
#define UI_FONT_CLOCK        (&lv_font_montserrat_48)
#define UI_FONT_XL           (&lv_font_montserrat_34)
#define UI_FONT_LG           (&lv_font_montserrat_28)
#define UI_FONT_MD           (&lv_font_montserrat_20)
#define UI_FONT_SM           (&lv_font_montserrat_16)
#define UI_FONT_XS           (&lv_font_montserrat_14)

/* --- Glyphs ------------------------------------------------------------
 * The built-in Montserrat fonts are generated over 0x20-0x7F plus 0xB0 and
 * 0x2022 only, so these two are the only non-ASCII characters available.
 * Notably there is no micro sign or superscript three -- write "ug/m3".
 *
 * Spelled as raw UTF-8 byte escapes so the compiler's source-encoding
 * assumptions cannot mangle them. */
#define UI_DEG    "\xC2\xB0"      /**< Degree sign, U+00B0 */

/* Icon for the humidity readout. There is no thermometer in the built-in
 * Montserrat/FontAwesome subset, so the temperature one is drawn instead --
 * see thermometer_create() in page_clock.c. */
#define UI_SYMBOL_HUMIDITY LV_SYMBOL_TINT
#define UI_BULLET "\xE2\x80\xA2"  /**< Bullet, U+2022 */

/* --- Icon font -----------------------------------------------------------
 * Navigation rail glyphs the built-in symbol font lacks, cut from Font
 * Awesome Free 6.7.2 Solid into src/ui/fonts/ui_font_icons_28.c (icons
 * CC BY 4.0, font SIL OFL 1.1 -- https://fontawesome.com). It carries only
 * these glyphs and falls back to Montserrat 28 for everything else, so the
 * LV_SYMBOL_* icons keep working through it.
 *
 * Not LVGL's bundled FontAwesome copy in lvgl/scripts/built_in_font: that one
 * is an old 5.x, and no Free 5.x font has the radio at all. To add a glyph,
 * fetch the font and regenerate with its code point appended to -r:
 *
 *   npm pack @fortawesome/fontawesome-free@6.7.2 && tar -xzf fortawesome-*.tgz
 *   npx lv_font_conv --no-compress --no-prefilter --bpp 4 --size 28
 *     --font package/webfonts/fa-solid-900.ttf
 *     -r 0xF6C4,0xF72E,0xF8D7,0xF0EB,0xF023,0xF3C1 --format lvgl --lv-include lvgl/lvgl.h
 *     --lv-font-name ui_font_icons_28 --lv-fallback lv_font_montserrat_28
 *     --force-fast-kern-format -o src/ui/fonts/ui_font_icons_28.c */
LV_FONT_DECLARE(ui_font_icons_28)
#define UI_FONT_ICON         (&ui_font_icons_28)

#define UI_SYMBOL_WEATHER "\xEF\x9B\x84"  /**< Cloud with sun, FontAwesome U+F6C4 */
#define UI_SYMBOL_AIR     "\xEF\x9C\xAE"  /**< Wind, FontAwesome U+F72E */
#define UI_SYMBOL_RADIO   "\xEF\xA3\x97"  /**< Classic radio, FontAwesome U+F8D7 */
#define UI_SYMBOL_DEVICES "\xEF\x83\xAB"  /**< Light bulb, FontAwesome U+F0EB */

/* --- Device icons ----------------------------------------------------------
 * The large glyphs in the middle of each device tile, from the same Font
 * Awesome 6.7.2 Solid source at 48 px (src/ui/fonts/ui_font_icons_48.c). Same
 * command as above with --size 48, --lv-font-name ui_font_icons_48,
 * --lv-fallback lv_font_montserrat_48, and these code points:
 *   -r 0xF0EB,0xF1E6,0xF863,0xF72E,0xF043,0xF773,0xF06D,0xF023,0xF3C1,
 *      0xF52A,0xF52B,0xF554,0xF205,0xF011,0xF2C9,0xE2CA
 * The curtain is drawn instead; Font Awesome Free has no blinds. */
LV_FONT_DECLARE(ui_font_icons_48)
#define UI_FONT_ICON_LG      (&ui_font_icons_48)

#define UI_GLYPH_BULB        "\xEF\x83\xAB"  /**< U+F0EB */
#define UI_GLYPH_PLUG        "\xEF\x87\xA6"  /**< U+F1E6 */
#define UI_GLYPH_FAN         "\xEF\xA1\xA3"  /**< U+F863 */
#define UI_GLYPH_WIND        "\xEF\x9C\xAE"  /**< U+F72E, air purifier */
#define UI_GLYPH_DROPLET     "\xEF\x81\x83"  /**< U+F043, dehumidifier */
#define UI_GLYPH_WATER       "\xEF\x9D\xB3"  /**< U+F773, humidifier */
#define UI_GLYPH_FIRE        "\xEF\x81\xAD"  /**< U+F06D, heater */
#define UI_GLYPH_LOCK        "\xEF\x80\xA3"  /**< U+F023; also in UI_FONT_ICON, for the lock panel */
#define UI_GLYPH_LOCK_OPEN   "\xEF\x8F\x81"  /**< U+F3C1; also in UI_FONT_ICON, for the lock panel */
#define UI_GLYPH_DOOR_CLOSED "\xEF\x94\xAA"  /**< U+F52A */
#define UI_GLYPH_DOOR_OPEN   "\xEF\x94\xAB"  /**< U+F52B */
#define UI_GLYPH_WALKING     "\xEF\x95\x94"  /**< U+F554, motion */
#define UI_GLYPH_TOGGLE      "\xEF\x88\x85"  /**< U+F205, switch */
#define UI_GLYPH_POWER       "\xEF\x80\x91"  /**< U+F011 */
#define UI_GLYPH_THERMOMETER "\xEF\x8B\x89"  /**< U+F2C9 */

/* A device icon that is off, or whose state has not arrived yet. Each type's
 * "on" colour is picked in smart_home_tile.c from the palette above. */
#define UI_COLOR_DEVICE_OFF  (ui_palette.device_off)

/* --- Metrics ----------------------------------------------------------- */
#define UI_GAP               12   /**< Gap between siblings */
#define UI_PAD               16   /**< Inner padding of a card */
#define UI_RADIUS            14   /**< Card corner radius */
#define UI_RAIL_WIDTH        84   /**< Navigation rail width */
#define UI_SLIDER_HEIGHT     14   /**< Slider track thickness */
#define UI_SLIDER_KNOB_PAD   10   /**< How far a slider knob stands off its track, all round */
#define UI_SLIDER_KNOB_GROW  3    /**< How much further it swells while pressed */

/** Room a slider keeps clear around its track for the knob -- pressed, with a
 *  couple of pixels to spare -- above and below it. */
#define UI_SLIDER_KNOB_ROOM     (UI_SLIDER_KNOB_PAD + UI_SLIDER_KNOB_GROW + 2)

/** The same, past either end of the track. */
#define UI_SLIDER_KNOB_OVERHANG (UI_SLIDER_HEIGHT / 2 + UI_SLIDER_KNOB_ROOM)

/* Minimum comfortable finger target on this panel (~170 DPI). */
#define UI_TOUCH_MIN         56

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Apply the base look (background, default text colour/font) to a screen.
 * @param screen   screen object to style
 */
void ui_theme_apply(lv_obj_t * screen);

/**
 * Create a card: the standard rounded surface every page is built from.
 * Comes with padding and a vertical flex layout already set up.
 * @param parent   parent object
 * @return         the card
 */
lv_obj_t * ui_card_create(lv_obj_t * parent);

/**
 * Create a flat tile for use inside a card (a single metric, an entity, ...).
 * @param parent   parent object
 * @return         the tile
 */
lv_obj_t * ui_tile_create(lv_obj_t * parent);

/**
 * Add a small dim heading to a card, as the first child.
 * @param card   card created by ui_card_create()
 * @param text   heading text
 * @return       the heading label
 */
lv_obj_t * ui_card_title(lv_obj_t * card, const char * text);

/**
 * Create a label with an explicit font and colour.
 * @param parent   parent object
 * @param text     initial text
 * @param font     font to use, e.g. UI_FONT_MD
 * @param color    text colour
 * @return         the label
 */
lv_obj_t * ui_label_create(lv_obj_t * parent, const char * text, const lv_font_t * font, lv_color_t color);

/**
 * Make a label occupy exactly one line and ellipsise anything that does not
 * fit.
 *
 * LV_LABEL_LONG_MODE_DOTS alone is not enough: lv_label only inserts the dots
 * when the laid-out text is TALLER than the label, so a label left at
 * LV_SIZE_CONTENT height simply grows to a second line and never ellipsises.
 * Pinning the height to one line is what makes the mode do anything. The
 * width still has to come from somewhere -- flex_grow or an explicit width.
 *
 * @param label   the label to constrain
 * @param font    the font the label is using; sets the one-line height
 */
void ui_label_single_line(lv_obj_t * label, const lv_font_t * font);

/**
 * Create a slider in the standard style: a UI_SLIDER_HEIGHT track, a round
 * knob sized for a fingertip, range 0..100.
 *
 * The knob stands UI_SLIDER_KNOB_PAD above and below the track, and swells by
 * UI_SLIDER_KNOB_GROW more while pressed. A parent clips its children to its
 * own box -- so a row sized to its content, only as tall as the track, would
 * slice the top and bottom off. The slider carries vertical margins of
 * UI_SLIDER_KNOB_ROOM, a pressed knob plus a little spare, which flex and grid
 * layouts make room for.
 *
 * At either end the knob also overhangs the track by UI_SLIDER_KNOB_OVERHANG.
 * That is left to the parent, which should leave that much beside each end:
 * as side padding where the slider meets the edge of the row, and as the
 * column gap where it meets a sibling -- or at 0 or 100 the knob covers the
 * neighbouring label.
 *
 * @param parent      parent object, normally a flex row
 * @param indicator   colour of the filled part of the track
 * @return            the slider
 */
lv_obj_t * ui_slider_create(lv_obj_t * parent, lv_color_t indicator);

/**
 * Colour for a US EPA air quality index value.
 * @param aqi   index value, 0..500
 * @return      band colour
 */
lv_color_t ui_aqi_color(int32_t aqi);

/**
 * Human readable band name for a US EPA air quality index value.
 * @param aqi   index value, 0..500
 * @return      band name, e.g. "Good"
 */
const char * ui_aqi_band(int32_t aqi);

/**
 * Switch the palette: the surface and text colours for a theme, and an accent.
 * Also re-initialises LVGL's default theme to match, for the parts of stock
 * widgets no page styles. Call before building the UI, or follow with
 * ui_rebuild() -- widgets already built keep the colours they were made with.
 * @param mode     dark or light
 * @param accent   index below UI_ACCENT_COUNT; out of range gives the first
 */
void ui_theme_set(ui_theme_mode_t mode, uint32_t accent);

/** @return   accent colour `index`, the first if out of range */
lv_color_t ui_accent_color(uint32_t index);

/** @return   the accent colour's name, e.g. "Teal" */
const char * ui_accent_name(uint32_t index);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_THEME_H*/
