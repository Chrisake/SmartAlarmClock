/**
 * @file ui_moon_icon.h
 *
 * The moon as it looks tonight: a disc lit from one side by as much as its
 * phase says.
 *
 * Drawn from two discs, like the weather icons: the moon itself, and a second
 * disc slid across it and clipped to it -- in the shadow's colour for a
 * crescent, in the moonlight's for a gibbous moon. The line between light and
 * shadow comes out as an arc rather than the true half-ellipse, which at icon
 * size nobody can tell.
 */

#ifndef UI_MOON_ICON_H
#define UI_MOON_ICON_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a moon icon, not clickable, showing a full moon until set.
 * @param parent   parent object
 * @param size     width and height in pixels
 * @return         the icon
 */
lv_obj_t * ui_moon_icon_create(lv_obj_t * parent, int32_t size);

/**
 * @param icon       icon created by ui_moon_icon_create()
 * @param age        0..1 through the cycle: 0 new, 0.5 full
 * @param southern   seen from south of the equator, where a waxing moon is lit on the left
 */
void ui_moon_icon_set(lv_obj_t * icon, float age, bool southern);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_MOON_ICON_H*/
