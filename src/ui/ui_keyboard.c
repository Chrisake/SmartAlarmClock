/**
 * @file ui_keyboard.c
 *
 * Letters are written here as UTF-8 text rather than byte escapes, so a
 * layout can be checked by eye; the build compiles sources as UTF-8.
 *
 * The keys are one lv_keyboard -- a button matrix, one object however many
 * keys, which keeps drawing and touch cheap. Around it:
 *
 * - key_clicked() replaces LVGL's key handler: typing, Shift, the pages, the
 *   language, Enter and the hide key.
 * - key_touched() follows the finger as a phone keyboard does: the preview,
 *   sliding between keys, the space bar moving the cursor, and holding a key
 *   for its alternatives.
 * - key_drawn() restyles keys as the button matrix draws them: number hints,
 *   the language on the space bar, Shift's state, the Enter key.
 *
 * The preview and the alternatives are small objects on the top layer, above
 * whatever the keyboard covers.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_keyboard.h"
#include "ui/ui_theme.h"
#include "lvgl/src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include "lvgl/src/misc/lv_area_private.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** Letter rows in a layout. */
#define ROWS 3

/** Entries in one case's map: keys, row breaks and the empty string ending it. */
#define MAP_MAX 64

/** Bytes for one case's letters, each key NUL-terminated. */
#define POOL_BYTES 256

/** Keyboards alive at once. The UI has three. */
#define LIVE_MAX 8

/** A letter key's width in button matrix units, and the widest a key can be. */
#define KEY_UNITS 2
#define UNITS_MAX 15

/* Keys that do something rather than type. Shift and the globe are Font
 * Awesome glyphs in the text fonts, U+F062 and U+F0AC. */
#define KEY_SHIFT   "\xEF\x81\xA2"
#define KEY_GLOBE   "\xEF\x82\xAC"
#define KEY_SYMBOLS "?123"
#define KEY_MORE    "=\\<"
#define KEY_LETTERS "ABC"
#define KEY_HIDE    LV_SYMBOL_KEYBOARD
#define KEY_ENTER   LV_SYMBOL_NEW_LINE
#define KEY_BACK    LV_SYMBOL_BACKSPACE
#define KEY_SPACE   " "

/* Typing keys act on release and never repeat, as on a phone -- which is also
 * what lets a finger slide from key to key before letting go. Function keys
 * the same, shaded. Backspace acts on press and repeats while held. */
#define CTRL_CHAR   (LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT)
#define CTRL_FUNC   (CTRL_CHAR | LV_BUTTONMATRIX_CTRL_CHECKED)
#define CTRL_BACK   LV_BUTTONMATRIX_CTRL_CHECKED
#define CTRL_SPACER LV_BUTTONMATRIX_CTRL_HIDDEN

/** Look of the keys. */
#define KEYBOARD_PAD 6
#define KEY_GAP_H    6
#define KEY_GAP_V    8
#define KEY_RADIUS   8
#define HINT_INSET_X 7
#define HINT_INSET_Y 3

/** A second tap on Shift within this locks capitals. */
#define SHIFT_LOCK_MS 400

/** A second space within this, after a word, ends the sentence. */
#define DOUBLE_SPACE_MS 800

/** How far a finger slides along the space bar before the cursor follows, and then per character. */
#define SWIPE_START_PX 20
#define SWIPE_STEP_PX  18

/** Alternatives a key can offer, how many to a row, bytes for each, and their cells. */
#define ALT_MAX        16
#define ALT_COLUMNS    8
#define ALT_TEXT_BYTES 8
#define ALT_CELL_WIDTH 64
#define ALT_PAD        4

/** The preview is this much larger than its key, and covers this share of it. */
#define BUBBLE_GROW_W  8
#define BUBBLE_GROW_H  16

/**********************
 *      TYPEDEFS
 **********************/

/** A language's letters, top row first, each row's keys separated by spaces.
 *  Both cases have the same number of keys in each row. */
typedef struct {
    const char * name;   /**< In the language itself, on the space bar */
    const char * lower[ROWS];
    const char * upper[ROWS];
} layout_t;

/** One case of the letters, in the form the button matrix takes. */
typedef struct {
    const char *           map[MAP_MAX];
    lv_buttonmatrix_ctrl_t ctrl[MAP_MAX];
    const char *           hints[MAP_MAX];   /**< By key: the number a long press offers first, or NULL */
    char                   pool[POOL_BYTES];  /**< The letter keys' texts */
} keymap_t;

typedef struct {
    keymap_t * km;
    uint32_t   entry;
    uint32_t   button;
    size_t     used;
} build_t;

typedef struct {
    lv_obj_t *         keyboard;
    ui_keyboard_kind_t kind;
} live_t;

typedef enum {
    SHIFT_OFF,
    SHIFT_ONCE,
    SHIFT_LOCKED,
} shift_t;

typedef struct {
    const char * key;
    const char * alternatives;   /**< Separated by spaces, most likely first */
} alternatives_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void     layout_apply(void);
static void     language_next(void);
static void     letters_build(keymap_t * km, const char * const rows[ROWS], bool globe);
static void     key_add(build_t * b, const char * text, lv_buttonmatrix_ctrl_t ctrl);
static void     spacer_add(build_t * b, uint32_t units);
static void     letters_add(build_t * b, const char * row, bool hints);
static uint32_t letters_count(const char * row);
static void     maps_set(lv_obj_t * keyboard);
static void     colors_get(lv_color_t * back, lv_color_t * key, lv_color_t * function);
static void     style_apply(lv_obj_t * keyboard);
static uint32_t enabled_count(void);
static live_t * live_find(const lv_obj_t * keyboard);

static void key_clicked(lv_event_t * e);
static void key_touched(lv_event_t * e);
static void key_drawn(lv_event_t * e);
static void keyboard_deleted(lv_event_t * e);

static void   type_text(lv_obj_t * keyboard, const char * text);
static void   action_send(lv_obj_t * keyboard, lv_event_code_t code);
static void   page_set(lv_obj_t * keyboard, lv_keyboard_mode_t mode);
static bool   letters_showing(lv_obj_t * keyboard);
static void   shift_tapped(lv_obj_t * keyboard);
static void   shift_set(lv_obj_t * keyboard, shift_t state, bool automatic);
static void   shift_auto(lv_obj_t * keyboard);
static bool   at_sentence_start(lv_obj_t * textarea);
static bool   after_word_and_space(lv_obj_t * textarea);
static size_t cursor_byte(lv_obj_t * textarea, const char ** text);

static void press_start(lv_obj_t * keyboard);
static void press_move(lv_obj_t * keyboard);
static void press_hold(lv_obj_t * keyboard);
static void press_end(lv_obj_t * keyboard, bool released);
static void press_forget(void);
static bool pointer_get(lv_point_t * point);

static bool         key_area(lv_obj_t * keyboard, uint32_t id, lv_area_t * area);
static uint32_t     key_at(lv_obj_t * keyboard, const lv_point_t * point);
static bool         key_is_function(const char * text);
static const char * key_hint(lv_obj_t * keyboard, uint32_t id);

static void bubble_show(lv_obj_t * keyboard, uint32_t id);
static void bubble_hide(void);
static void bubble_deleted(lv_event_t * e);

static uint32_t alt_collect(lv_obj_t * keyboard, uint32_t id);
static void     alt_open(lv_obj_t * keyboard, uint32_t id);
static void     alt_select(const lv_point_t * point);
static void     alt_highlight(uint32_t index);
static void     alt_close(void);

static void   line_area(const lv_area_t * key, const lv_font_t * font, lv_area_t * line);
static void   label_draw(lv_layer_t * layer, const lv_area_t * area, const char * text, const lv_font_t * font,
                         lv_color_t color, lv_text_align_t align);
static size_t utf8_length(const char * text);

/**********************
 *  STATIC VARIABLES
 **********************/

/*Gboard's layouts. Letters Gboard keeps behind a long press stay there.*/
static const layout_t layouts[SETTINGS_KEYBOARD_COUNT] = {
    [SETTINGS_KEYBOARD_EN] = {
        "English",
        {"q w e r t y u i o p", "a s d f g h j k l", "z x c v b n m"},
        {"Q W E R T Y U I O P", "A S D F G H J K L", "Z X C V B N M"},
    },
    [SETTINGS_KEYBOARD_EL] = {
        /*Final sigma has no capital, so it stays on the upper case too.*/
        "Ελληνικά",
        {"; ς ε ρ τ υ θ ι ο π", "α σ δ φ γ η ξ κ λ", "ζ χ ψ ω β ν μ"},
        {"; ς Ε Ρ Τ Υ Θ Ι Ο Π", "Α Σ Δ Φ Γ Η Ξ Κ Λ", "Ζ Χ Ψ Ω Β Ν Μ"},
    },
    [SETTINGS_KEYBOARD_DE] = {
        "Deutsch",
        {"q w e r t z u i o p ü", "a s d f g h j k l ö ä", "y x c v b n m"},
        {"Q W E R T Z U I O P Ü", "A S D F G H J K L Ö Ä", "Y X C V B N M"},
    },
    [SETTINGS_KEYBOARD_FR] = {
        "Français",
        {"a z e r t y u i o p", "q s d f g h j k l m", "w x c v b n '"},
        {"A Z E R T Y U I O P", "Q S D F G H J K L M", "W X C V B N '"},
    },
    [SETTINGS_KEYBOARD_ES] = {
        "Español",
        {"q w e r t y u i o p", "a s d f g h j k l ñ", "z x c v b n m"},
        {"Q W E R T Y U I O P", "A S D F G H J K L Ñ", "Z X C V B N M"},
    },
    [SETTINGS_KEYBOARD_RU] = {
        "Русский",
        {"й ц у к е н г ш щ з х", "ф ы в а п р о л д ж э", "я ч с м и т ь б ю"},
        {"Й Ц У К Е Н Г Ш Щ З Х", "Ф Ы В А П Р О Л Д Ж Э", "Я Ч С М И Т Ь Б Ю"},
    },
    [SETTINGS_KEYBOARD_UK] = {
        "Українська",
        {"й ц у к е н г ш щ з х ї", "ф і в а п р о л д ж є", "я ч с м и т ь б ю"},
        {"Й Ц У К Е Н Г Ш Щ З Х Ї", "Ф І В А П Р О Л Д Ж Є", "Я Ч С М И Т Ь Б Ю"},
    },
};

/*What holding a key offers, after a top-row key's number. One table for
 *every language: a key's text is the same letter whichever layout it is on.*/
static const alternatives_t alternatives[] = {
    {"a", "à á â ä æ ã å ā"}, {"A", "À Á Â Ä Æ Ã Å Ā"},
    {"c", "ç ć č"},           {"C", "Ç Ć Č"},
    {"e", "è é ê ë ē ę ė"},   {"E", "È É Ê Ë Ē Ę Ė"},
    {"i", "î ï í ī į ì"},     {"I", "Î Ï Í Ī Į Ì"},
    {"l", "ł"},               {"L", "Ł"},
    {"n", "ñ ń"},             {"N", "Ñ Ń"},
    {"o", "ô ö ò ó œ ø ō õ"}, {"O", "Ô Ö Ò Ó Œ Ø Ō Õ"},
    {"s", "ß ś š"},           {"S", "Ś Š"},
    {"u", "û ü ù ú ū"},       {"U", "Û Ü Ù Ú Ū"},
    {"y", "ÿ ý"},             {"Y", "Ÿ Ý"},
    {"z", "ž ź ż"},           {"Z", "Ž Ź Ż"},

    {"α", "ά"},     {"Α", "Ά"},
    {"ε", "έ"},     {"Ε", "Έ"},
    {"η", "ή"},     {"Η", "Ή"},
    {"ι", "ί ϊ ΐ"}, {"Ι", "Ί Ϊ"},
    {"ο", "ό"},     {"Ο", "Ό"},
    {"υ", "ύ ϋ ΰ"}, {"Υ", "Ύ Ϋ"},
    {"ω", "ώ"},     {"Ω", "Ώ"},
    {";", ":"},

    {"е", "ё"}, {"Е", "Ё"},
    {"ь", "ъ"}, {"Ь", "Ъ"},
    {"г", "ґ"}, {"Г", "Ґ"},

    {".", "… , ? ! ' \" : ; - @ /"},
    {"'", "‘ ’ ‚ \" « »"},
    {"\"", "“ ” „ « »"},
    {"-", "_ – — ·"},
    {"+", "±"},
    {"(", "< [ {"},
    {")", "> ] }"},
    {"!", "¡"},
    {"?", "¿"},
    {"$", "¢ € £ ¥"},
    {"*", "† ‡"},
    {"1", "¹ ½ ¼"},
    {"2", "²"},
    {"3", "³ ¾"},
};

static const char * const digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};

/*Gboard's two symbol pages.*/
static const char * const symbols_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "@", "#", "$", "_", "&", "-", "+", "(", ")", "/", "\n",
    KEY_MORE, "*", "\"", "'", ":", ";", "!", "?", KEY_BACK, "\n",
    KEY_LETTERS, KEY_HIDE, ",", KEY_SPACE, ".", KEY_ENTER, "",
};

static const char * const more_map[] = {
    "~", "`", "|", "•", "√", "π", "÷", "×", "¶", "∆", "\n",
    "£", "¢", "€", "¥", "^", "°", "=", "{", "}", "\\", "\n",
    KEY_SYMBOLS, "%", "©", "®", "™", "✓", "[", "]", KEY_BACK, "\n",
    KEY_LETTERS, KEY_HIDE, ",", KEY_SPACE, ".", KEY_ENTER, "",
};

/*Both pages: ten keys, ten keys, seven between two wide ones, then the bottom row.*/
static const lv_buttonmatrix_ctrl_t symbols_ctrl[] = {
    CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2,
    CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2,
    CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2,
    CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2,
    CTRL_FUNC | 3, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2,
    CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_CHAR | 2, CTRL_BACK | 3,
    CTRL_FUNC | 3, CTRL_FUNC | 2, CTRL_FUNC | 2, CTRL_CHAR | 8, CTRL_FUNC | 2, CTRL_FUNC | 3,
};

/*A phone's number pad, with the hide key where Gboard has space.*/
static const char * const number_map[] = {
    "1", "2", "3", "-", "\n",
    "4", "5", "6", KEY_HIDE, "\n",
    "7", "8", "9", KEY_BACK, "\n",
    ",", "0", ".", KEY_ENTER, "",
};

static const lv_buttonmatrix_ctrl_t number_ctrl[] = {
    CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_FUNC | 2,
    CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_FUNC | 2,
    CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_BACK | 2,
    CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_CHAR | 3, CTRL_FUNC | 2,
};

static keymap_t lower;
static keymap_t upper;
static bool     built;

static uint32_t enabled = 1U << SETTINGS_KEYBOARD_EN;
static uint32_t current = SETTINGS_KEYBOARD_EN;

static live_t live[LIVE_MAX];

static shift_t  shift;
static bool     shift_automatic;   /**< Turned on by the start of a sentence, not a tap */
static bool     shift_tap_recent;
static uint32_t shift_tap_at;
static bool     space_recent;
static uint32_t space_at;

/** The finger on the keys. */
static struct {
    lv_obj_t * keyboard;   /**< NULL when none */
    uint32_t   id;         /**< Key it would type on letting go */
    lv_point_t start;
    int32_t    swipe_x;    /**< Where the cursor last moved, while sliding on space */
    bool       space;      /**< Went down on the space bar */
    bool       swiping;
    bool       slidable;   /**< Went down on a key that acts on release */
} press;

static lv_obj_t * bubble;

/** The alternatives pop-up, while a key is held. */
static struct {
    lv_obj_t * panel;   /**< NULL when closed */
    lv_obj_t * cells[ALT_MAX];
    char       texts[ALT_MAX][ALT_TEXT_BYTES];
    uint32_t   count;
    uint32_t   columns;
    uint32_t   selected;
    int32_t    cell_w;
    int32_t    cell_h;
} alt;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * ui_keyboard_create(lv_obj_t * parent)
{
    if(!built) layout_apply();

    lv_obj_t * keyboard = lv_keyboard_create(parent);

    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, key_clicked, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(keyboard, key_touched, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(keyboard, key_touched, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(keyboard, key_touched, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(keyboard, key_touched, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(keyboard, key_touched, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(keyboard, key_drawn, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_set_send_draw_task_events(keyboard, true);

    style_apply(keyboard);

    bool registered = false;
    for(uint32_t i = 0; i < LIVE_MAX && !registered; i++) {
        if(live[i].keyboard == NULL) {
            live[i].keyboard = keyboard;
            live[i].kind     = UI_KEYBOARD_TEXT;
            registered       = true;
        }
    }
    LV_ASSERT_MSG(registered, "More keyboards than LIVE_MAX");

    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, symbols_map, symbols_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, more_map, symbols_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_NUMBER, number_map, number_ctrl);
    maps_set(keyboard);

    return keyboard;
}

void ui_keyboard_attach(lv_obj_t * keyboard, lv_obj_t * textarea, ui_keyboard_kind_t kind)
{
    live_t * entry = live_find(keyboard);
    if(entry) entry->kind = kind;

    if(press.keyboard == keyboard) press_forget();
    space_recent = false;

    lv_keyboard_set_textarea(keyboard, textarea);
    lv_keyboard_set_mode(keyboard, kind == UI_KEYBOARD_NUMBER ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    shift_set(keyboard, SHIFT_OFF, false);
    shift_auto(keyboard);
}

void ui_keyboard_set_languages(uint32_t languages)
{
    languages |= 1U << SETTINGS_KEYBOARD_EN;
    languages &= (1U << SETTINGS_KEYBOARD_COUNT) - 1U;
    if(built && languages == enabled) return;

    enabled = languages;
    if(!(enabled & (1U << current))) current = SETTINGS_KEYBOARD_EN;
    layout_apply();
}

const char * ui_keyboard_language_name(settings_keyboard_t language)
{
    return (uint32_t)language < SETTINGS_KEYBOARD_COUNT ? layouts[language].name : "";
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Build both cases of the language in use, and give them to every keyboard. */
static void layout_apply(void)
{
    bool globe = enabled_count() > 1;

    letters_build(&lower, layouts[current].lower, globe);
    letters_build(&upper, layouts[current].upper, globe);
    built = true;

    bubble_hide();
    alt_close();
    press.id = LV_BUTTONMATRIX_BUTTON_NONE;

    /*The maps were rewritten in place: every keyboard has to take them again
     *before it next draws, or it would read them with the old key count.*/
    for(uint32_t i = 0; i < LIVE_MAX; i++) {
        if(live[i].keyboard) maps_set(live[i].keyboard);
    }
}

static void language_next(void)
{
    uint32_t next = current;
    do {
        next = (next + 1) % SETTINGS_KEYBOARD_COUNT;
    } while(!(enabled & (1U << next)));

    current = next;
    layout_apply();
}

/**
 * One case of the letters, as Gboard lays them out:
 *
 *         letters, numbers held on each
 *          letters, centred
 *   [shift]  letters  [backspace]
 *   [?123] [hide] [,] [globe] [ space ] [.] [enter]
 *
 * Every letter is one width, so a language's longest row sets the width of
 * the others: short rows are centred between hidden keys, and Shift and
 * Backspace share what the bottom letters leave. The globe is only there with
 * more than one language on offer.
 */
static void letters_build(keymap_t * km, const char * const rows[ROWS], bool globe)
{
    uint32_t count[ROWS];
    for(uint32_t r = 0; r < ROWS; r++) count[r] = letters_count(rows[r]);

    /*Shift and Backspace are a key and a half each.*/
    uint32_t columns = LV_MAX(LV_MAX(count[0], count[1]), count[2] + 3);
    uint32_t units   = columns * KEY_UNITS;

    build_t b = {km, 0, 0, 0};
    lv_memzero((void *)km->hints, sizeof(km->hints));

    for(uint32_t r = 0; r < 2; r++) {
        uint32_t pad = units - count[r] * KEY_UNITS;
        spacer_add(&b, pad / 2);
        letters_add(&b, rows[r], r == 0);
        spacer_add(&b, pad - pad / 2);
        km->map[b.entry++] = "\n";
    }

    uint32_t side = LV_MIN((units - count[2] * KEY_UNITS) / 2, UNITS_MAX);
    key_add(&b, KEY_SHIFT, CTRL_FUNC | side);
    letters_add(&b, rows[2], false);
    key_add(&b, KEY_BACK, CTRL_BACK | side);
    km->map[b.entry++] = "\n";

    uint32_t fixed = 3 + 2 + 2 + 2 + 3 + (globe ? 2 : 0);
    key_add(&b, KEY_SYMBOLS, CTRL_FUNC | 3);
    key_add(&b, KEY_HIDE, CTRL_FUNC | 2);
    key_add(&b, ",", CTRL_FUNC | 2);
    if(globe) key_add(&b, KEY_GLOBE, CTRL_FUNC | 2);
    key_add(&b, KEY_SPACE, CTRL_CHAR | LV_MIN(units - fixed, UNITS_MAX));
    key_add(&b, ".", CTRL_FUNC | 2);
    key_add(&b, KEY_ENTER, CTRL_FUNC | 3);

    LV_ASSERT(b.entry < MAP_MAX);
    km->map[b.entry] = "";
}

static void key_add(build_t * b, const char * text, lv_buttonmatrix_ctrl_t ctrl)
{
    LV_ASSERT(b->entry + 1 < MAP_MAX);
    b->km->map[b->entry++]   = text;
    b->km->ctrl[b->button++] = ctrl;
}

/** A hidden key, holding space. Hidden keys cannot be pressed, so its text is never typed. */
static void spacer_add(build_t * b, uint32_t units)
{
    if(units > 0) key_add(b, KEY_SPACE, CTRL_SPACER | LV_MIN(units, UNITS_MAX));
}

/** Add a row's letters as keys, their texts copied into the pool, numbered if asked. */
static void letters_add(build_t * b, const char * row, bool hints)
{
    uint32_t digit = 0;

    while(*row) {
        if(*row == ' ') {
            row++;
            continue;
        }

        size_t len = 0;
        while(row[len] != '\0' && row[len] != ' ') len++;

        LV_ASSERT(b->used + len + 1 <= POOL_BYTES);
        char * text = &b->km->pool[b->used];
        lv_memcpy(text, row, len);
        text[len] = '\0';
        b->used += len + 1;

        if(hints && digit < sizeof(digits) / sizeof(digits[0])) b->km->hints[b->button] = digits[digit++];
        key_add(b, text, CTRL_CHAR | KEY_UNITS);
        row += len;
    }
}

static uint32_t letters_count(const char * row)
{
    uint32_t count  = 0;
    bool     in_key = false;

    for(; *row; row++) {
        if(*row == ' ') in_key = false;
        else if(!in_key) {
            in_key = true;
            count++;
        }
    }
    return count;
}

static void maps_set(lv_obj_t * keyboard)
{
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, (const char * const *)lower.map, lower.ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, (const char * const *)upper.map, upper.ctrl);
}

/** Gboard's shades: on a dark theme the letter keys are the lighter ones, on
 *  a light theme white keys on grey. */
static void colors_get(lv_color_t * back, lv_color_t * key, lv_color_t * function)
{
    bool dark = lv_color_brightness(UI_COLOR_BG) < 128;

    *back     = dark ? UI_COLOR_BG : UI_COLOR_RAIL;
    *key      = dark ? UI_COLOR_BORDER : UI_COLOR_CARD;
    *function = dark ? UI_COLOR_CARD_ALT : UI_COLOR_BORDER;
}

static void style_apply(lv_obj_t * keyboard)
{
    lv_color_t back, key, function;
    colors_get(&back, &key, &function);

    lv_obj_set_style_bg_color(keyboard, back, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, KEYBOARD_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard, KEY_GAP_V, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, KEY_GAP_H, LV_PART_MAIN);

    lv_obj_set_style_bg_color(keyboard, key, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, KEY_RADIUS, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, UI_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, UI_FONT_LG, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, function, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, UI_COLOR_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_TRACK, LV_PART_ITEMS | LV_STATE_PRESSED);
}

static uint32_t enabled_count(void)
{
    uint32_t count = 0;
    for(uint32_t i = 0; i < SETTINGS_KEYBOARD_COUNT; i++) {
        if(enabled & (1U << i)) count++;
    }
    return count;
}

static live_t * live_find(const lv_obj_t * keyboard)
{
    for(uint32_t i = 0; i < LIVE_MAX; i++) {
        if(live[i].keyboard == keyboard) return &live[i];
    }
    return NULL;
}

/*=====================
 * Keys
 *====================*/

static void key_clicked(lv_event_t * e)
{
    lv_obj_t * keyboard = lv_event_get_current_target_obj(e);
    uint32_t   id       = lv_buttonmatrix_get_selected_button(keyboard);

    const char * text = lv_buttonmatrix_get_button_text(keyboard, id);
    if(text == NULL) return;

    bool space = strcmp(text, KEY_SPACE) == 0;
    /*The finger slid along the space bar to move the cursor: no space.*/
    if(space && press.keyboard == keyboard && press.swiping) return;

    if(strcmp(text, KEY_SHIFT) == 0)         shift_tapped(keyboard);
    else if(strcmp(text, KEY_SYMBOLS) == 0)  page_set(keyboard, LV_KEYBOARD_MODE_SPECIAL);
    else if(strcmp(text, KEY_MORE) == 0)     page_set(keyboard, LV_KEYBOARD_MODE_USER_1);
    else if(strcmp(text, KEY_LETTERS) == 0)  page_set(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    else if(strcmp(text, KEY_GLOBE) == 0)    language_next();
    else if(strcmp(text, KEY_HIDE) == 0)     action_send(keyboard, LV_EVENT_CANCEL);
    else if(strcmp(text, KEY_ENTER) == 0) {
        lv_obj_t * textarea = lv_keyboard_get_textarea(keyboard);
        if(textarea && !lv_textarea_get_one_line(textarea)) type_text(keyboard, "\n");
        else action_send(keyboard, LV_EVENT_READY);
    }
    else if(strcmp(text, KEY_BACK) == 0) {
        lv_obj_t * textarea = lv_keyboard_get_textarea(keyboard);
        if(textarea) lv_textarea_delete_char(textarea);
        space_recent = false;
        shift_auto(keyboard);
    }
    else type_text(keyboard, text);
}

/** Type a key's text, then settle Shift and the page as Gboard does. */
static void type_text(lv_obj_t * keyboard, const char * text)
{
    lv_obj_t * textarea = lv_keyboard_get_textarea(keyboard);
    live_t *   entry    = live_find(keyboard);
    bool       sentence = entry && entry->kind == UI_KEYBOARD_SENTENCE;
    bool       space    = strcmp(text, KEY_SPACE) == 0;
    bool       stop     = false;

    /*A second space straight after a word and a space: a full stop.*/
    if(space && sentence && space_recent && lv_tick_elaps(space_at) < DOUBLE_SPACE_MS && textarea &&
       after_word_and_space(textarea)) {
        stop = true;
    }

    if(textarea) {
        if(stop) {
            lv_textarea_delete_char(textarea);
            lv_textarea_add_text(textarea, ". ");
        }
        else lv_textarea_add_text(textarea, text);
    }

    space_recent = space && !stop;
    space_at     = lv_tick_get();

    lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
    if(mode == LV_KEYBOARD_MODE_SPECIAL || mode == LV_KEYBOARD_MODE_USER_1) {
        if(space) page_set(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        return;
    }

    if(shift == SHIFT_ONCE) shift_set(keyboard, SHIFT_OFF, false);
    shift_auto(keyboard);
}

/** READY or CANCEL, to the keyboard and then its text area -- as LVGL's handler sends them. */
static void action_send(lv_obj_t * keyboard, lv_event_code_t code)
{
    if(lv_obj_send_event(keyboard, code, NULL) != LV_RESULT_OK) return;

    lv_obj_t * textarea = lv_keyboard_get_textarea(keyboard);
    if(textarea) lv_obj_send_event(textarea, code, NULL);
}

static void page_set(lv_obj_t * keyboard, lv_keyboard_mode_t mode)
{
    lv_keyboard_set_mode(keyboard, mode);
    space_recent = false;

    if(mode == LV_KEYBOARD_MODE_TEXT_LOWER) {
        shift_set(keyboard, SHIFT_OFF, false);
        shift_auto(keyboard);
    }
}

static bool letters_showing(lv_obj_t * keyboard)
{
    lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
    return mode == LV_KEYBOARD_MODE_TEXT_LOWER || mode == LV_KEYBOARD_MODE_TEXT_UPPER;
}

/** Off to once; once to locked if tapped again quickly, otherwise off; locked to off. */
static void shift_tapped(lv_obj_t * keyboard)
{
    bool    again = shift_tap_recent && lv_tick_elaps(shift_tap_at) < SHIFT_LOCK_MS;
    shift_t next;

    if(shift == SHIFT_LOCKED)    next = SHIFT_OFF;
    else if(shift == SHIFT_ONCE) next = again ? SHIFT_LOCKED : SHIFT_OFF;
    else                         next = SHIFT_ONCE;

    shift_tap_recent = next == SHIFT_ONCE;
    shift_tap_at     = lv_tick_get();
    shift_set(keyboard, next, false);
}

static void shift_set(lv_obj_t * keyboard, shift_t state, bool automatic)
{
    shift           = state;
    shift_automatic = automatic;

    if(letters_showing(keyboard)) {
        lv_keyboard_set_mode(keyboard, state == SHIFT_OFF ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_TEXT_UPPER);
    }
    /*Once and locked share a map; the key shows which.*/
    lv_obj_invalidate(keyboard);
}

/** In sentences: Shift on where one starts, and off again if the cursor leaves. */
static void shift_auto(lv_obj_t * keyboard)
{
    live_t * entry = live_find(keyboard);
    if(!entry || entry->kind != UI_KEYBOARD_SENTENCE || !letters_showing(keyboard)) return;
    if(shift == SHIFT_LOCKED) return;

    lv_obj_t * textarea = lv_keyboard_get_textarea(keyboard);
    bool       start    = textarea && at_sentence_start(textarea);

    if(start && shift == SHIFT_OFF) shift_set(keyboard, SHIFT_ONCE, true);
    else if(!start && shift == SHIFT_ONCE && shift_automatic) shift_set(keyboard, SHIFT_OFF, false);
}

/** The cursor at the start, a new line, or spaces after . ! or ? */
static bool at_sentence_start(lv_obj_t * textarea)
{
    const char * text;
    size_t       at     = cursor_byte(textarea, &text);
    bool         spaces = false;

    while(at > 0 && text[at - 1] == ' ') {
        at--;
        spaces = true;
    }

    if(at == 0 || text[at - 1] == '\n') return true;
    return spaces && (text[at - 1] == '.' || text[at - 1] == '!' || text[at - 1] == '?');
}

/** A space just before the cursor, and before it a letter or digit. Bytes will do: spaces and
 *  punctuation are single bytes, and no byte of a longer character is one of them. */
static bool after_word_and_space(lv_obj_t * textarea)
{
    const char * text;
    size_t       at = cursor_byte(textarea, &text);

    if(at < 2 || text[at - 1] != ' ') return false;

    char before = text[at - 2];
    return before != ' ' && before != '\n' && strchr(".,;:!?", before) == NULL;
}

/** Where the cursor is, in bytes: the text area counts it in characters. */
static size_t cursor_byte(lv_obj_t * textarea, const char ** text)
{
    const char * t   = lv_textarea_get_text(textarea);
    uint32_t     pos = lv_textarea_get_cursor_pos(textarea);
    size_t       at  = 0;

    for(uint32_t i = 0; i < pos && t[at] != '\0'; i++) at += utf8_length(&t[at]);

    *text = t;
    return at;
}

/*=====================
 * The finger
 *====================*/

/* These run after the button matrix's own handling of the same event, so its
 * selected key is already up to date -- and, on release, the key it typed
 * has already gone through key_clicked(). */
static void key_touched(lv_event_t * e)
{
    lv_obj_t * keyboard = lv_event_get_current_target_obj(e);

    switch(lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:      press_start(keyboard); break;
        case LV_EVENT_PRESSING:     press_move(keyboard); break;
        case LV_EVENT_LONG_PRESSED: press_hold(keyboard); break;
        case LV_EVENT_RELEASED:     press_end(keyboard, true); break;
        case LV_EVENT_PRESS_LOST:   press_end(keyboard, false); break;
        default: break;
    }
}

static void press_start(lv_obj_t * keyboard)
{
    press_forget();

    press.keyboard = keyboard;
    press.id       = lv_buttonmatrix_get_selected_button(keyboard);
    if(!pointer_get(&press.start)) return;

    const char * text = lv_buttonmatrix_get_button_text(keyboard, press.id);
    press.space    = text && strcmp(text, KEY_SPACE) == 0;
    press.slidable = press.id == LV_BUTTONMATRIX_BUTTON_NONE ||
                     lv_buttonmatrix_has_button_ctrl(keyboard, press.id, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);

    bubble_show(keyboard, press.id);
}

static void press_move(lv_obj_t * keyboard)
{
    lv_point_t point;
    if(press.keyboard != keyboard || !pointer_get(&point)) return;

    if(alt.panel) {
        alt_select(&point);
        return;
    }

    /*Along the space bar, the cursor follows the finger.*/
    if(press.space) {
        if(!press.swiping && LV_ABS(point.x - press.start.x) >= SWIPE_START_PX) {
            press.swiping = true;
            press.swipe_x = press.start.x;
        }
        if(!press.swiping) return;

        lv_obj_t * textarea = lv_keyboard_get_textarea(keyboard);
        while(point.x - press.swipe_x >= SWIPE_STEP_PX) {
            if(textarea) lv_textarea_cursor_right(textarea);
            press.swipe_x += SWIPE_STEP_PX;
        }
        while(press.swipe_x - point.x >= SWIPE_STEP_PX) {
            if(textarea) lv_textarea_cursor_left(textarea);
            press.swipe_x -= SWIPE_STEP_PX;
        }
        shift_auto(keyboard);
        return;
    }

    if(!press.slidable) return;

    /*Off the keyboard, letting go types nothing.*/
    lv_area_t coords;
    lv_obj_get_coords(keyboard, &coords);
    if(!lv_area_is_point_on(&coords, &point, 0)) {
        press.id = LV_BUTTONMATRIX_BUTTON_NONE;
        lv_buttonmatrix_set_selected_button(keyboard, LV_BUTTONMATRIX_BUTTON_NONE);
        bubble_hide();
        return;
    }

    /*Onto another key that acts on release: that one is typed. The button
     *matrix itself drops a key the finger leaves, so between keys, or over
     *Backspace, the last key is selected again.*/
    uint32_t over = key_at(keyboard, &point);
    if(over != LV_BUTTONMATRIX_BUTTON_NONE && over != press.id &&
       lv_buttonmatrix_has_button_ctrl(keyboard, over, LV_BUTTONMATRIX_CTRL_CLICK_TRIG)) {
        press.id = over;
        bubble_show(keyboard, over);
    }

    if(press.id != LV_BUTTONMATRIX_BUTTON_NONE && lv_buttonmatrix_get_selected_button(keyboard) != press.id) {
        lv_buttonmatrix_set_selected_button(keyboard, press.id);
    }
}

static void press_hold(lv_obj_t * keyboard)
{
    if(press.keyboard != keyboard || press.swiping || press.id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    if(alt_collect(keyboard, press.id) == 0) return;

    /*The button matrix is left without a key, so letting go types only what
     *the finger picks from the pop-up.*/
    lv_buttonmatrix_set_selected_button(keyboard, LV_BUTTONMATRIX_BUTTON_NONE);
    bubble_hide();
    alt_open(keyboard, press.id);
}

static void press_end(lv_obj_t * keyboard, bool released)
{
    if(press.keyboard != keyboard) return;

    char chosen[ALT_TEXT_BYTES] = "";
    if(alt.panel && released && alt.selected < alt.count) lv_strlcpy(chosen, alt.texts[alt.selected], sizeof(chosen));

    press_forget();
    if(chosen[0]) type_text(keyboard, chosen);
}

static void press_forget(void)
{
    bubble_hide();
    alt_close();
    lv_memzero(&press, sizeof(press));
    press.id = LV_BUTTONMATRIX_BUTTON_NONE;
}

static bool pointer_get(lv_point_t * point)
{
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return false;

    lv_indev_get_point(indev, point);
    return true;
}

/** A key's area on the screen. */
static bool key_area(lv_obj_t * keyboard, uint32_t id, lv_area_t * area)
{
    lv_buttonmatrix_t * btnm = (lv_buttonmatrix_t *)keyboard;
    if(id >= btnm->btn_cnt) return false;

    lv_area_t coords;
    lv_obj_get_coords(keyboard, &coords);
    *area = btnm->button_areas[id];
    lv_area_move(area, coords.x1, coords.y1);
    return true;
}

static uint32_t key_at(lv_obj_t * keyboard, const lv_point_t * point)
{
    lv_buttonmatrix_t * btnm = (lv_buttonmatrix_t *)keyboard;
    lv_area_t           area;

    for(uint32_t i = 0; i < btnm->btn_cnt; i++) {
        if(btnm->ctrl_bits[i] & LV_BUTTONMATRIX_CTRL_HIDDEN) continue;
        if(key_area(keyboard, i, &area) && lv_area_is_point_on(&area, point, 0)) return i;
    }
    return LV_BUTTONMATRIX_BUTTON_NONE;
}

static bool key_is_function(const char * text)
{
    static const char * const keys[] = {
        KEY_SHIFT, KEY_GLOBE, KEY_SYMBOLS, KEY_MORE, KEY_LETTERS, KEY_HIDE, KEY_ENTER, KEY_BACK, KEY_SPACE,
    };

    for(uint32_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if(strcmp(text, keys[i]) == 0) return true;
    }
    return false;
}

/** The number on a top-row letter, while the letters are showing. */
static const char * key_hint(lv_obj_t * keyboard, uint32_t id)
{
    if(id >= MAP_MAX) return NULL;

    const char * const * map = lv_buttonmatrix_get_map(keyboard);
    if(map == (const char * const *)lower.map) return lower.hints[id];
    if(map == (const char * const *)upper.map) return upper.hints[id];
    return NULL;
}

/*=====================
 * Preview
 *====================*/

/** The key, larger, above the finger. Not for keys that do something. */
static void bubble_show(lv_obj_t * keyboard, uint32_t id)
{
    const char * text = lv_buttonmatrix_get_button_text(keyboard, id);
    lv_area_t    key;

    if(text == NULL || key_is_function(text) || !key_area(keyboard, id, &key)) {
        bubble_hide();
        return;
    }

    if(bubble == NULL) {
        bubble = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(bubble);
        lv_obj_set_clickable(bubble, false);
        lv_obj_add_event_cb(bubble, bubble_deleted, LV_EVENT_DELETE, NULL);
        lv_obj_center(lv_label_create(bubble));
    }

    lv_color_t back, key_color, function;
    colors_get(&back, &key_color, &function);

    int32_t key_w = lv_area_get_width(&key);
    int32_t key_h = lv_area_get_height(&key);
    int32_t w     = key_w + BUBBLE_GROW_W;
    int32_t h     = key_h + BUBBLE_GROW_H;
    int32_t x     = key.x1 + key_w / 2 - w / 2;
    /*Over the top third of the key, like Gboard's.*/
    int32_t y     = key.y1 + key_h / 3 - h;

    x = LV_CLAMP(0, x, lv_display_get_horizontal_resolution(NULL) - w);
    y = LV_MAX(0, y);

    lv_obj_set_style_bg_color(bubble, key_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bubble, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_border_width(bubble, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bubble, KEY_RADIUS + 4, LV_PART_MAIN);
    lv_obj_set_pos(bubble, x, y);
    lv_obj_set_size(bubble, w, h);

    lv_obj_t * label = lv_obj_get_child(bubble, 0);
    lv_obj_set_style_text_font(label, UI_FONT_XL, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_label_set_text(label, text);

    lv_obj_set_hidden(bubble, false);
    lv_obj_move_foreground(bubble);
}

static void bubble_hide(void)
{
    if(bubble) lv_obj_set_hidden(bubble, true);
}

static void bubble_deleted(lv_event_t * e)
{
    LV_UNUSED(e);
    bubble = NULL;
}

/*=====================
 * Alternatives
 *====================*/

/** Gather what holding the key offers into `alt.texts`: its number, then the table's. */
static uint32_t alt_collect(lv_obj_t * keyboard, uint32_t id)
{
    alt.count = 0;

    const char * hint = key_hint(keyboard, id);
    if(hint) lv_strlcpy(alt.texts[alt.count++], hint, ALT_TEXT_BYTES);

    const char * text = lv_buttonmatrix_get_button_text(keyboard, id);
    if(text == NULL) return alt.count;

    for(uint32_t i = 0; i < sizeof(alternatives) / sizeof(alternatives[0]); i++) {
        if(strcmp(text, alternatives[i].key) != 0) continue;

        const char * p = alternatives[i].alternatives;
        while(*p && alt.count < ALT_MAX) {
            if(*p == ' ') {
                p++;
                continue;
            }

            size_t len = 0;
            while(p[len] != '\0' && p[len] != ' ') len++;

            if(len < ALT_TEXT_BYTES) {
                lv_memcpy(alt.texts[alt.count], p, len);
                alt.texts[alt.count][len] = '\0';
                alt.count++;
            }
            p += len;
        }
        break;
    }

    return alt.count;
}

/**
 * The pop-up: a row of cells above the key -- more rows for a long list, the
 * first row nearest the key -- with the first cell, picked to begin with,
 * right above it.
 */
static void alt_open(lv_obj_t * keyboard, uint32_t id)
{
    lv_area_t key;
    if(alt.count == 0 || !key_area(keyboard, id, &key)) return;

    lv_color_t back, key_color, function;
    colors_get(&back, &key_color, &function);

    int32_t  key_w = lv_area_get_width(&key);
    uint32_t rows;

    alt.cell_w  = LV_MIN(key_w, ALT_CELL_WIDTH);
    alt.cell_h  = lv_area_get_height(&key);
    alt.columns = LV_MIN(alt.count, ALT_COLUMNS);
    rows        = (alt.count + alt.columns - 1) / alt.columns;

    int32_t w = (int32_t)alt.columns * alt.cell_w + 2 * ALT_PAD;
    int32_t h = (int32_t)rows * alt.cell_h + 2 * ALT_PAD;
    int32_t x = key.x1 + key_w / 2 - alt.cell_w / 2 - ALT_PAD;
    int32_t y = key.y1 - h - ALT_PAD;

    x = LV_CLAMP(0, x, lv_display_get_horizontal_resolution(NULL) - w);
    y = LV_MAX(0, y);

    alt.panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(alt.panel);
    lv_obj_set_clickable(alt.panel, false);
    lv_obj_set_scrollable(alt.panel, false);
    lv_obj_set_pos(alt.panel, x, y);
    lv_obj_set_size(alt.panel, w, h);
    lv_obj_set_style_bg_color(alt.panel, key_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alt.panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(alt.panel, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_border_width(alt.panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(alt.panel, KEY_RADIUS + 4, LV_PART_MAIN);

    for(uint32_t i = 0; i < alt.count; i++) {
        uint32_t row  = i / alt.columns;
        lv_obj_t * cell = lv_obj_create(alt.panel);
        lv_obj_remove_style_all(cell);
        lv_obj_set_clickable(cell, false);
        lv_obj_set_pos(cell, ALT_PAD + (int32_t)(i % alt.columns) * alt.cell_w,
                       ALT_PAD + (int32_t)(rows - 1 - row) * alt.cell_h);
        lv_obj_set_size(cell, alt.cell_w, alt.cell_h);
        lv_obj_set_style_radius(cell, KEY_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_bg_color(cell, UI_COLOR_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);

        lv_obj_t * label = lv_label_create(cell);
        lv_label_set_text(label, alt.texts[i]);
        lv_obj_set_style_text_font(label, UI_FONT_LG, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_center(label);

        alt.cells[i] = cell;
    }

    alt.selected = UINT32_MAX;
    alt_highlight(0);
}

/** The cell under the finger; beyond the pop-up, the nearest. */
static void alt_select(const lv_point_t * point)
{
    lv_area_t panel;
    lv_obj_get_coords(alt.panel, &panel);

    int32_t rows = (int32_t)((alt.count + alt.columns - 1) / alt.columns);
    int32_t col  = LV_CLAMP(0, (point->x - panel.x1 - ALT_PAD) / alt.cell_w, (int32_t)alt.columns - 1);
    int32_t row  = LV_CLAMP(0, (point->y - panel.y1 - ALT_PAD) / alt.cell_h, rows - 1);

    /*Rows are numbered from the bottom.*/
    uint32_t index = (uint32_t)(rows - 1 - row) * alt.columns + (uint32_t)col;
    alt_highlight(LV_MIN(index, alt.count - 1));
}

static void alt_highlight(uint32_t index)
{
    if(index == alt.selected) return;

    if(alt.selected < alt.count) {
        lv_obj_set_style_bg_opa(alt.cells[alt.selected], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(alt.cells[alt.selected], 0), UI_COLOR_TEXT, LV_PART_MAIN);
    }

    alt.selected = index;
    lv_obj_set_style_bg_opa(alt.cells[index], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(alt.cells[index], 0), lv_color_white(), LV_PART_MAIN);
}

static void alt_close(void)
{
    if(alt.panel) lv_obj_delete(alt.panel);
    lv_memzero(&alt, sizeof(alt));
    alt.selected = UINT32_MAX;
}

/*=====================
 * Drawing
 *====================*/

/**
 * Restyle keys as the button matrix draws them. Beside a key's background:
 * the number hint, and the language on the space bar. For function keys, the
 * button matrix's label -- in the letters' font -- is hidden and drawn again:
 * words smaller, icons between, Shift in the accent colour while on, with a
 * bar under it while locked, and Enter white on the accent colour, a tick
 * for a one-line field.
 */
static void key_drawn(lv_event_t * e)
{
    lv_obj_t *           keyboard = lv_event_get_current_target_obj(e);
    lv_draw_task_t *     task     = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t * base     = lv_draw_task_get_draw_dsc(task);
    if(base->part != LV_PART_ITEMS) return;

    uint32_t     id   = base->id1;
    const char * text = lv_buttonmatrix_get_button_text(keyboard, id);
    lv_area_t    key;
    if(text == NULL || !key_area(keyboard, id, &key)) return;

    bool                enter = strcmp(text, KEY_ENTER) == 0;
    lv_draw_task_type_t type  = lv_draw_task_get_type(task);

    if(type == LV_DRAW_TASK_TYPE_FILL) {
        if(enter) {
            lv_draw_fill_dsc_t * fill    = lv_draw_task_get_fill_dsc(task);
            bool                 pressed = lv_buttonmatrix_get_selected_button(keyboard) == id &&
                                           lv_obj_has_state(keyboard, LV_STATE_PRESSED);
            fill->color = pressed ? lv_color_darken(UI_COLOR_ACCENT, LV_OPA_20) : UI_COLOR_ACCENT;
        }

        const char * hint = key_hint(keyboard, id);
        if(hint) {
            lv_area_t corner = key;
            corner.x2 -= HINT_INSET_X;
            corner.y1 += HINT_INSET_Y;
            corner.y2  = corner.y1 + lv_font_get_line_height(UI_FONT_XS);
            label_draw(base->layer, &corner, hint, UI_FONT_XS, UI_COLOR_TEXT_DIM, LV_TEXT_ALIGN_RIGHT);
        }

        if(strcmp(text, KEY_SPACE) == 0 && letters_showing(keyboard)) {
            lv_area_t line;
            line_area(&key, UI_FONT_SM, &line);
            label_draw(base->layer, &line, layouts[current].name, UI_FONT_SM, UI_COLOR_TEXT_DIM, LV_TEXT_ALIGN_CENTER);
        }
        return;
    }

    if(type != LV_DRAW_TASK_TYPE_LABEL || !key_is_function(text) || strcmp(text, KEY_SPACE) == 0) return;

    lv_draw_task_get_label_dsc(task)->opa = LV_OPA_TRANSP;

    const char *      glyph = text;
    const lv_font_t * font  = UI_FONT_MD;
    lv_color_t        color = UI_COLOR_TEXT;
    bool              shift_key = strcmp(text, KEY_SHIFT) == 0;

    if(strcmp(text, KEY_SYMBOLS) == 0 || strcmp(text, KEY_MORE) == 0 || strcmp(text, KEY_LETTERS) == 0) {
        font = UI_FONT_SM;
    }
    else if(enter) {
        lv_obj_t * textarea = lv_keyboard_get_textarea(keyboard);
        glyph = textarea == NULL || lv_textarea_get_one_line(textarea) ? LV_SYMBOL_OK : LV_SYMBOL_NEW_LINE;
        color = lv_color_white();
    }
    else if(shift_key && shift != SHIFT_OFF) {
        color = UI_COLOR_ACCENT;
    }

    lv_area_t line;
    line_area(&key, font, &line);
    label_draw(base->layer, &line, glyph, font, color, LV_TEXT_ALIGN_CENTER);

    if(shift_key && shift == SHIFT_LOCKED) {
        lv_draw_rect_dsc_t bar;
        lv_draw_rect_dsc_init(&bar);
        bar.bg_color = UI_COLOR_ACCENT;
        bar.radius   = 1;

        int32_t   middle = key.x1 + lv_area_get_width(&key) / 2;
        lv_area_t area   = {middle - 9, line.y2 + 2, middle + 9, line.y2 + 4};
        lv_draw_rect(base->layer, &bar, &area);
    }
}

static void keyboard_deleted(lv_event_t * e)
{
    lv_obj_t * keyboard = lv_event_get_current_target_obj(e);

    if(press.keyboard == keyboard) press_forget();

    live_t * entry = live_find(keyboard);
    if(entry) lv_memzero(entry, sizeof(*entry));
}

/** One line of `font`, centred up and down in `key`. */
static void line_area(const lv_area_t * key, const lv_font_t * font, lv_area_t * line)
{
    int32_t height = lv_font_get_line_height(font);

    *line    = *key;
    line->y1 = key->y1 + (lv_area_get_height(key) - height) / 2;
    line->y2 = line->y1 + height - 1;
}

static void label_draw(lv_layer_t * layer, const lv_area_t * area, const char * text, const lv_font_t * font,
                       lv_color_t color, lv_text_align_t align)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text  = text;
    dsc.font  = font;
    dsc.color = color;
    dsc.align = align;
    lv_draw_label(layer, &dsc, area);
}

/** Bytes in the UTF-8 character `text` starts with. */
static size_t utf8_length(const char * text)
{
    uint8_t lead = (uint8_t)text[0];

    if(lead < 0x80) return 1;
    if(lead < 0xE0) return 2;
    if(lead < 0xF0) return 3;
    return 4;
}
