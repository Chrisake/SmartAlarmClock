/**
 * @file ui_weather_icon.h
 *
 * A weather condition, drawn.
 *
 * The built-in Montserrat/FontAwesome subset has no weather glyphs, so these
 * icons are assembled from plain rounded objects -- the same trick as the
 * thermometer on the clock page -- and scale to whatever size they are asked
 * for. Callers only ever deal in ui_weather_t, so the drawing can be swapped
 * for an icon font or images later without touching a page.
 */

#ifndef UI_WEATHER_ICON_H
#define UI_WEATHER_ICON_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/**********************
 *      TYPEDEFS
 **********************/

/** Conditions an icon can show. Day and night variants are separate values:
 *  the weather service knows sunrise and sunset, the icon does not. */
typedef enum {
    UI_WEATHER_CLEAR,
    UI_WEATHER_CLEAR_NIGHT,
    UI_WEATHER_PARTLY_CLOUDY,
    UI_WEATHER_PARTLY_CLOUDY_NIGHT,
    UI_WEATHER_CLOUDY,
    UI_WEATHER_FOG,
    UI_WEATHER_SHOWERS,       /**< Sun and rain */
    UI_WEATHER_RAIN,
    UI_WEATHER_THUNDERSTORM,
    UI_WEATHER_SNOW,
    UI_WEATHER_COUNT,
} ui_weather_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a weather icon.
 *
 * The icon is not clickable, so a press on it falls through to whatever it
 * sits in.
 *
 * The moon's crescent is cut with a disc in the colour of the nearest opaque
 * ancestor, looked up whenever the condition is set. Give the surface its
 * background before creating the icon on it.
 *
 * @param parent      parent object
 * @param size        width and height in pixels
 * @param condition   what to draw
 * @return            the icon
 */
lv_obj_t * ui_weather_icon_create(lv_obj_t * parent, int32_t size, ui_weather_t condition);

/**
 * Change the condition an icon shows.
 * @param icon        icon created by ui_weather_icon_create()
 * @param condition   what to draw
 */
void ui_weather_icon_set(lv_obj_t * icon, ui_weather_t condition);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_WEATHER_ICON_H*/
