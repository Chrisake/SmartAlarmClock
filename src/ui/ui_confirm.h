/**
 * @file ui_confirm.h
 *
 * Ask before doing something that cannot be undone: a panel over the dimmed
 * screen with a title, an optional line of explanation, Cancel and a red
 * action button. Cancel, or a tap outside, leaves everything as it was.
 *
 * One at a time; opening another replaces it. It lives on the top layer, so a
 * page that opens one should close it from its on_hide(), and whenever what
 * it asks about goes away.
 */

#ifndef UI_CONFIRM_H
#define UI_CONFIRM_H

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

/**
 * The action was tapped.
 * @param user   as given to ui_confirm_open()
 */
typedef void (*ui_confirm_cb_t)(void * user);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Open the confirmation.
 * @param title       what is at stake, e.g. the name of what goes; copied
 * @param message     one or two lines under it, or NULL; copied
 * @param action      the red button's text, e.g. "Remove"; copied
 * @param confirmed   called on the action, just after the panel closes
 * @param user        passed back to `confirmed`
 */
void ui_confirm_open(const char * title, const char * message, const char * action, ui_confirm_cb_t confirmed,
                     void * user);

/** Close the confirmation if one is open, confirming nothing. */
void ui_confirm_close(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_CONFIRM_H*/
