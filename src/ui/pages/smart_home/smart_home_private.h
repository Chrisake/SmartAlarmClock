/**
 * @file smart_home_private.h
 *
 * Shared between the three files the devices page is split across:
 * page_smart_home.c (layout, rooms, scenes), smart_home_tile.c (the tiles)
 * and smart_home_modal.c (the controls panel). Not for use outside the page.
 */

#ifndef SMART_HOME_PRIVATE_H
#define SMART_HOME_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/smart_home/page_smart_home.h"

#include <stddef.h>

/**********************
 *      TYPEDEFS
 **********************/

/** What tapping a tile does. */
typedef enum {
    SH_TAP_NONE,     /**< Sensors */
    SH_TAP_TOGGLE,   /**< Flip power */
    SH_TAP_PANEL,    /**< Open the controls panel */
} sh_tap_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*page_smart_home.c*/

/** @return   the device, or NULL if the index is out of range */
const device_t * sh_device(uint32_t index);

/** Forward a request to the command callback. */
void sh_command(uint32_t index, device_attr_t attr, float value);

/** A transparent, non-scrolling, content-sized box that lets presses through. */
lv_obj_t * sh_box_create(lv_obj_t * parent);

/**
 * Format a number to the precision its step implies: "21" for a step of 1,
 * "21.5" for 0.5. Done by hand because lv_snprintf may be built without float.
 */
void sh_number_format(char * buf, size_t size, float value, float step);

/*smart_home_tile.c*/

/** Build the tile for device `index` into `parent`, sized `size` square. */
lv_obj_t * sh_tile_create(lv_obj_t * parent, uint32_t index, int32_t size);

/** Redraw a tile from its device's state. */
void sh_tile_refresh(lv_obj_t * tile, uint32_t index);

/** @return   what tapping the device's tile does */
sh_tap_t sh_device_tap(const device_t * device);

/** @return   the colour a device's icon takes while it is on */
lv_color_t sh_device_on_color(const device_t * device);

/** @return   true if the device is on; unknown counts as off */
bool sh_device_is_on(const device_t * device);

/*smart_home_modal.c*/

/** Open the controls panel for a device, replacing any open one. */
void sh_modal_open(uint32_t index);

/** Close the panel if one is open. */
void sh_modal_close(void);

/** Redraw the panel if it is showing device `index`. */
void sh_modal_refresh(uint32_t index);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SMART_HOME_PRIVATE_H*/
