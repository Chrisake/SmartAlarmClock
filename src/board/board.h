/**
 * @file board.h
 *
 * Display profile of the board this simulator build is mimicking.
 *
 * The values come from boards/boards.cmake as compile definitions, selected
 * with -DBOARD=<id> at configure time. The fallbacks below keep the sources
 * readable in an editor that has not picked up the CMake configuration yet;
 * they mirror the default board profile.
 */

#ifndef BOARD_H
#define BOARD_H

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

/** Human readable board name, shown in the simulator window title. */
#ifndef BOARD_NAME
  #define BOARD_NAME "ESP32-P4-WIFI6-Touch-LCD-7B"
#endif

/** Panel width in pixels. */
#ifndef BOARD_HOR_RES
  #define BOARD_HOR_RES 1024
#endif

/** Panel height in pixels. */
#ifndef BOARD_VER_RES
  #define BOARD_VER_RES 600
#endif

/** Panel colour depth: 16 for RGB565, 32 for XRGB8888. */
#ifndef BOARD_COLOR_DEPTH
  #define BOARD_COLOR_DEPTH 16
#endif

/** Panel pixel density, sqrt(hor_res^2 + ver_res^2) / diagonal_inches. */
#ifndef BOARD_DPI
  #define BOARD_DPI 170
#endif

/** Scale factor for the simulator window. The framebuffer is unaffected. */
#ifndef BOARD_SIM_ZOOM
  #define BOARD_SIM_ZOOM 1.0f
#endif

/**********************
 *      MACROS
 **********************/

#define BOARD_STRINGIFY_(x) #x
#define BOARD_STRINGIFY(x)  BOARD_STRINGIFY_(x)

/** Resolution as a string literal, e.g. "1024x600". */
#define BOARD_RES_STR BOARD_STRINGIFY(BOARD_HOR_RES) "x" BOARD_STRINGIFY(BOARD_VER_RES)

/** Simulator window title, e.g. "ESP32-P4-WIFI6-Touch-LCD-7B | 1024x600". */
#define BOARD_WINDOW_TITLE BOARD_NAME " | " BOARD_RES_STR

/* Rendering at a different colour depth than the panel hides banding and
 * blending artefacts that do show up on the device, so treat a mismatch
 * between the board profile and lv_conf.h as a build error. */
#if LV_COLOR_DEPTH != BOARD_COLOR_DEPTH
  #error "LV_COLOR_DEPTH does not match BOARD_COLOR_DEPTH; reconfigure CMake so the board profile is applied."
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*BOARD_H*/
