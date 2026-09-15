/**
 * @file ui_weather_icon.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_weather_icon.h"
#include "ui/ui_theme.h"

/*********************
 *      DEFINES
 *********************/

/** Every shape below is laid out on a GRID x GRID square and scaled to the
 *  icon's real size, so one set of numbers serves every size. */
#define GRID 100

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void       icon_build(lv_obj_t * icon, int32_t size, ui_weather_t condition);
static lv_obj_t * blob_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t w, int32_t h,
                              lv_color_t color);
static void       sun_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t d);
static void       moon_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t d);
static void       cloud_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t w, lv_color_t color);
static void       drops_create(lv_obj_t * icon, int32_t size, int32_t dx, int32_t y);
static void       bolt_create(lv_obj_t * icon, int32_t size, int32_t y);
static void       flakes_create(lv_obj_t * icon, int32_t size, int32_t y);

static lv_color_t        surface_color(lv_obj_t * obj);
static const lv_font_t * glyph_font(int32_t px);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * ui_weather_icon_create(lv_obj_t * parent, int32_t size, ui_weather_t condition)
{
    lv_obj_t * icon = lv_obj_create(parent);

    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, size, size);
    lv_obj_set_scrollable(icon, false);
    lv_obj_set_clickable(icon, false);

    ui_weather_icon_set(icon, condition);

    return icon;
}

void ui_weather_icon_set(lv_obj_t * icon, ui_weather_t condition)
{
    /*Rebuilt rather than restyled: the conditions share too few parts for
     *reuse to be worth the bookkeeping, and this only runs when a forecast
     *lands.*/
    lv_obj_clean(icon);
    icon_build(icon, lv_obj_get_style_width(icon, LV_PART_MAIN), condition);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void icon_build(lv_obj_t * icon, int32_t size, ui_weather_t condition)
{
    /*Children draw in creation order, so whatever sits behind comes first.*/
    switch(condition) {
        case UI_WEATHER_CLEAR:
            sun_create(icon, size, 22, 22, 56);
            break;

        case UI_WEATHER_CLEAR_NIGHT:
            moon_create(icon, size, 22, 22, 56);
            break;

        case UI_WEATHER_PARTLY_CLOUDY:
            sun_create(icon, size, 46, 12, 42);
            cloud_create(icon, size, 4, 38, 76, UI_COLOR_CLOUD);
            break;

        case UI_WEATHER_PARTLY_CLOUDY_NIGHT:
            moon_create(icon, size, 46, 12, 42);
            cloud_create(icon, size, 4, 38, 76, UI_COLOR_CLOUD);
            break;

        case UI_WEATHER_CLOUDY:
            /*A darker cloud peeking out from behind is what separates this
             *from a single passing one.*/
            cloud_create(icon, size, 40, 16, 56, UI_COLOR_CLOUD_DARK);
            cloud_create(icon, size, 4, 34, 80, UI_COLOR_CLOUD);
            break;

        case UI_WEATHER_FOG:
            blob_create(icon, size, 8, 28, 84, 9, UI_COLOR_CLOUD);
            blob_create(icon, size, 18, 46, 70, 9, UI_COLOR_CLOUD);
            blob_create(icon, size, 8, 64, 84, 9, UI_COLOR_CLOUD);
            break;

        case UI_WEATHER_SHOWERS:
            sun_create(icon, size, 52, 8, 38);
            cloud_create(icon, size, 6, 24, 76, UI_COLOR_CLOUD);
            drops_create(icon, size, -6, 72);
            break;

        case UI_WEATHER_RAIN:
            cloud_create(icon, size, 8, 10, 84, UI_COLOR_CLOUD_DARK);
            drops_create(icon, size, 0, 66);
            break;

        case UI_WEATHER_THUNDERSTORM:
            cloud_create(icon, size, 8, 6, 84, UI_COLOR_CLOUD_DARK);
            bolt_create(icon, size, 48);
            break;

        case UI_WEATHER_SNOW:
            cloud_create(icon, size, 8, 8, 84, UI_COLOR_CLOUD);
            flakes_create(icon, size, 64);
            break;

        case UI_WEATHER_COUNT:
        default:
            break;
    }
}

/**
 * One filled, fully rounded shape: a disc when square, a pill otherwise.
 * Position and size are in grid units; nothing is allowed to scale away to
 * less than 2 px.
 */
static lv_obj_t * blob_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t w, int32_t h,
                              lv_color_t color)
{
    lv_obj_t * obj = lv_obj_create(icon);

    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x * size / GRID, y * size / GRID);
    lv_obj_set_size(obj, LV_MAX(w * size / GRID, 2), LV_MAX(h * size / GRID, 2));
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);

    return obj;
}

static void sun_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t d)
{
    lv_obj_t * disc = blob_create(icon, size, x, y, d, d, UI_COLOR_SUN);

    /*A faint ring standing off the disc reads as its glow, without the
     *rotated objects that drawing rays would take.*/
    lv_obj_set_style_outline_color(disc, UI_COLOR_SUN, LV_PART_MAIN);
    lv_obj_set_style_outline_opa(disc, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_outline_width(disc, LV_MAX(size / 32, 1), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(disc, LV_MAX(size / 16, 2), LV_PART_MAIN);
}

static void moon_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t d)
{
    blob_create(icon, size, x, y, d, d, UI_COLOR_MOON);

    /*The crescent is a full disc with a bite taken out of its upper right by
     *a second disc in the colour of whatever the icon sits on.*/
    int32_t bite = d * 4 / 5;
    blob_create(icon, size, x + d * 2 / 5, y - d / 6, bite, bite, surface_color(icon));
}

/**
 * Two puffs sitting on a rounded base. `w` is the cloud's width; it comes out
 * about 0.6 w tall.
 */
static void cloud_create(lv_obj_t * icon, int32_t size, int32_t x, int32_t y, int32_t w, lv_color_t color)
{
    blob_create(icon, size, x, y + w * 30 / 100, w, w * 30 / 100, color);
    blob_create(icon, size, x + w * 12 / 100, y + w * 12 / 100, w * 42 / 100, w * 42 / 100, color);
    blob_create(icon, size, x + w * 36 / 100, y, w * 54 / 100, w * 54 / 100, color);
}

/**
 * Three short streaks under a cloud. The middle one hangs lower, so they read
 * as falling rather than as a bar chart.
 * @param dx   horizontal shift, to sit under a cloud that is off-centre
 * @param y    top of the streaks
 */
static void drops_create(lv_obj_t * icon, int32_t size, int32_t dx, int32_t y)
{
    static const int32_t xs[3] = {30, 47, 64};

    for(uint32_t i = 0; i < 3; i++) {
        blob_create(icon, size, xs[i] + dx, y + (i == 1 ? 8 : 0), 6, 18, UI_COLOR_RAIN);
    }
}

/**
 * A lightning bolt. There is one in the symbol font, and a zigzag lv_line
 * cannot be scaled with the icon: percentage points do not survive being
 * stored as floats with LV_USE_FLOAT on.
 * @param y   top of the glyph
 */
static void bolt_create(lv_obj_t * icon, int32_t size, int32_t y)
{
    lv_obj_t * bolt = ui_label_create(icon, LV_SYMBOL_CHARGE, glyph_font(size * 40 / GRID), UI_COLOR_SUN);
    lv_obj_align(bolt, LV_ALIGN_TOP_MID, 0, y * size / GRID);
}

static void flakes_create(lv_obj_t * icon, int32_t size, int32_t y)
{
    static const lv_point_t spots[5] = {{24, 0}, {45, 8}, {66, 0}, {34, 20}, {56, 20}};

    for(uint32_t i = 0; i < 5; i++) {
        blob_create(icon, size, spots[i].x, y + spots[i].y, 10, 10, UI_COLOR_SNOW);
    }
}

/**
 * Background colour of the nearest ancestor that paints one. Falls back to the
 * screen colour if nothing does.
 */
static lv_color_t surface_color(lv_obj_t * obj)
{
    for(lv_obj_t * p = obj; p != NULL; p = lv_obj_get_parent(p)) {
        if(lv_obj_get_style_bg_opa(p, LV_PART_MAIN) >= LV_OPA_COVER) {
            return lv_obj_get_style_bg_color(p, LV_PART_MAIN);
        }
    }

    return UI_COLOR_BG;
}

/**
 * The largest theme font no taller than roughly `px`, for glyphs scaled with
 * the icon.
 */
static const lv_font_t * glyph_font(int32_t px)
{
    if(px <= 15) return UI_FONT_XS;
    if(px <= 18) return UI_FONT_SM;
    if(px <= 24) return UI_FONT_MD;
    if(px <= 31) return UI_FONT_LG;
    if(px <= 41) return UI_FONT_XL;
    return UI_FONT_CLOCK;
}
