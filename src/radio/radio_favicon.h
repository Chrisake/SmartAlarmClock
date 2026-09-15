/**
 * @file radio_favicon.h
 *
 * Turns a downloaded station favicon into the square image the station list
 * draws.
 *
 * Reads PNG, JPEG, BMP, GIF (its first frame) and ICO (with a PNG or a BMP
 * inside), through stb_image. SVG and WebP are not read: those stations keep
 * the default icon. The picture is fitted into the square keeping its shape,
 * with transparent margins.
 *
 * Plain C using malloc(), never LVGL's allocator, so it runs on http_worker's
 * threads.
 */

#ifndef RADIO_FAVICON_H
#define RADIO_FAVICON_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stddef.h>
#include <stdint.h>

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @param data   the file as downloaded
 * @param len    its length
 * @param size   side of the square wanted, in pixels
 * @return       size x size pixels of LVGL ARGB8888 (B, G, R, A bytes),
 *               malloc'd; NULL if the file could not be read
 */
uint8_t * radio_favicon_convert(const uint8_t * data, size_t len, uint32_t size);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*RADIO_FAVICON_H*/
