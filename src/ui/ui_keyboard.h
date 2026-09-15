/**
 * @file ui_keyboard.h
 *
 * The on-screen keyboard: LVGL's own, made to work like Android's Gboard.
 *
 * - A letter layout per input language, as on Gboard. The settings page picks
 *   which languages are on offer, English always among them. With more than
 *   one, the globe key goes on to the next; the space bar names the one in
 *   use. The language is shared by every keyboard until changed, or restart.
 * - Shift capitalises the next letter; tapped twice, it locks capitals.
 * - Holding a key offers its alternatives -- the number on a top-row key,
 *   accented letters, more punctuation on the full stop -- in a pop-up above
 *   it: slide onto one and let go.
 * - A preview of the key shows above the finger. Sliding onto another key
 *   before letting go types that one instead.
 * - Sliding along the space bar moves the cursor.
 * - Two symbol pages, ?123 and =\<, back to the letters after a space.
 * - A phone number pad for numbers.
 * - In sentences, a capital to start each one, and a double space after a
 *   word becomes a full stop.
 *
 * Every keyboard must come from ui_keyboard_create(). The layouts replace
 * LVGL's built-in maps, which all keyboards share, and only keyboards made
 * here take the new maps when the language changes.
 */

#ifndef UI_KEYBOARD_H
#define UI_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"
#include "settings/settings.h"

/**********************
 *      TYPEDEFS
 **********************/

/** What a text area takes, which decides the keyboard's page and its help. */
typedef enum {
    UI_KEYBOARD_TEXT,       /**< Names, addresses, passwords: typed exactly as keyed */
    UI_KEYBOARD_SENTENCE,   /**< Prose: capitals to start sentences, double space for a full stop */
    UI_KEYBOARD_NUMBER,     /**< The number pad */
} ui_keyboard_kind_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a keyboard in the language in use. Size and place it as an
 * lv_keyboard, give it its text area with ui_keyboard_attach(), and listen
 * for LV_EVENT_READY (the Enter key) and LV_EVENT_CANCEL (the hide key).
 * @param parent   parent object
 * @return         the keyboard
 */
lv_obj_t * ui_keyboard_create(lv_obj_t * parent);

/**
 * Type into a text area, or into nothing. Starts on the letters, or the
 * number pad, with Shift off -- or on, at the start of a sentence.
 * @param keyboard   from ui_keyboard_create()
 * @param textarea   the text area, or NULL
 * @param kind       what it takes
 */
void ui_keyboard_attach(lv_obj_t * keyboard, lv_obj_t * textarea, ui_keyboard_kind_t kind);

/**
 * Set the languages on offer. English is added if missing. If the language in
 * use is dropped, keyboards go back to English.
 * @param languages   a bit per settings_keyboard_t, as in settings_t::keyboards
 */
void ui_keyboard_set_languages(uint32_t languages);

/** @return   a language's name in that language, e.g. "Ελληνικά" */
const char * ui_keyboard_language_name(settings_keyboard_t language);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_KEYBOARD_H*/
