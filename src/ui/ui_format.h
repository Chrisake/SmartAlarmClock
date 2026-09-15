/**
 * @file ui_format.h
 *
 * Times and dates as the user asked to see them: 12- or 24-hour, and day,
 * month and year in their chosen order. Every time or date shown anywhere in
 * the UI goes through here, so one setting changes them all.
 *
 *                     12-hour, DMY              24-hour, MDY             YMD
 *   ui_format_time    7:24 PM                   19:24                    (as clock setting)
 *   ui_format_clock   7:24  (+ "PM" apart)      19:24
 *   ui_format_hour    2 PM                      14:00
 *   date_long         Monday, 14 September      Monday, September 14     Monday, 2026-09-14
 *   date_short        Mon 14 Sep                Mon Sep 14               Mon 09-14
 *   date_numeric      14/09/2026                09/14/2026               2026-09-14
 *
 * The settings are read on every call. Text already on screen does not
 * change by itself: after a change of format the UI is rebuilt, and the feeds
 * format their text afresh.
 *
 * Also here: copying text a user typed into a fixed buffer without cutting a
 * character in half.
 */

#ifndef UI_FORMAT_H
#define UI_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** @return   true if times are shown on the 24-hour clock */
bool ui_format_24h(void);

/** A time of day with its AM/PM where there is one: "7:24 PM" or "19:24". */
void ui_format_time(char * buf, size_t size, int hour, int minute);

/** The digits only, for a readout that shows AM/PM apart: "7:24" or "19:24". */
void ui_format_clock(char * buf, size_t size, int hour, int minute);

/** @return   "AM" or "PM", or NULL on the 24-hour clock */
const char * ui_format_meridiem(int hour);

/** A whole hour, for hourly forecasts: "2 PM" or "14:00". */
void ui_format_hour(char * buf, size_t size, int hour);

/** "Monday, 14 September" -- the date as a heading. */
void ui_format_date_long(char * buf, size_t size, const struct tm * date);

/** "Mon 14 Sep" -- a date in a list. */
void ui_format_date_short(char * buf, size_t size, const struct tm * date);

/** "14/09/2026" -- a date in full, in figures. */
void ui_format_date_numeric(char * buf, size_t size, const struct tm * date);

/** @return   day name for `wday` (0 = Sunday), e.g. "Monday" or "Mon" */
const char * ui_format_weekday(int wday, bool abbreviated);

/** @return   month name for `month` (0 = January), e.g. "September" or "Sep" */
const char * ui_format_month(int month, bool abbreviated);

/**
 * Copy UTF-8 text into a buffer. If it does not fit, it is cut short between
 * characters -- a character split partway shows as garbage.
 * @param buf    destination, always terminated
 * @param size   its size in bytes
 * @param text   the text
 */
void ui_format_text_copy(char * buf, size_t size, const char * text);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_FORMAT_H*/
