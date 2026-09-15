/**
 * @file ui_format.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_format.h"
#include "settings/settings.h"

#include <stdio.h>

/**********************
 *  STATIC VARIABLES
 **********************/

static const char * const weekdays[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};

static const char * const weekdays_short[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static const char * const months[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};

static const char * const months_short[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool ui_format_24h(void)
{
    return settings_get()->clock_24h;
}

void ui_format_time(char * buf, size_t size, int hour, int minute)
{
    char digits[8];
    ui_format_clock(digits, sizeof(digits), hour, minute);

    const char * meridiem = ui_format_meridiem(hour);
    if(meridiem) snprintf(buf, size, "%s %s", digits, meridiem);
    else         snprintf(buf, size, "%s", digits);
}

void ui_format_clock(char * buf, size_t size, int hour, int minute)
{
    if(ui_format_24h()) {
        snprintf(buf, size, "%02d:%02d", hour, minute);
        return;
    }

    int hour12 = hour % 12;
    snprintf(buf, size, "%d:%02d", hour12 == 0 ? 12 : hour12, minute);
}

const char * ui_format_meridiem(int hour)
{
    if(ui_format_24h()) return NULL;
    return hour % 24 < 12 ? "AM" : "PM";
}

void ui_format_hour(char * buf, size_t size, int hour)
{
    if(ui_format_24h()) {
        snprintf(buf, size, "%02d:00", hour % 24);
        return;
    }

    int hour12 = hour % 12;
    snprintf(buf, size, "%d %s", hour12 == 0 ? 12 : hour12, hour % 24 < 12 ? "AM" : "PM");
}

void ui_format_date_long(char * buf, size_t size, const struct tm * date)
{
    const char * day = ui_format_weekday(date->tm_wday, false);

    switch(settings_get()->date_format) {
        case SETTINGS_DATE_MDY:
            snprintf(buf, size, "%s, %s %d", day, ui_format_month(date->tm_mon, false), date->tm_mday);
            break;
        case SETTINGS_DATE_YMD:
            snprintf(buf, size, "%s, %04d-%02d-%02d", day, date->tm_year + 1900, date->tm_mon + 1, date->tm_mday);
            break;
        case SETTINGS_DATE_DMY:
        default:
            snprintf(buf, size, "%s, %d %s", day, date->tm_mday, ui_format_month(date->tm_mon, false));
            break;
    }
}

void ui_format_date_short(char * buf, size_t size, const struct tm * date)
{
    const char * day = ui_format_weekday(date->tm_wday, true);

    switch(settings_get()->date_format) {
        case SETTINGS_DATE_MDY:
            snprintf(buf, size, "%s %s %d", day, ui_format_month(date->tm_mon, true), date->tm_mday);
            break;
        case SETTINGS_DATE_YMD:
            snprintf(buf, size, "%s %02d-%02d", day, date->tm_mon + 1, date->tm_mday);
            break;
        case SETTINGS_DATE_DMY:
        default:
            snprintf(buf, size, "%s %d %s", day, date->tm_mday, ui_format_month(date->tm_mon, true));
            break;
    }
}

void ui_format_date_numeric(char * buf, size_t size, const struct tm * date)
{
    int year = date->tm_year + 1900, month = date->tm_mon + 1, day = date->tm_mday;

    switch(settings_get()->date_format) {
        case SETTINGS_DATE_MDY: snprintf(buf, size, "%02d/%02d/%04d", month, day, year); break;
        case SETTINGS_DATE_YMD: snprintf(buf, size, "%04d-%02d-%02d", year, month, day); break;
        case SETTINGS_DATE_DMY:
        default:                snprintf(buf, size, "%02d/%02d/%04d", day, month, year); break;
    }
}

const char * ui_format_weekday(int wday, bool abbreviated)
{
    wday = ((wday % 7) + 7) % 7;
    return abbreviated ? weekdays_short[wday] : weekdays[wday];
}

const char * ui_format_month(int month, bool abbreviated)
{
    month = ((month % 12) + 12) % 12;
    return abbreviated ? months_short[month] : months[month];
}
