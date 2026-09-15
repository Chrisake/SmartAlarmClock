/**
 * @file page_alarms.h
 *
 * Alarms page, modelled on the iOS Alarm app.
 *
 * The page has two views in the same cell. The list shows every alarm with its
 * time, label, repeat summary and an on/off switch; the editor opens over it
 * when an alarm is tapped or a new one is added:
 *
 *   +-------------------------------------------------+
 *   |  ALARMS                                     [+]  |
 *   |  +-----------------+  +-----------------+        |
 *   |  | 07:00 AM   (o)  |  | 11:00 AM   (o)  |        |
 *   |  | Wake up         |  | Stand-up        |        |
 *   |  | Weekdays        |  | Mon Wed Fri     |        |
 *   |  +-----------------+  +-----------------+        |
 *   +-------------------------------------------------+
 *
 * The page owns the alarms while the device has nowhere to persist them. When
 * you add storage, load with page_alarms_set_alarms() and save from the
 * callback registered with page_alarms_set_changed_cb() -- the page fires it
 * on every add, edit, delete and toggle.
 *
 * Working out which alarm rings next needs a clock, which this page does not
 * have. The application decides that and pushes the answer to the clock page
 * with page_clock_set_next_alarm().
 */

#ifndef PAGE_ALARMS_H
#define PAGE_ALARMS_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_page.h"

/*********************
 *      DEFINES
 *********************/

/**
 * How many alarms can exist at once.
 *
 * Deliberately not surfaced anywhere in the UI: on reaching it the add button
 * simply goes disabled, the way iOS stops you rather than explaining itself.
 */
#define PAGE_ALARMS_MAX 32

/** Longest label an alarm can carry, in characters. The name field takes no
 *  more than this as it is typed. */
#define PAGE_ALARMS_NAME_CHARS 64

/**
 * Bytes to hold that label, including the terminator. Two a character: every
 * letter the keyboards type -- accented Latin, Greek, Cyrillic -- is one or two
 * bytes of UTF-8, so a full-length name fits in any of them. A name full of
 * three-byte symbols (€, “, …) is cut short, at a character boundary.
 */
#define PAGE_ALARMS_NAME_LEN (PAGE_ALARMS_NAME_CHARS * 2 + 1)

/** A radio station's UUID, including the terminator. */
#define PAGE_ALARMS_STATION_LEN 37

/** Most stations the sound menu lists, and the longest name it keeps for one. */
#define PAGE_ALARMS_STATIONS_MAX     32
#define PAGE_ALARMS_STATION_NAME_LEN 64

/**********************
 *      TYPEDEFS
 **********************/

/** Days an alarm repeats on, as a bit set. No bits means it fires once. */
typedef enum {
    PAGE_ALARM_MON = 1 << 0,
    PAGE_ALARM_TUE = 1 << 1,
    PAGE_ALARM_WED = 1 << 2,
    PAGE_ALARM_THU = 1 << 3,
    PAGE_ALARM_FRI = 1 << 4,
    PAGE_ALARM_SAT = 1 << 5,
    PAGE_ALARM_SUN = 1 << 6,

    PAGE_ALARM_WEEKDAYS = PAGE_ALARM_MON | PAGE_ALARM_TUE | PAGE_ALARM_WED |
                          PAGE_ALARM_THU | PAGE_ALARM_FRI,
    PAGE_ALARM_WEEKENDS = PAGE_ALARM_SAT | PAGE_ALARM_SUN,
    PAGE_ALARM_EVERY_DAY = PAGE_ALARM_WEEKDAYS | PAGE_ALARM_WEEKENDS,
} page_alarm_days_t;

/** The tones an alarm can ring with, in the order the editor lists them. */
typedef enum {
    PAGE_ALARM_TONE_RADAR,
    PAGE_ALARM_TONE_CHIMES,
    PAGE_ALARM_TONE_BEACON,
    PAGE_ALARM_TONE_SIGNAL,
    PAGE_ALARM_TONE_BIRDSONG,
    PAGE_ALARM_TONE_COUNT,
} page_alarm_tone_t;

/** One alarm. */
typedef struct {
    char     name[PAGE_ALARMS_NAME_LEN];
    uint8_t  hour;             /**< 0..23; shown and edited on the clock the settings ask for */
    uint8_t  minute;           /**< 0..59 */
    uint8_t  days;             /**< Bit set of page_alarm_days_t */
    bool     enabled;
    bool     snooze;
    uint8_t  tone;             /**< page_alarm_tone_t; also what plays if `station` cannot */
    char     station[PAGE_ALARMS_STATION_LEN];  /**< Radio station to wake to, by UUID; empty for the tone */
} page_alarm_t;

/** A saved radio station, as the sound menu offers it. */
typedef struct {
    const char * uuid;
    const char * name;
} page_alarm_station_t;

/** Fired after any change, so the application can persist the new set. */
typedef void (*page_alarms_changed_cb_t)(const page_alarm_t alarms[], uint32_t count);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @return   this page's descriptor, for the shell's page table
 */
const ui_page_t * page_alarms_desc(void);

/**
 * Replace the whole set of alarms, as when restoring them at boot.
 * Does not fire the changed callback -- this is a load, not an edit.
 *
 * The order given does not matter: the page sorts by time of day.
 *
 * @param alarms   array of alarms; copied, so the caller keeps ownership
 * @param count    number of alarms, clamped to PAGE_ALARMS_MAX
 */
void page_alarms_set_alarms(const page_alarm_t alarms[], uint32_t count);

/**
 * @param count   receives the number of alarms; @nullable
 * @return        the page's alarms in display order -- by time of day,
 *                earliest first -- valid until the next edit
 */
const page_alarm_t * page_alarms_get_alarms(uint32_t * count);

/**
 * Register the callback fired whenever the set of alarms changes.
 * @param cb   callback, or NULL to clear
 */
void page_alarms_set_changed_cb(page_alarms_changed_cb_t cb);

/**
 * Write a repeat summary for a day set, the way the list shows it: "Never",
 * "Every day", "Weekdays", "Weekends", or the abbreviated days.
 *
 * @param days   bit set of page_alarm_days_t
 * @param buf    destination
 * @param len    size of `buf`
 */
void page_alarms_days_text(uint8_t days, char * buf, size_t len);

/**
 * @param tone   a page_alarm_tone_t
 * @return       its display name, or the first tone's name if out of range
 */
const char * page_alarms_tone_name(uint8_t tone);

/**
 * Replace the radio stations the sound menu offers after the tones. An editor
 * that is open keeps what it had picked.
 * @param stations   array of stations; copied
 * @param count      clamped to PAGE_ALARMS_STATIONS_MAX
 */
void page_alarms_set_stations(const page_alarm_station_t stations[], uint32_t count);

/**
 * @param uuid   a station's UUID
 * @return       its name as last given to page_alarms_set_stations(), or NULL
 *               if it is not among them
 */
const char * page_alarms_station_name(const char * uuid);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*PAGE_ALARMS_H*/
