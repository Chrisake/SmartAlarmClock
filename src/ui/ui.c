/**
 * @file ui.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui.h"
#include "ui/ui_air_feed.h"
#include "ui/ui_alarm_feed.h"
#include "ui/ui_clock_feed.h"
#include "ui/ui_devices_feed.h"
#include "ui/ui_radio_feed.h"
#include "ui/ui_settings_feed.h"
#include "ui/ui_weather_feed.h"
#include "ui/ui_presence_feed.h"
#include "ui/ui_page.h"
#include "ui/ui_theme.h"

#include "ui/pages/clock/page_clock.h"
#include "ui/pages/alarms/page_alarms.h"
#include "ui/pages/weather/page_weather.h"
#include "ui/pages/air_quality/page_air_quality.h"
#include "ui/pages/radio/page_radio.h"
#include "ui/pages/smart_home/page_smart_home.h"
#include "ui/pages/settings/page_settings.h"

/*********************
 *      DEFINES
 *********************/

/** How often the idle timeout is checked. */
#define UI_IDLE_POLL_MS 1000

/** Length of the chrome fade, matched to the clock page's own transition. */
#define UI_CHROME_FADE_MS 450

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void        shell_build(void);
static void        nav_rail_create(lv_obj_t * parent);
static lv_obj_t *  nav_button_create(lv_obj_t * parent, const ui_page_t * page, ui_page_id_t id);
static void        nav_button_clicked(lv_event_t * e);
static void        idle_timer_cb(lv_timer_t * timer);
static void        chrome_opa_set(void * obj, int32_t value);
static void        chrome_show_now(void);
static void        screen_off_set(bool off);
static void        screen_off_pressed(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t * pages[UI_PAGE_COUNT];

/** Root object of each page, in the content area. */
static lv_obj_t * page_roots[UI_PAGE_COUNT];

/** Navigation rail button for each page. */
static lv_obj_t * nav_buttons[UI_PAGE_COUNT];

static lv_obj_t *   content;
static lv_obj_t *   rail;
static ui_page_id_t current = UI_PAGE_CLOCK;

/** Untouched time before the ambient clock face; 0 = never. Set from settings. */
static uint32_t idle_timeout_ms = 4 * 60 * 1000;

/** Idle is the ambient face; without, the screen goes off. Set from settings. */
static bool always_on = true;

/** Over everything while the screen is off; NULL while it is on. */
static lv_obj_t * screen_off_cover;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_init(void)
{
    pages[UI_PAGE_CLOCK]       = page_clock_desc();
    pages[UI_PAGE_ALARMS]      = page_alarms_desc();
    pages[UI_PAGE_WEATHER]     = page_weather_desc();
    pages[UI_PAGE_AIR_QUALITY] = page_air_quality_desc();
    pages[UI_PAGE_RADIO]       = page_radio_desc();
    pages[UI_PAGE_SMART_HOME]  = page_smart_home_desc();
    pages[UI_PAGE_SETTINGS]    = page_settings_desc();

    /*Settings first: they pick the palette everything is built in.*/
    ui_settings_feed_load();

    shell_build();

    /*Show the home page. Force the transition so on_show() fires.*/
    current = UI_PAGE_COUNT;
    ui_navigate(UI_PAGE_CLOCK);

    /*Falls back to the ambient clock face when the panel goes untouched.
     *lv_display_get_inactive_time() covers every input device, so this also
     *catches interaction on the other pages.*/
    lv_timer_create(idle_timer_cb, UI_IDLE_POLL_MS, NULL);

    /*Everything that changes on its own as time passes -- the clock itself and
     *which alarm rings next -- is driven from here.*/
    ui_clock_feed_init();

    /*The alarms kept, and rung when they are due.*/
    ui_alarm_feed_init();

    /*The devices page's configuration, state and (in the simulator) broker.*/
    ui_devices_feed_init();

    /*The saved radio stations, the station directory and the stream player.
     *The alarm editor's sound menu gets the stations from here too.*/
    ui_radio_feed_init();

    /*The forecasts, and the air quality from the sensors and the forecast.
     *Before the settings feed, which brings up the broker the readings go to.*/
    ui_weather_feed_init();
    ui_air_feed_init();

    /*Applies the rest of the settings and answers the settings page.*/
    ui_settings_feed_init();

    /*Watches for a face while idle, when face wake is on.*/
    ui_presence_feed_init();
}

void ui_rebuild(void)
{
    ui_page_id_t showing     = current;
    bool         was_ambient = page_clock_is_ambient();

    /*Every page's on_hide first, so anything a page put on the top layer --
     *a popup, a panel, a keyboard -- goes with it.*/
    for(int i = 0; i < UI_PAGE_COUNT; i++) {
        if(pages[i]->on_hide) pages[i]->on_hide();
    }

    /*The alarms page seeds its list when it is built. Keep the user's.*/
    uint32_t             count  = 0;
    const page_alarm_t * alarms = page_alarms_get_alarms(&count);
    page_alarm_t *       saved  = count ? lv_malloc(count * sizeof(page_alarm_t)) : NULL;
    if(saved) lv_memcpy(saved, alarms, count * sizeof(page_alarm_t));

    lv_obj_clean(lv_screen_active());
    shell_build();

    if(saved) {
        page_alarms_set_alarms(saved, count);
        lv_free(saved);
    }

    current = UI_PAGE_COUNT;
    ui_navigate(showing);
    if(showing == UI_PAGE_CLOCK) page_clock_set_ambient(was_ambient);

    /*The new pages show placeholders until told otherwise.*/
    ui_clock_feed_refresh();
    ui_devices_feed_republish();
    ui_radio_feed_republish();
    ui_weather_feed_republish();
    ui_air_feed_republish();
    ui_settings_feed_republish();
}

void ui_set_idle_timeout(uint32_t ms)
{
    idle_timeout_ms = ms;
}

void ui_set_always_on(bool on)
{
    always_on = on;
    if(on) screen_off_set(false);
}

void ui_wake(void)
{
    lv_display_trigger_activity(lv_display_get_default());
    screen_off_set(false);
    if(page_clock_is_ambient()) page_clock_set_ambient(false);
}

bool ui_is_screen_off(void)
{
    return screen_off_cover != NULL;
}

bool ui_is_idle(void)
{
    return screen_off_cover != NULL || page_clock_is_ambient();
}

void ui_set_chrome_hidden(bool hidden)
{
    lv_opa_t target = hidden ? LV_OPA_TRANSP : LV_OPA_COVER;

    if(lv_obj_get_style_opa(rail, LV_PART_MAIN) == target) return;

    /*Stop the faded-out rail from swallowing the touch that wakes the page.*/
    lv_obj_set_clickable(rail, !hidden);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, rail);
    lv_anim_set_exec_cb(&a, chrome_opa_set);
    lv_anim_set_duration(&a, UI_CHROME_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_values(&a, lv_obj_get_style_opa(rail, LV_PART_MAIN), target);
    lv_anim_start(&a);
}

void ui_navigate(ui_page_id_t id)
{
    if(id >= UI_PAGE_COUNT || id == current) return;

    if(current < UI_PAGE_COUNT) {
        lv_obj_set_hidden(page_roots[current], true);
        lv_obj_remove_state(nav_buttons[current], LV_STATE_CHECKED);
        if(pages[current]->on_hide) pages[current]->on_hide();
    }

    current = id;

    /*Only the clock's ambient face hides the rail, and every other page needs
     *it to get anywhere. After a rebuild the new clock page has just started
     *hiding it, so make sure it is back, and at once.*/
    if(id != UI_PAGE_CLOCK) chrome_show_now();

    lv_obj_set_hidden(page_roots[current], false);
    lv_obj_add_state(nav_buttons[current], LV_STATE_CHECKED);
    if(pages[current]->on_show) pages[current]->on_show();
}

ui_page_id_t ui_current_page(void)
{
    return current;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** The screen's contents: rail on the left, every page built into the rest. */
static void shell_build(void)
{
    lv_obj_t * screen = lv_screen_active();
    ui_theme_apply(screen);

    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(screen, 0, LV_PART_MAIN);

    nav_rail_create(screen);

    content = lv_obj_create(screen);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(content, false);

    /*Build every page up front. At 1024x600 with 32 MB of PSRAM this is
     *cheap, and it keeps navigation instant. Pages that need to poll should
     *do so from on_show()/on_hide() rather than running all the time.*/
    for(int i = 0; i < UI_PAGE_COUNT; i++) {
        page_roots[i] = pages[i]->create(content);
        lv_obj_set_hidden(page_roots[i], true);
    }
}

static void nav_rail_create(lv_obj_t * parent)
{
    rail = lv_obj_create(parent);

    lv_obj_set_size(rail, UI_RAIL_WIDTH, LV_PCT(100));
    lv_obj_set_style_bg_color(rail, UI_COLOR_RAIL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rail, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rail, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rail, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(rail, UI_GAP / 2, LV_PART_MAIN);
    lv_obj_set_scrollable(rail, false);

    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for(int i = 0; i < UI_PAGE_COUNT; i++) {
        nav_buttons[i] = nav_button_create(rail, pages[i], (ui_page_id_t)i);
    }
}

static lv_obj_t * nav_button_create(lv_obj_t * parent, const ui_page_t * page, ui_page_id_t id)
{
    lv_obj_t * btn = lv_button_create(parent);

    lv_obj_set_size(btn, LV_PCT(100), 78);
    lv_obj_set_style_radius(btn, UI_RADIUS - 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(btn, 2, LV_PART_MAIN);

    /*Unselected: blends into the rail. Selected: accent fill.*/
    lv_obj_set_style_bg_color(btn, UI_COLOR_RAIL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon = lv_label_create(btn);
    lv_label_set_text(icon, page->icon);
    lv_obj_set_style_text_font(icon, UI_FONT_ICON, LV_PART_MAIN);

    lv_obj_t * title = lv_label_create(btn);
    lv_label_set_text(title, page->title);
    lv_obj_set_style_text_font(title, UI_FONT_XS, LV_PART_MAIN);

    lv_obj_add_event_cb(btn, nav_button_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)id);

    return btn;
}

static void nav_button_clicked(lv_event_t * e)
{
    ui_navigate((ui_page_id_t)(lv_uintptr_t)lv_event_get_user_data(e));
}

static void chrome_opa_set(void * obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, LV_PART_MAIN);
}

static void chrome_show_now(void)
{
    lv_anim_delete(rail, chrome_opa_set);
    lv_obj_set_style_opa(rail, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_clickable(rail, true);
}

static void idle_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    if(idle_timeout_ms == 0) return;
    if(lv_display_get_inactive_time(NULL) < idle_timeout_ms) return;

    if(!page_clock_is_ambient()) {
        ui_navigate(UI_PAGE_CLOCK);
        page_clock_set_ambient(true);
    }

    /*Without always-on display, idle is the screen off -- the ambient face
     *still underneath, for when always-on comes back.*/
    if(!always_on) screen_off_set(true);
}

/**
 * The screen off: a black cover over everything, on the system layer, that
 * takes the touch waking it so the touch does nothing else. On the clock the
 * backlight goes off with it (ui_settings_feed); in the simulator, which has
 * no backlight, the cover is how the screen shows it is off.
 */
static void screen_off_set(bool off)
{
    if(off == (screen_off_cover != NULL)) return;

    if(!off) {
        /*Often from the cover's own press.*/
        lv_obj_delete_async(screen_off_cover);
        screen_off_cover = NULL;
        return;
    }

    screen_off_cover = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(screen_off_cover);
    lv_obj_set_size(screen_off_cover, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_off_cover, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_off_cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_clickable(screen_off_cover, true);
    lv_obj_add_event_cb(screen_off_cover, screen_off_pressed, LV_EVENT_PRESSED, NULL);
}

static void screen_off_pressed(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_wake();
}
