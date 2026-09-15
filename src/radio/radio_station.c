/**
 * @file radio_station.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "radio/radio_station.h"

#include <stdio.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void copy_string(const cJSON * object, const char * key, char * out, size_t size);
static void trim(char * text);
static void utf8_trim(char * text);
static int  lower(int c);
static bool equals_ci(const char * a, const char * b);
static bool contains_ci(const char * haystack, const char * needle);
static bool next_item(const char ** cursor, char * out, size_t size);
static bool url_encode(const char * text, char * out, size_t size);

/**********************
 *  STATIC VARIABLES
 **********************/

/*The directory uses formal country names. Matched without a leading "The ".*/
static const struct {
    const char * formal;
    const char * common;
} country_names[] = {
    {"United Kingdom Of Great Britain And Northern Ireland", "United Kingdom"},
    {"United States Of America",                             "United States"},
    {"Russian Federation",                                   "Russia"},
    {"Republic Of Korea",                                    "South Korea"},
    {"Korea, Republic Of",                                   "South Korea"},
    {"Democratic People's Republic Of Korea",                "North Korea"},
    {"Islamic Republic Of Iran",                             "Iran"},
    {"Iran, Islamic Republic Of",                            "Iran"},
    {"Plurinational State Of Bolivia",                       "Bolivia"},
    {"Bolivarian Republic Of Venezuela",                     "Venezuela"},
    {"Republic Of Moldova",                                  "Moldova"},
    {"United Republic Of Tanzania",                          "Tanzania"},
    {"Syrian Arab Republic",                                 "Syria"},
    {"Lao People's Democratic Republic",                     "Laos"},
    {"Viet Nam",                                             "Vietnam"},
    {"Taiwan, Province Of China",                            "Taiwan"},
    {"Democratic Republic Of The Congo",                     "DR Congo"},
};

/*Tags that name a style of music or a kind of programme, matched anywhere in
 *a tag, so "alternative rock" counts. Codecs, bitrates, places and station
 *names are left out on purpose.*/
static const char * const genre_words[] = {
    "alternative", "ambient", "blues", "chill", "classical", "country", "dance", "disco", "downtempo",
    "eclectic", "electronic", "experimental", "folk", "funk", "gospel", "hip hop", "hiphop", "hits",
    "house", "indie", "instrumental", "jazz", "laika", "latin", "lounge", "metal", "news", "oldies",
    "pop", "punk", "r&b", "rap", "rebetiko", "reggae", "rock", "soul", "soundtrack", "sports", "talk",
    "techno", "trance", "underground", "world", "70s", "80s", "90s",
};

/*Kinds of station: said only when no tag names a genre.*/
static const char * const station_kinds[] = {
    "public radio", "community radio", "college radio", "student radio", "christian",
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool radio_station_uuid_valid(const char * uuid)
{
    if(!uuid || strlen(uuid) != RADIO_UUID_LEN - 1) return false;

    for(int i = 0; i < RADIO_UUID_LEN - 1; i++) {
        char c = uuid[i];
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;

        if(dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool radio_station_from_json(const cJSON * object, radio_station_t * out)
{
    memset(out, 0, sizeof(*out));
    if(!cJSON_IsObject(object)) return false;

    copy_string(object, "stationuuid", out->uuid, sizeof(out->uuid));
    if(!radio_station_uuid_valid(out->uuid)) return false;

    copy_string(object, "name", out->name, sizeof(out->name));
    copy_string(object, "url_resolved", out->stream_url, sizeof(out->stream_url));
    if(!out->stream_url[0]) copy_string(object, "url", out->stream_url, sizeof(out->stream_url));
    copy_string(object, "homepage", out->homepage, sizeof(out->homepage));
    copy_string(object, "favicon", out->favicon_url, sizeof(out->favicon_url));
    copy_string(object, "country", out->country, sizeof(out->country));
    copy_string(object, "countrycode", out->country_code, sizeof(out->country_code));
    copy_string(object, "language", out->language, sizeof(out->language));
    copy_string(object, "tags", out->tags, sizeof(out->tags));
    copy_string(object, "codec", out->codec, sizeof(out->codec));

    const cJSON * bitrate = cJSON_GetObjectItemCaseSensitive(object, "bitrate");
    out->bitrate = cJSON_IsNumber(bitrate) ? bitrate->valueint : 0;

    /*The API says 0 or 1; accept a JSON boolean too.*/
    const cJSON * hls = cJSON_GetObjectItemCaseSensitive(object, "hls");
    out->hls = cJSON_IsNumber(hls) ? hls->valueint != 0 : cJSON_IsTrue(hls);

    return true;
}

cJSON * radio_station_to_json(const radio_station_t * station)
{
    cJSON * object = cJSON_CreateObject();
    if(!object) return NULL;

    cJSON_AddStringToObject(object, "stationuuid", station->uuid);
    cJSON_AddStringToObject(object, "name", station->name);
    cJSON_AddStringToObject(object, "url_resolved", station->stream_url);
    cJSON_AddStringToObject(object, "homepage", station->homepage);
    cJSON_AddStringToObject(object, "favicon", station->favicon_url);
    cJSON_AddStringToObject(object, "country", station->country);
    cJSON_AddStringToObject(object, "countrycode", station->country_code);
    cJSON_AddStringToObject(object, "language", station->language);
    cJSON_AddStringToObject(object, "tags", station->tags);
    cJSON_AddStringToObject(object, "codec", station->codec);
    cJSON_AddNumberToObject(object, "bitrate", station->bitrate);
    cJSON_AddNumberToObject(object, "hls", station->hls ? 1 : 0);

    return object;
}

uint32_t radio_station_parse_list(const char * json, size_t len, radio_station_t * out, uint32_t max)
{
    cJSON *       root  = cJSON_ParseWithLength(json, len);
    const cJSON * item  = NULL;
    uint32_t      count = 0;

    if(cJSON_IsArray(root)) {
        cJSON_ArrayForEach(item, root) {
            if(count >= max) break;
            if(radio_station_from_json(item, &out[count])) count++;
        }
    }

    cJSON_Delete(root);
    return count;
}

void radio_station_country_text(const radio_station_t * station, char * buf, size_t size)
{
    const char * name = station->country;

    if(lower(name[0]) == 't' && lower(name[1]) == 'h' && lower(name[2]) == 'e' && name[3] == ' ') name += 4;

    for(size_t i = 0; i < ARRAY_LEN(country_names); i++) {
        if(equals_ci(name, country_names[i].formal)) {
            snprintf(buf, size, "%s", country_names[i].common);
            return;
        }
    }

    snprintf(buf, size, "%s", name[0] ? name : station->country_code);
}

void radio_station_language_text(const radio_station_t * station, char * buf, size_t size)
{
    const char * cursor = station->language;

    if(size == 0) return;
    buf[0] = '\0';
    if(!next_item(&cursor, buf, size)) return;

    /*"english" -> "English", "serbo-croatian" -> "Serbo-Croatian".*/
    for(size_t i = 0; buf[i]; i++) {
        if(i == 0 || buf[i - 1] == ' ' || buf[i - 1] == '-') {
            if(buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (char)(buf[i] - 32);
        }
    }
}

void radio_station_genre_text(const radio_station_t * station, char * buf, size_t size)
{
    char tag[64];

    if(size == 0) return;
    buf[0] = '\0';

    for(int pass = 0; pass < 2; pass++) {
        const char * const * words  = pass == 0 ? genre_words : station_kinds;
        size_t               count  = pass == 0 ? ARRAY_LEN(genre_words) : ARRAY_LEN(station_kinds);
        const char *         cursor = station->tags;

        while(next_item(&cursor, tag, sizeof(tag))) {
            for(size_t i = 0; i < count; i++) {
                if(!contains_ci(tag, words[i])) continue;

                snprintf(buf, size, "%s", tag);
                if(buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 32);
                return;
            }
        }
    }
}

bool radio_browser_search_url(radio_search_field_t field, const char * query, uint32_t limit, char * buf,
                              size_t size)
{
    static const char * const keys[] = {"name", "tag", "country"};
    char encoded[256];

    if((size_t)field >= ARRAY_LEN(keys) || !url_encode(query, encoded, sizeof(encoded))) return false;

    int n = snprintf(buf, size,
                     RADIO_BROWSER_API "/json/stations/search?%s=%s&hidebroken=true&order=clickcount&reverse=true"
                     "&limit=%u",
                     keys[field], encoded, (unsigned)limit);
    return n > 0 && (size_t)n < size;
}

bool radio_browser_byuuid_url(const char * const uuids[], uint32_t count, char * buf, size_t size)
{
    int n = snprintf(buf, size, RADIO_BROWSER_API "/json/stations/byuuid?uuids=");
    if(n < 0 || (size_t)n >= size) return false;

    size_t used = (size_t)n;
    for(uint32_t i = 0; i < count; i++) {
        n = snprintf(buf + used, size - used, "%s%s", i ? "," : "", uuids[i]);
        if(n < 0 || (size_t)n >= size - used) return false;
        used += (size_t)n;
    }
    return true;
}

bool radio_browser_click_url(const char * uuid, char * buf, size_t size)
{
    int n = snprintf(buf, size, RADIO_BROWSER_API "/json/url/%s", uuid);
    return n > 0 && (size_t)n < size;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void copy_string(const cJSON * object, const char * key, char * out, size_t size)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(object, key);

    snprintf(out, size, "%s", cJSON_IsString(item) && item->valuestring ? item->valuestring : "");
    utf8_trim(out);
    trim(out);
}

/** Strip surrounding white space, which some directory entries carry. */
static void trim(char * text)
{
    size_t start = 0;
    while(text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n') start++;

    size_t end = strlen(text);
    while(end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                          text[end - 1] == '\n')) end--;

    memmove(text, text + start, end - start);
    text[end - start] = '\0';
}

/** Drop a multi-byte character that truncation cut in half. */
static void utf8_trim(char * text)
{
    size_t len = strlen(text);
    size_t i   = len;

    while(i > 0 && ((unsigned char)text[i - 1] & 0xC0) == 0x80) i--;
    if(i == 0) return;

    unsigned char lead = (unsigned char)text[i - 1];
    size_t        need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;

    if(len - (i - 1) < need) text[i - 1] = '\0';
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool equals_ci(const char * a, const char * b)
{
    while(*a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool contains_ci(const char * haystack, const char * needle)
{
    size_t n = strlen(needle);

    for(; *haystack; haystack++) {
        size_t i = 0;
        while(i < n && haystack[i] && lower((unsigned char)haystack[i]) == lower((unsigned char)needle[i])) i++;
        if(i == n) return true;
    }
    return false;
}

/** The next entry of a comma separated list, trimmed. */
static bool next_item(const char ** cursor, char * out, size_t size)
{
    const char * p = *cursor;

    while(*p == ',' || *p == ' ') p++;
    if(*p == '\0') return false;

    const char * end = p;
    while(*end && *end != ',') end++;

    const char * last = end;
    while(last > p && last[-1] == ' ') last--;

    size_t len = (size_t)(last - p);
    if(len >= size) len = size - 1;

    memcpy(out, p, len);
    out[len] = '\0';
    *cursor  = end;
    return true;
}

static bool url_encode(const char * text, char * out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    for(const unsigned char * p = (const unsigned char *)text; *p; p++) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                     *p == '-' || *p == '_' || *p == '.' || *p == '~';

        if(plain) {
            if(n + 1 >= size) return false;
            out[n++] = (char)*p;
        }
        else {
            if(n + 3 >= size) return false;
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 0x0F];
        }
    }

    out[n] = '\0';
    return true;
}
