/**
 * @file page_clock.h
 *
 * Home page. Has two states.
 *
 * AMBIENT is what the device shows when nobody is using it: the time and the
 * date, centred on an otherwise empty screen. The shell drops back to it after
 * UI_IDLE_TIMEOUT_MS of no input (see ui.c), and the first touch anywhere
 * brings the detail back.
 *
 * ACTIVE moves the clock up to the top-left and fades everything else in:
 *
 *   +-----------------------------+---------------------+
 *   |  07:24                      |  NOW    21 deg      |
 *   |  Saturday, 13 September     |  Partly cloudy      |
 *   |  (bell) 06:30 Weekdays      |  MinuteCast graph   |
 *   +-----------------------------+  |  +---------------+
 *   |  CALENDAR EVENTS            |  |  |  AIR QUALITY  |
 *   |  today   |  upcoming        |  |  |  AQI + PM/CO2 |
 *   |  09:30   |  Mon 15          |  |  |  indoor / rh  |
 *   +-----------------------------+---------------------+
 *
 * Four regions, separated by hairlines that fade out at both ends rather than
 * by boxes: one down the middle, and one across each side.
 *
 * The two rows split the height evenly. There are deliberately no card
 * outlines here -- only whitespace and two dividers -- so the ambient state
 * can dissolve into the active one without a grid of boxes appearing.
 *
 * Application code should only ever talk to this page through the setters
 * below, so the clock, weather, calendar and sensor services never include
 * lvgl.h.
 */

#ifndef PAGE_CLOCK_H
#define PAGE_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_page.h"
#include "ui/ui_status.h"
#include "ui/ui_weather_icon.h"

/*********************
 *      DEFINES
 *********************/

/** Minutes covered by the precipitation band. */
#define PAGE_CLOCK_RAIN_MINUTES 120

/** Bars the graph is drawn from, so each covers 2 minutes. */
#define PAGE_CLOCK_RAIN_SEGMENTS 60

/**
 * Slots per column. Entries stack from the top and the column scrolls once
 * they no longer fit, so this is only the point at which entries start being
 * dropped rather than the point at which they stop being visible.
 */
#define PAGE_CLOCK_EVENTS_PER_COLUMN 16

/**********************
 *      TYPEDEFS
 **********************/

/**
 * How hard it is raining during one bar of the precipitation graph. Intensity
 * sets both the bar's height and its colour, matching the MinuteCast legend.
 */
typedef enum {
    PAGE_CLOCK_RAIN_NONE,      /**< Dry; no bar drawn */
    PAGE_CLOCK_RAIN_LIGHT,     /**< Green */
    PAGE_CLOCK_RAIN_MODERATE,  /**< Yellow */
    PAGE_CLOCK_RAIN_HEAVY,     /**< Orange */
    PAGE_CLOCK_RAIN_EXTREME,   /**< Red */
} page_clock_rain_level_t;

/**
 * The two columns of the events section.
 *
 * Neither is labelled on screen. They are told apart by what they hold and by
 * how each entry is timestamped: the left column is the day's schedule, so it
 * shows clock times; the right column is the fortnight ahead, made of all-day
 * entries, so it shows the day instead.
 */
typedef enum {
    PAGE_CLOCK_COLUMN_TODAY,     /**< Left: today, labelled by time */
    PAGE_CLOCK_COLUMN_UPCOMING,  /**< Right: next 14 days, labelled by day */
    PAGE_CLOCK_COLUMN_COUNT,
} page_clock_column_t;

/**
 * Which calendar an entry came from. Entries are not labelled -- this is what
 * picks the colour of the bar in front of the title, and that colour is the
 * only thing identifying the calendar.
 */
typedef enum {
    PAGE_CLOCK_CAL_HOLIDAY,   /**< National holidays, green */
    PAGE_CLOCK_CAL_OCCASION,  /**< Birthdays and anniversaries, yellow */
    PAGE_CLOCK_CAL_PERSONAL,  /**< Personal, blue */
    PAGE_CLOCK_CAL_WORK,      /**< Work meetings, purple */
    PAGE_CLOCK_CAL_COUNT,
} page_clock_calendar_t;

/**
 * One entry in the events section.
 *
 * Which of `time` and `day` is shown depends on the column the entry is routed
 * to, so fill in whichever suits the entry and leave the other NULL; the page
 * falls back to it if the entry ends up in the other column.
 */
typedef struct {
    const char *          time;      /**< Shown in the TODAY column, e.g. "09:30", "All day" */
    const char *          day;       /**< Shown in the UPCOMING column, e.g. "Mon 15", "in 3 days" */
    const char *          title;
    page_clock_calendar_t calendar;  /**< Picks the colour of the leading bar */
} page_clock_event_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_clock_desc(void);

/**
 * Switch between the ambient and active states, animating the transition.
 * Calling it with the state the page is already in does nothing.
 * @param ambient   true for the idle clock face, false for the full page
 */
void page_clock_set_ambient(bool ambient);

/**
 * @return   true while the page is showing the ambient clock face
 */
bool page_clock_is_ambient(void);

/**
 * Set the clock readout.
 * @param time_text   e.g. "07:24"
 * @param seconds     e.g. "31", or NULL to hide the seconds
 * @param meridiem    e.g. "AM", or NULL for 24-hour display
 */
void page_clock_set_time(const char * time_text, const char * seconds, const char * meridiem);

/**
 * Set the date line under the clock.
 * @param date_text   e.g. "Saturday, 13 September"
 */
void page_clock_set_date(const char * date_text);

/**
 * Set the next-alarm chip, e.g. "Wake up  7:00 AM  Tomorrow".
 *
 * Deciding which alarm is next needs a clock and a calendar, neither of which
 * this page has; the application works it out from the alarms page's list and
 * pushes the answer here.
 *
 * @param name      the alarm's label, or NULL to hide the chip entirely
 * @param time      e.g. "7:00 AM"
 * @param when      e.g. "Tomorrow" or "on Monday"
 * @param enabled   true to show it as armed, false to show it as off
 */
void page_clock_set_next_alarm(const char * name, const char * time, const char * when,
                               bool enabled);

/**
 * Set the current-conditions block above the precipitation graph. Tapping the
 * weather section opens the weather page.
 * @param icon        the condition, drawn; the _NIGHT variants after dark
 * @param temp        e.g. "21" UI_DEG
 * @param condition   e.g. "Partly cloudy"
 * @param real_feel   e.g. "19" UI_DEG
 * @param humidity    e.g. "62 %"
 */
void page_clock_set_weather_now(ui_weather_t icon, const char * temp, const char * condition,
                                const char * real_feel, const char * humidity);

/**
 * Say whether there is a forecast for the weather section. Until there is,
 * the section shows a spinner, or why not; tapping it still opens the weather
 * page, which offers Try again.
 * @param state     loading, ready or failed
 * @param message   why, when failed
 */
void page_clock_set_weather_state(ui_status_state_t state, const char * message);

/**
 * Fill the minute-by-minute precipitation graph.
 *
 * The graph runs left to right over the next PAGE_CLOCK_RAIN_MINUTES minutes,
 * one bar per entry, each rising from a common baseline. Bars past `count`
 * are drawn dry, so a short forecast simply leaves the tail flat.
 *
 * @param levels    intensity per bar, earliest first
 * @param count     number of entries, clamped to PAGE_CLOCK_RAIN_SEGMENTS
 * @param summary   headline above the graph, e.g. "Rain starts in 24 min";
 *                  NULL leaves the previous text. Must outlive the call.
 */
void page_clock_set_rain(const page_clock_rain_level_t levels[], uint32_t count,
                         const char * summary);

/**
 * Route a calendar to a column.
 *
 * This is configuration, not something the user reaches from the UI yet. The
 * defaults are holidays and occasions on the right, personal and work on the
 * left; call this before page_clock_set_events() to change that.
 *
 * @param calendar   the calendar to route
 * @param column     the column its entries should appear in
 */
void page_clock_set_calendar_column(page_clock_calendar_t calendar, page_clock_column_t column);

/**
 * @param calendar   the calendar to look up
 * @return           the column that calendar is currently routed to
 */
page_clock_column_t page_clock_get_calendar_column(page_clock_calendar_t calendar);

/**
 * Fill the events section from one merged list. Each entry goes to whichever
 * column its calendar is routed to, in the order given; once a column is full
 * the rest of its entries are dropped.
 *
 * The caller still decides *which* events to pass -- typically today's plus
 * the next 14 days of all-day entries. Sort before calling: the page renders
 * the order it is given.
 *
 * @param events   array of entries; strings must outlive the call
 * @param count    number of entries
 */
void page_clock_set_events(const page_clock_event_t events[], uint32_t count);

/**
 * Set the air quality summary. Tapping the section opens the air quality page.
 * @param aqi     index value, 0..500; drives the colour and band name; negative when unknown
 * @param pm25    e.g. "8 ug/m3"
 * @param co2     e.g. "640 ppm"
 */
void page_clock_set_air_summary(int32_t aqi, const char * pm25, const char * co2);

/**
 * Set the indoor readings shown beneath the air quality summary.
 * @param temperature   e.g. "21" UI_DEG "C"
 * @param humidity      e.g. "46 %"
 */
void page_clock_set_indoor(const char * temperature, const char * humidity);

/**
 * As page_clock_set_weather_state(), for the air quality section: whether
 * the sensors have reported yet.
 */
void page_clock_set_air_state(ui_status_state_t state, const char * message);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_CLOCK_H*/
