/**
 * @file radio_private.h
 *
 * Shared between page_radio.c (the page and its station list) and
 * radio_search.c (the add dialog): the station row both of them list, a few
 * small widgets, and the dialog's entry points.
 */

#ifndef RADIO_PRIVATE_H
#define RADIO_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/radio/page_radio.h"

/*********************
 *      DEFINES
 *********************/

/** Side of the favicon at the start of a station row. */
#define RADIO_ROW_ICON_SIZE 48

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Style `row` as a station row and fill it with what every station row has:
 * the favicon, then the name over the subtitle. Anything added to the row
 * afterwards goes after them.
 * @param row       an empty object; the caller decides whether it is clickable
 * @param station   what to show
 */
void radio_row_fill(lv_obj_t * row, const page_radio_station_t * station);

/**
 * Show new details in a row built by radio_row_fill().
 * @param row       the row
 * @param station   what to show
 */
void radio_row_update(lv_obj_t * row, const page_radio_station_t * station);

/**
 * A rounded square holding a favicon on white, or the default music icon
 * without one.
 * @param parent       parent object
 * @param size         side of the square
 * @param image_size   side of the favicon inside it
 * @param font         font of the default icon
 * @param empty        the square's colour behind the default icon; pick one
 *                     that stands out from the parent
 * @return             the square
 */
lv_obj_t * radio_icon_create(lv_obj_t * parent, int32_t size, int32_t image_size, const lv_font_t * font,
                             lv_color_t empty);

/**
 * @param icon      a square from radio_icon_create()
 * @param favicon   the picture, or NULL for the default icon
 */
void radio_icon_set(lv_obj_t * icon, const lv_image_dsc_t * favicon);

/**
 * A round button with a symbol, which does not grow when pressed -- it would
 * be clipped by the rows and lists these sit in.
 */
lv_obj_t * radio_round_button_create(lv_obj_t * parent, const char * symbol, int32_t size);

/** Stop the default theme growing a pressed button past its edges. */
void radio_press_grow_off(lv_obj_t * obj);

/** Open the add dialog over the page, with the keyboard up. */
void radio_search_open(void);

/** Close the add dialog, if it is open. */
void radio_search_close(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*RADIO_PRIVATE_H*/
