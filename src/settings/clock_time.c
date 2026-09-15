/**
 * @file clock_time.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "settings/clock_time.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define SECONDS_PER_DAY 86400

/**********************
 *      TYPEDEFS
 **********************/

/** A POSIX "Mm.w.d/time" transition: the d-th weekday (0 = Sunday) of week w
 *  (5 = last) of month m, at `time` seconds past local midnight. */
typedef struct {
    int     month;
    int     week;
    int     day;
    int32_t time;
} transition_t;

typedef struct {
    int32_t      std_offset;   /**< Seconds east of UTC */
    int32_t      dst_offset;
    bool         has_dst;
    transition_t start;        /**< Into daylight saving, in standard time */
    transition_t end;          /**< Out of it, in daylight time */
} zone_rules_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool    parse_name(const char ** p);
static bool    parse_offset(const char ** p, int32_t * seconds);
static bool    parse_transition(const char ** p, transition_t * t);
static int64_t days_from_civil(int64_t year, int month, int day);
static void    civil_from_days(int64_t days, int * year, int * month, int * day);
static int64_t transition_local(int year, const transition_t * t);
static bool    dst_at(time_t utc);

/**********************
 *  STATIC VARIABLES
 **********************/

/*The zones the settings page offers, west to east. POSIX strings as in the
 *IANA database's own TZ footers, which is also what ESP-IDF uses.*/
static const clock_zone_t zones[] = {
    {"Pacific/Honolulu",    "HST10"},
    {"America/Anchorage",   "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Denver",      "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix",     "MST7"},
    {"America/Chicago",     "CST6CDT,M3.2.0,M11.1.0"},
    {"America/New_York",    "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo",   "<-03>3"},
    {"UTC",                 "UTC0"},
    {"Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon",       "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam",    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Africa/Johannesburg", "SAST-2"},
    {"Africa/Cairo",        "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Europe/Athens",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Asia/Nicosia",        "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki",     "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul",     "<+03>-3"},
    {"Europe/Moscow",       "MSK-3"},
    {"Asia/Dubai",          "<+04>-4"},
    {"Asia/Kolkata",        "IST-5:30"},
    {"Asia/Bangkok",        "<+07>-7"},
    {"Asia/Singapore",      "<+08>-8"},
    {"Asia/Shanghai",       "CST-8"},
    {"Asia/Tokyo",          "JST-9"},
    {"Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland",    "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
#define ZONE_COUNT (sizeof(zones) / sizeof(zones[0]))

static zone_rules_t rules;          /**< All zero: UTC */
static time_t       manual_offset;  /**< Added to the system clock */
static bool         manual;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

uint32_t clock_zone_count(void)
{
    return ZONE_COUNT;
}

const clock_zone_t * clock_zone_get(uint32_t index)
{
    return index < ZONE_COUNT ? &zones[index] : NULL;
}

int32_t clock_zone_find(const char * name)
{
    for(uint32_t i = 0; name && i < ZONE_COUNT; i++) {
        if(strcmp(zones[i].name, name) == 0) return (int32_t)i;
    }
    return -1;
}

bool clock_time_set_zone(const char * posix)
{
    zone_rules_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    memset(&rules, 0, sizeof(rules));

    const char * p = posix;
    int32_t      west;

    /*std offset [dst [offset] [,start[/time],end[/time]]]*/
    if(!p || !parse_name(&p) || !parse_offset(&p, &west)) return false;
    parsed.std_offset = -west;

    if(*p != '\0') {
        if(!parse_name(&p)) return false;
        parsed.has_dst    = true;
        parsed.dst_offset = parsed.std_offset + 3600;

        if(*p != '\0' && *p != ',') {
            if(!parse_offset(&p, &west)) return false;
            parsed.dst_offset = -west;
        }

        /*Without rules there is no telling when it applies, so none is used.*/
        const char * q = p;
        bool         ruled = false;

        if(*q == ',') {
            q++;
            if(parse_transition(&q, &parsed.start) && *q == ',') {
                q++;
                ruled = parse_transition(&q, &parsed.end);
            }
        }
        if(!ruled) parsed.has_dst = false;
    }

    rules = parsed;
    return true;
}

void clock_time_now(struct tm * local)
{
    clock_time_local(time(NULL) + manual_offset, local);
}

int32_t clock_time_utc_offset(time_t utc)
{
    return dst_at(utc) ? rules.dst_offset : rules.std_offset;
}

void clock_time_local(time_t utc, struct tm * local)
{
    bool    dst    = dst_at(utc);
    int64_t t      = (int64_t)utc + (dst ? rules.dst_offset : rules.std_offset);
    int64_t days   = t / SECONDS_PER_DAY;
    int64_t second = t % SECONDS_PER_DAY;
    if(second < 0) {
        second += SECONDS_PER_DAY;
        days--;
    }

    int year, month, day;
    civil_from_days(days, &year, &month, &day);

    memset(local, 0, sizeof(*local));
    local->tm_year  = year - 1900;
    local->tm_mon   = month - 1;
    local->tm_mday  = day;
    local->tm_hour  = (int)(second / 3600);
    local->tm_min   = (int)(second / 60 % 60);
    local->tm_sec   = (int)(second % 60);
    local->tm_wday  = (int)(((days % 7) + 11) % 7);   /*1970-01-01 was a Thursday*/
    local->tm_yday  = (int)(days - days_from_civil(year, 1, 1));
    local->tm_isdst = dst;
}

void clock_time_set_local(const struct tm * local)
{
    int year  = local->tm_year + 1900;
    int month = local->tm_mon;
    int day   = local->tm_mday;

    if(month < 0) month = 0;
    if(month > 11) month = 11;
    if(day < 1) day = 1;
    if(day > clock_time_days_in_month(year, month)) day = clock_time_days_in_month(year, month);

    int64_t wall = days_from_civil(year, month + 1, day) * SECONDS_PER_DAY +
                   local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec;

    /*The offset depends on the instant, which depends on the offset: guess
     *with standard time, then settle on whatever applies there.*/
    time_t utc = (time_t)(wall - rules.std_offset);
    utc        = (time_t)(wall - clock_time_utc_offset(utc));

    manual_offset = utc - time(NULL);
    manual        = true;
}

void clock_time_use_system(void)
{
    manual_offset = 0;
    manual        = false;
}

bool clock_time_is_manual(void)
{
    return manual;
}

int clock_time_days_in_month(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return (month == 1 && leap) ? 29 : days[month % 12];
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** "EET", or a quoted "<+03>". */
static bool parse_name(const char ** p)
{
    const char * s = *p;

    if(*s == '<') {
        const char * close = strchr(s, '>');
        if(!close) return false;
        *p = close + 1;
        return true;
    }

    while(isalpha((unsigned char)*s)) s++;
    if(s - *p < 3) return false;
    *p = s;
    return true;
}

/** "[+|-]hh[:mm[:ss]]" in seconds, as written: positive is west of Greenwich. */
static bool parse_offset(const char ** p, int32_t * seconds)
{
    const char * s    = *p;
    int          sign = 1;

    if(*s == '+' || *s == '-') {
        if(*s == '-') sign = -1;
        s++;
    }
    if(!isdigit((unsigned char)*s)) return false;

    int32_t parts[3] = {0, 0, 0};
    for(int i = 0; i < 3; i++) {
        while(isdigit((unsigned char)*s)) parts[i] = parts[i] * 10 + (*s++ - '0');
        if(*s != ':' || i == 2) break;
        s++;
    }

    *seconds = sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]);
    *p       = s;
    return true;
}

/** "Mm.w.d[/time]". Julian-day rules are rare enough to go unsupported. */
static bool parse_transition(const char ** p, transition_t * t)
{
    int m = 0, w = 0, d = 0, used = 0;
    if(sscanf(*p, "M%d.%d.%d%n", &m, &w, &d, &used) != 3) return false;
    if(m < 1 || m > 12 || w < 1 || w > 5 || d < 0 || d > 6) return false;

    *p      += used;
    t->month = m;
    t->week  = w;
    t->day   = d;
    t->time  = 7200;

    if(**p == '/') {
        (*p)++;
        if(!parse_offset(p, &t->time)) return false;
    }
    return true;
}

/** Days since 1970-01-01 of a proleptic Gregorian date; month 1..12. */
static int64_t days_from_civil(int64_t year, int month, int day)
{
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t yoe = year - era * 400;
    int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t days, int * year, int * month, int * day)
{
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp  = (5 * doy + 2) / 153;

    *day   = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year  = (int)(yoe + era * 400 + (*month <= 2));
}

/** A transition in `year`, as seconds of local wall time since the epoch. */
static int64_t transition_local(int year, const transition_t * t)
{
    int64_t first     = days_from_civil(year, t->month, 1);
    int     first_day = (int)(((first % 7) + 11) % 7);
    int     day       = 1 + (t->day - first_day + 7) % 7 + (t->week - 1) * 7;
    int     last      = clock_time_days_in_month(year, t->month - 1);

    while(day > last) day -= 7;   /*Week 5 means the last one*/

    return (first + day - 1) * SECONDS_PER_DAY + t->time;
}

static bool dst_at(time_t utc)
{
    if(!rules.has_dst) return false;

    int64_t days = ((int64_t)utc + rules.std_offset) / SECONDS_PER_DAY;
    int     year, month, day;
    civil_from_days(days, &year, &month, &day);

    int64_t start = transition_local(year, &rules.start) - rules.std_offset;
    int64_t end   = transition_local(year, &rules.end) - rules.dst_offset;

    /*Southern-hemisphere zones start in spring and end the following autumn,
     *so their summer straddles the new year.*/
    if(start < end) return utc >= start && utc < end;
    return !(utc >= end && utc < start);
}
