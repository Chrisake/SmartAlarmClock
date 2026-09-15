/**
 * @file ui_page.h
 *
 * The contract every page implements.
 *
 * A page owns one root object and knows nothing about the shell around it or
 * about its siblings. To add a page:
 *
 *   1. Create src/ui/pages/<name>/ with page_<name>.h / page_<name>.c
 *   2. Fill in a static ui_page_t and return it from page_<name>_desc()
 *   3. Add an id to ui_page_id_t and the descriptor to the table in ui.c
 *
 * CMake picks up the new files automatically; nothing else has to change.
 */

#ifndef UI_PAGE_H
#define UI_PAGE_H

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
 * Describes one page to the shell.
 */
typedef struct {
    /** Title shown in the navigation rail. */
    const char * title;

    /** LV_SYMBOL_* glyph shown in the navigation rail. */
    const char * icon;

    /**
     * Build the page's widget tree.
     * Called once during ui_init(). Must not assume it is visible.
     * @param parent   container to build into; fills it completely
     * @return         the page's root object
     */
    lv_obj_t * (*create)(lv_obj_t * parent);

    /**
     * Called every time the page becomes visible. @nullable
     * Start refresh timers and subscribe to data sources here.
     */
    void (*on_show)(void);

    /**
     * Called every time the page is hidden. @nullable
     * Stop timers and unsubscribe here, so background pages cost nothing.
     */
    void (*on_hide)(void);
} ui_page_t;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_PAGE_H*/
