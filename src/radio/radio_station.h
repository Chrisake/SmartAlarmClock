/**
 * @file radio_station.h
 *
 * A station as the Radio Browser directory (https://www.radio-browser.info)
 * describes it, and the URLs of the directory's API.
 *
 * Only the fields the clock uses are kept. They are read from the API's JSON
 * and written back under the same key names, so a station.json on the SD card
 * reads like a trimmed copy of the directory's own record.
 *
 * Plain C with no LVGL: parsing happens on whichever thread has the text.
 */

#ifndef RADIO_STATION_H
#define RADIO_STATION_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "cJSON.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

/** A station UUID, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", with its terminator. */
#define RADIO_UUID_LEN 37

/** Radio Browser's round-robin API host. */
#define RADIO_BROWSER_API "https://all.api.radio-browser.info"

/**********************
 *      TYPEDEFS
 **********************/

/** What a search matches against. */
typedef enum {
    RADIO_SEARCH_NAME,
    RADIO_SEARCH_TAG,
    RADIO_SEARCH_COUNTRY,
} radio_search_field_t;

typedef struct {
    char uuid[RADIO_UUID_LEN];  /**< "stationuuid", which stays the same when the entry is edited */
    char name[128];
    char stream_url[512];       /**< "url_resolved", else "url" */
    char homepage[256];
    char favicon_url[512];      /**< "favicon"; often empty */
    char country[80];           /**< As the directory writes it: "The United Kingdom Of ..." */
    char country_code[4];       /**< ISO 3166-1, "GB" */
    char language[64];          /**< Comma separated, lower case: "english,french" */
    char tags[256];             /**< Comma separated, lower case */
    char codec[16];
    int  bitrate;               /**< kbps; 0 if unknown */
    bool hls;                   /**< The stream is an HLS playlist */
} radio_station_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @param uuid   text to check
 * @return       true if it is a well-formed UUID, and so safe in a file path
 */
bool radio_station_uuid_valid(const char * uuid);

/**
 * Read a station from a Radio Browser record or a saved station.json.
 * @param object   the JSON object
 * @param out      receives the station; cleared first
 * @return         true if it had a valid UUID
 */
bool radio_station_from_json(const cJSON * object, radio_station_t * out);

/**
 * @param station   the station
 * @return          a new JSON object with the directory's key names; free with cJSON_Delete()
 */
cJSON * radio_station_to_json(const radio_station_t * station);

/**
 * Read an array of stations, as the API's search and lookup calls return.
 * @param json   the document
 * @param len    its length
 * @param out    receives up to `max` stations
 * @param max    size of `out`
 * @return       stations read
 */
uint32_t radio_station_parse_list(const char * json, size_t len, radio_station_t * out, uint32_t max);

/**
 * The country as people say it: "United Kingdom", not the directory's
 * "The United Kingdom Of Great Britain And Northern Ireland".
 */
void radio_station_country_text(const radio_station_t * station, char * buf, size_t size);

/** The first language, capitalised: "English". Empty if unknown. */
void radio_station_language_text(const radio_station_t * station, char * buf, size_t size);

/**
 * The first tag that names a genre or a kind of programme -- "Alternative
 * rock", "News", "Public radio" -- skipping codecs, places and the like.
 * Empty if no tag does.
 */
void radio_station_genre_text(const radio_station_t * station, char * buf, size_t size);

/**
 * @param field   what to match
 * @param query   what the user typed
 * @param limit   most results wanted
 * @param buf     receives the URL
 * @param size    size of `buf`
 * @return        false if it did not fit
 */
bool radio_browser_search_url(radio_search_field_t field, const char * query, uint32_t limit, char * buf,
                              size_t size);

/**
 * The URL that looks up several stations at once by UUID.
 * @return   false if it did not fit
 */
bool radio_browser_byuuid_url(const char * const uuids[], uint32_t count, char * buf, size_t size);

/**
 * The URL that tells the directory a station is being played, which it asks
 * clients to call so it can rank stations.
 * @return   false if it did not fit
 */
bool radio_browser_click_url(const char * uuid, char * buf, size_t size);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*RADIO_STATION_H*/
