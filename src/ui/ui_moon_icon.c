/**
 * @file ui_moon_icon.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_moon_icon.h"
#include "ui/ui_theme.h"

#include <math.h>

/*********************
 *      DEFINES
 *********************/

#define TWO_PI 6.2831853f

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * ui_moon_icon_create(lv_obj_t * parent, int32_t size)
{
    lv_obj_t * disc = lv_obj_create(parent);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, size, size);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, LV_PART_MAIN);
    /*Keeps the sliding disc within the moon.*/
    lv_obj_set_style_clip_corner(disc, true, LV_PART_MAIN);
    lv_obj_set_scrollable(disc, false);
    lv_obj_set_clickable(disc, false);

    lv_obj_t * cover = lv_obj_create(disc);
    lv_obj_remove_style_all(cover);
    lv_obj_set_size(cover, size, size);
    lv_obj_set_style_radius(cover, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_clickable(cover, false);

    ui_moon_icon_set(disc, 0.5f, false);
    return disc;
}

void ui_moon_icon_set(lv_obj_t * icon, float age, bool southern)
{
    /*The size as set: the laid-out one is 0 until the icon is first drawn.*/
    int32_t    size  = lv_obj_get_style_width(icon, LV_PART_MAIN);
    lv_obj_t * cover = lv_obj_get_child(icon, 0);

    age -= floorf(age);

    float lit      = (1.0f - cosf(TWO_PI * age)) / 2.0f;
    bool  crescent = lit < 0.5f;

    /*Seen from the north, a waxing moon is lit on its right.*/
    int32_t toward_light = ((age < 0.5f) != southern) ? 1 : -1;

    /*A crescent: the shadow slides off the lit side, uncovering `lit` of the
     *width. A gibbous moon: the light slides over the shadow, leaving the
     *unlit share uncovered.*/
    int32_t shift = crescent ? -toward_light * (int32_t)lroundf((float)size * lit)
                             : toward_light * (int32_t)lroundf((float)size * (1.0f - lit));

    /*Whichever of the two is lighter stands for the moonlight: the moon's
     *colour on the dark theme, the border's on the light one, where the moon's
     *grey is the darker.*/
    bool       moon_lighter = lv_color_brightness(UI_COLOR_MOON) >= lv_color_brightness(UI_COLOR_BORDER);
    lv_color_t light        = moon_lighter ? UI_COLOR_MOON : UI_COLOR_BORDER;
    lv_color_t shadow       = moon_lighter ? UI_COLOR_BORDER : UI_COLOR_MOON;

    lv_obj_set_style_bg_color(icon, crescent ? light : shadow, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cover, crescent ? shadow : light, LV_PART_MAIN);
    lv_obj_set_pos(cover, shift, 0);
}
