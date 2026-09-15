/**
 * @file clock_time.h
 *
 * What time it is here: the one source of the local date and time for the
 * whole device.
 *
 * Time zones are POSIX TZ strings, the form ESP-IDF's libc takes through
 * setenv("TZ") -- "EET-2EEST,M3.5.0/3,M10.5.0/4" for Athens -- and they are
 * worked out here rather than through the C library, so daylight saving comes
 * out the same in the simulator as on the clock. A short table names the
 * common zones for the settings page.
 *
 * The clock is either on network time (the system clock, kept by SNTP on the
 * device) or set by hand, which is kept as an offset from the system clock.
 *
 * Plain C with no LVGL.
 */

#ifndef CLOCK_TIME_H
#define CLOCK_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*********************
 *      DEFINES
 *********************/

#define CLOCK_ZONE_NAME_LEN  48
#define CLOCK_ZONE_POSIX_LEN 48

/**********************
 *      TYPEDEFS
 **********************/

/** A named time zone. */
typedef struct {
    const char * name;    /**< IANA name, e.g. "Europe/Athens" */
    const char * posix;   /**< POSIX TZ string */
} clock_zone_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** @return   number of zones in the table */
uint32_t clock_zone_count(void);

/** @return   zone `index`, or NULL if out of range */
const clock_zone_t * clock_zone_get(uint32_t index);

/** @return   index of the zone called `name`, or -1 */
int32_t clock_zone_find(const char * name);

/**
 * Use a time zone.
 * @param posix   POSIX TZ string
 * @return        false if it could not be read; the zone is then UTC
 */
bool clock_time_set_zone(const char * posix);

/** The local date and time now. `tm_isdst` says whether daylight saving is in force. */
void clock_time_now(struct tm * local);

/** @return   seconds east of UTC in the current zone at `utc` */
int32_t clock_time_utc_offset(time_t utc);

/** Convert a UTC instant to local time in the current zone. */
void clock_time_local(time_t utc, struct tm * local);

/**
 * Set the clock by hand. The fields that matter are the year, month, day,
 * hour, minute and second; the day is clamped to the month.
 */
void clock_time_set_local(const struct tm * local);

/** Go back to the system clock -- network time. */
void clock_time_use_system(void);

/** @return   true while the clock is set by hand */
bool clock_time_is_manual(void);

/** @return   days in `month` (0..11) of `year` */
int clock_time_days_in_month(int year, int month);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*CLOCK_TIME_H*/
