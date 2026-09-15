/**
 * @file radio_store.h
 *
 * The saved stations, on the SD card:
 *
 *   /radio/stations.json          {"stations": ["<uuid>", ...]}, in list order
 *   /radio/<uuid>/station.json    what Radio Browser said about the station
 *   /radio/<uuid>/favicon.bin     its favicon, ready to draw
 *
 * favicon.bin is an LVGL image file: a 12-byte lv_image_header_t, then
 * RADIO_FAVICON_SIZE x RADIO_FAVICON_SIZE pixels of ARGB8888. A favicon is
 * converted once, when it is downloaded, so drawing one never decodes a PNG
 * or JPEG. A station with no favicon file shows the default icon: it had no
 * favicon, the download failed, or the format could not be read.
 *
 * Plain C with no LVGL: favicons are written from http_worker's threads.
 */

#ifndef RADIO_STORE_H
#define RADIO_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "radio/radio_station.h"

/*********************
 *      DEFINES
 *********************/

#if defined(ESP_PLATFORM)
  #define RADIO_STORE_ROOT "/sdcard/radio"
#else
  /** The simulator's SD card: a directory under the working directory. */
  #define RADIO_STORE_ROOT "data/sdcard/radio"
#endif

/** Most stations the list holds. */
#define RADIO_STORE_MAX 32

/** Side of a stored favicon, in pixels. */
#define RADIO_FAVICON_SIZE 96

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** @return   true if a list has ever been saved -- even an empty one */
bool radio_store_has_list(void);

/**
 * @param uuids   receives the saved stations' UUIDs, in list order
 * @param max     size of `uuids`
 * @return        how many were read; 0 if there is no list
 */
uint32_t radio_store_load_list(char uuids[][RADIO_UUID_LEN], uint32_t max);

/**
 * @param uuids   the stations' UUIDs, in list order
 * @param count   how many
 * @return        false if the card could not be written
 */
bool radio_store_save_list(const char * const uuids[], uint32_t count);

/**
 * @param uuid   the station
 * @param out    receives its station.json; untouched on failure
 * @return       false if there is none, or it could not be read
 */
bool radio_store_load_station(const char * uuid, radio_station_t * out);

/** Write a station's station.json, creating its directory. */
bool radio_store_save_station(const radio_station_t * station);

/** Delete a station's directory and everything in it. */
void radio_store_remove_station(const char * uuid);

/**
 * Write a station's favicon.bin.
 * @param uuid     the station
 * @param pixels   RADIO_FAVICON_SIZE squared ARGB8888 pixels
 */
bool radio_store_save_favicon(const char * uuid, const uint8_t * pixels);

/**
 * @param uuid   the station
 * @return       its favicon's ARGB8888 pixels, malloc'd; NULL if it has none
 */
uint8_t * radio_store_load_favicon(const char * uuid);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*RADIO_STORE_H*/
