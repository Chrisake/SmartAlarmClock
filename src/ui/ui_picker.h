/**
 * @file ui_picker.h
 *
 * A wheel picker in the style of iOS: a panel over the dimmed screen with an x
 * in the top right, one or more wheels, and Apply. Scroll each wheel until
 * the value wanted sits in the middle band, then Apply; the x, or a tap
 * outside, leaves everything as it was. Nothing is reported while scrolling.
 *
 * One picker at a time; opening another replaces it. It lives on the top
 * layer, so a page that opens one should close it from its on_hide().
 */

#ifndef UI_PICKER_H
#define UI_PICKER_H

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

#define UI_PICKER_COLUMNS_MAX 4

/**********************
 *      TYPEDEFS
 **********************/

/** One wheel. */
typedef struct {
    const char * options;    /**< Newline-separated; copied */
    uint32_t     selected;   /**< Index the wheel opens on */
    bool         infinite;   /**< Wraps round, like hours and minutes */
    int32_t      width;      /**< Pixels; 0 shares the width with the other 0s */
} ui_picker_column_t;

/**
 * Apply was tapped.
 * @param selected   index chosen on each wheel, in column order
 * @param count      number of wheels
 * @param user       as given to ui_picker_open()
 */
typedef void (*ui_picker_apply_cb_t)(const uint32_t selected[], uint32_t count, void * user);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Open the picker.
 * @param columns   the wheels, left to right
 * @param count     clamped to UI_PICKER_COLUMNS_MAX
 * @param apply     called on Apply, just before the picker closes
 * @param user      passed back to `apply`
 */
void ui_picker_open(const ui_picker_column_t columns[], uint32_t count, ui_picker_apply_cb_t apply, void * user);

/** Close the picker if one is open, applying nothing. */
void ui_picker_close(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_PICKER_H*/
