/**
 * @file ui_radio_feed.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/ui_radio_feed.h"
#include "ui/pages/alarms/page_alarms.h"
#include "ui/pages/radio/page_radio.h"
#include "audio/radio_player.h"
#include "net/http_worker.h"
#include "radio/radio_favicon.h"
#include "radio/radio_station.h"
#include "radio/radio_store.h"

#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** How often finished downloads are collected. */
#define POLL_MS 50

/** Largest station list accepted from Radio Browser. */
#define LIST_MAX_BYTES (512 * 1024)

/** Largest favicon download. The biggest logos in the directory run to a megabyte or so. */
#define FAVICON_MAX_BYTES (2 * 1024 * 1024)

/**********************
 *      TYPEDEFS
 **********************/

/** A station, and the words and picture the page shows for it. */
typedef struct {
    radio_station_t info;
    bool            known;          /**< `info` holds the directory's record, not just a UUID */
    bool            favicon_tried;  /**< Its favicon has been asked for since start */
    uint8_t *       pixels;         /**< Favicon, RADIO_FAVICON_SIZE squared ARGB8888; NULL if none */
    lv_image_dsc_t  favicon;        /**< Describes `pixels` to LVGL */
    char            country[48];
    char            language[40];
    char            genre[40];
} entry_t;

/** A favicon on its way to a saved station, or to a search result. */
typedef struct {
    char      uuid[RADIO_UUID_LEN];
    uint32_t  result;  /**< Index into the search results; UINT32_MAX for a saved station */
    uint8_t * pixels;  /**< Set on the worker thread */
} favicon_request_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void    list_load(void);
static void    list_save(void);
static void    list_publish(void);
static void    now_playing_publish(void);
static int32_t saved_find(const char * uuid);

static void entry_describe(entry_t * entry);
static void entry_set_favicon(entry_t * entry, uint8_t * pixels);
static void entry_row(const entry_t * entry, page_radio_station_t * row);
static void entry_row_update(uint32_t index);
static void alarm_stations_publish(void);

static void details_fetch(void);
static void details_done(http_job_t * job);
static void favicon_fetch(entry_t * entry);
static void favicon_work(http_job_t * job);
static void favicon_done(http_job_t * job);
static void search_done(http_job_t * job);
static void results_clear(void);
static void result_favicons_fetch(void);
static void result_favicon_done(http_job_t * job);
static void poll_cb(lv_timer_t * timer);

static void player_start(void);
static void player_stop(void);
static void player_poll(void);

static void station_selected(uint32_t index);
static void play_requested(bool play);
static void volume_changed(int32_t value);
static void station_removed(uint32_t index);
static void station_moved(uint32_t from, uint32_t to);
static void search_requested(page_radio_search_field_t field, const char * query);
static void result_add(uint32_t index);
static void result_remove(uint32_t index);
static void saved_remove(uint32_t index);

/**********************
 *  STATIC VARIABLES
 **********************/

/*Saved on the very first start, by their Radio Browser UUID. The names show
 *only until the directory's own records arrive.*/
static const struct {
    const char * uuid;
    const char * name;
} default_stations[] = {
    {"1c6dcd6f-88c6-4fd4-8191-078435168e85", "BBC Radio 6 Music"},
    {"932eb148-e6f6-11e9-a96c-52543be04c81", "FIP"},
    {"96183962-0601-11e8-ae97-52543be04c81", "ERA Kosmos"},
    {"961e6cac-0601-11e8-ae97-52543be04c81", "NTS Radio 1"},
    {"6a7508a9-27ab-11e8-91bf-52543be04c81", "KEXP 90.3 Seattle, WA"},
    {"9617a958-0601-11e8-ae97-52543be04c81", "Radio Paradise Main Mix"},
    {"960cf833-0601-11e8-ae97-52543be04c81", "SomaFM Groove Salad"},
    {"96466f91-0601-11e8-ae97-52543be04c81", "Radio Swiss Jazz"},
};

static entry_t *            saved[RADIO_STORE_MAX];
static uint32_t             saved_count;
static page_radio_station_t rows[RADIO_STORE_MAX];

static entry_t              results[PAGE_RADIO_RESULT_MAX];
static page_radio_station_t result_rows[PAGE_RADIO_RESULT_MAX];
static bool                 result_saved[PAGE_RADIO_RESULT_MAX];
static uint32_t             result_count;
static uint32_t             search_generation;

static int32_t current = -1;
static bool    playing;
static int32_t volume = 35;

/** The player's change count when last shown, and the track title it gave. */
static uint32_t player_changes = UINT32_MAX;
static char     track[RADIO_PLAYER_TITLE_LEN];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_radio_feed_init(void)
{
    page_radio_set_station_cb(station_selected);
    page_radio_set_play_cb(play_requested);
    page_radio_set_volume_cb(volume_changed);
    page_radio_set_remove_cb(station_removed);
    page_radio_set_move_cb(station_moved);
    page_radio_set_search_cb(search_requested);
    page_radio_set_add_cb(result_add);
    page_radio_set_result_remove_cb(result_remove);

    lv_timer_create(poll_cb, POLL_MS, NULL);

    if(!radio_player_init()) LV_LOG_WARN("radio: no audio output");
    radio_player_set_volume(volume);

    list_load();
    if(saved_count > 0) current = 0;

    ui_radio_feed_republish();
    details_fetch();
}

void ui_radio_feed_republish(void)
{
    list_publish();
    now_playing_publish();
    page_radio_set_state(PAGE_RADIO_STATE_STOPPED, NULL);
    page_radio_set_volume(volume);

    /*Show the player's state again even though it has not changed.*/
    player_changes = UINT32_MAX;
    player_poll();
}

bool ui_radio_feed_play_station(const char * uuid)
{
    int32_t index = saved_find(uuid);
    if(index < 0 || !saved[index]->info.stream_url[0]) return false;

    current = index;
    page_radio_set_current_station((uint32_t)index);
    player_start();
    return playing;
}

void ui_radio_feed_stop(void)
{
    player_stop();
}

int32_t ui_radio_feed_get_volume(void)
{
    return volume;
}

void ui_radio_feed_set_volume(int32_t value)
{
    volume = LV_CLAMP(0, value, 100);
    radio_player_set_volume(volume);
    page_radio_set_volume(volume);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void list_load(void)
{
    char     uuids[RADIO_STORE_MAX][RADIO_UUID_LEN];
    uint32_t count  = 0;
    bool     seeded = !radio_store_has_list();

    if(seeded) {
        for(size_t i = 0; i < sizeof(default_stations) / sizeof(default_stations[0]); i++) {
            lv_strlcpy(uuids[count++], default_stations[i].uuid, RADIO_UUID_LEN);
        }
    }
    else {
        count = radio_store_load_list(uuids, RADIO_STORE_MAX);
    }

    for(uint32_t i = 0; i < count; i++) {
        entry_t * entry = calloc(1, sizeof(entry_t));
        if(!entry) break;

        lv_strlcpy(entry->info.uuid, uuids[i], sizeof(entry->info.uuid));
        entry->known = radio_store_load_station(uuids[i], &entry->info);

        if(!entry->known) {
            const char * name = "Unknown station";
            for(size_t d = 0; d < sizeof(default_stations) / sizeof(default_stations[0]); d++) {
                if(strcmp(default_stations[d].uuid, uuids[i]) == 0) name = default_stations[d].name;
            }
            lv_strlcpy(entry->info.name, name, sizeof(entry->info.name));
        }

        entry_set_favicon(entry, radio_store_load_favicon(uuids[i]));
        entry_describe(entry);
        saved[saved_count++] = entry;
    }

    if(seeded) list_save();
}

static void list_save(void)
{
    const char * uuids[RADIO_STORE_MAX];

    for(uint32_t i = 0; i < saved_count; i++) uuids[i] = saved[i]->info.uuid;

    if(!radio_store_save_list(uuids, saved_count)) {
        LV_LOG_WARN("radio: could not write the station list under " RADIO_STORE_ROOT);
    }
}

static void list_publish(void)
{
    for(uint32_t i = 0; i < saved_count; i++) entry_row(saved[i], &rows[i]);

    page_radio_set_stations(rows, saved_count);
    if(current >= 0) page_radio_set_current_station((uint32_t)current);
    alarm_stations_publish();
}

static void now_playing_publish(void)
{
    if(current < 0) {
        page_radio_set_now_playing(NULL, NULL, NULL);
        return;
    }

    const entry_t * entry = saved[current];
    page_radio_set_now_playing(entry->info.name, track, entry->pixels ? &entry->favicon : NULL);
}

static int32_t saved_find(const char * uuid)
{
    for(uint32_t i = 0; i < saved_count; i++) {
        if(strcmp(saved[i]->info.uuid, uuid) == 0) return (int32_t)i;
    }
    return -1;
}

static void entry_describe(entry_t * entry)
{
    radio_station_country_text(&entry->info, entry->country, sizeof(entry->country));
    radio_station_language_text(&entry->info, entry->language, sizeof(entry->language));
    radio_station_genre_text(&entry->info, entry->genre, sizeof(entry->genre));
}

/** Take ownership of `pixels`, freeing any favicon the entry had. */
static void entry_set_favicon(entry_t * entry, uint8_t * pixels)
{
    free(entry->pixels);
    entry->pixels = pixels;
    lv_memzero(&entry->favicon, sizeof(entry->favicon));

    if(!pixels) return;

    entry->favicon.header.magic  = LV_IMAGE_HEADER_MAGIC;
    entry->favicon.header.cf     = LV_COLOR_FORMAT_ARGB8888;
    entry->favicon.header.w      = RADIO_FAVICON_SIZE;
    entry->favicon.header.h      = RADIO_FAVICON_SIZE;
    entry->favicon.header.stride = RADIO_FAVICON_SIZE * 4;
    entry->favicon.data_size     = RADIO_FAVICON_SIZE * RADIO_FAVICON_SIZE * 4;
    entry->favicon.data          = pixels;
}

static void entry_row(const entry_t * entry, page_radio_station_t * row)
{
    row->name     = entry->info.name;
    row->country  = entry->country;
    row->language = entry->language;
    row->genre    = entry->genre;
    row->favicon  = entry->pixels ? &entry->favicon : NULL;
}

static void entry_row_update(uint32_t index)
{
    entry_row(saved[index], &rows[index]);
    page_radio_update_station(index, &rows[index]);
    if((int32_t)index == current) now_playing_publish();
    alarm_stations_publish();
}

/** The alarm editor's sound menu offers the saved stations, by name. */
static void alarm_stations_publish(void)
{
    page_alarm_station_t stations[RADIO_STORE_MAX];

    for(uint32_t i = 0; i < saved_count; i++) {
        stations[i].uuid = saved[i]->info.uuid;
        stations[i].name = saved[i]->info.name;
    }

    page_alarms_set_stations(stations, saved_count);
}

/**
 * Ask Radio Browser about every saved station it has not described yet, in one
 * request, and fetch the favicons of the ones it has whose picture is not on
 * the card.
 */
static void details_fetch(void)
{
    const char * missing[RADIO_STORE_MAX];
    uint32_t     count = 0;

    for(uint32_t i = 0; i < saved_count; i++) {
        if(!saved[i]->known) missing[count++] = saved[i]->info.uuid;
        else favicon_fetch(saved[i]);
    }

    if(count == 0) return;

    char url[RADIO_STORE_MAX * RADIO_UUID_LEN + 128];
    if(!radio_browser_byuuid_url(missing, count, url, sizeof(url))) return;

    http_job_t * job = http_job_create(url, LIST_MAX_BYTES);
    if(!job) return;

    job->done = details_done;
    http_worker_submit(job, false);
}

static void details_done(http_job_t * job)
{
    if(!job->ok) {
        LV_LOG_WARN("radio: Radio Browser did not answer the station lookup (HTTP %d)", job->response.status);
        return;
    }

    radio_station_t * found = malloc(sizeof(radio_station_t) * RADIO_STORE_MAX);
    if(!found) return;

    uint32_t count = radio_station_parse_list(job->response.body, job->response.len, found, RADIO_STORE_MAX);

    for(uint32_t i = 0; i < count; i++) {
        int32_t index = saved_find(found[i].uuid);
        if(index < 0) continue;

        entry_t * entry = saved[index];
        entry->info  = found[i];
        entry->known = true;
        entry_describe(entry);

        if(!radio_store_save_station(&entry->info)) {
            LV_LOG_WARN("radio: could not write station %s to the card", entry->info.uuid);
        }

        entry_row_update((uint32_t)index);
        favicon_fetch(entry);
    }

    free(found);
}

static void favicon_fetch(entry_t * entry)
{
    const char * url = entry->info.favicon_url;

    if(entry->pixels || entry->favicon_tried) return;
    if(strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return;

    favicon_request_t * request = calloc(1, sizeof(favicon_request_t));
    http_job_t *        job     = request ? http_job_create(url, FAVICON_MAX_BYTES) : NULL;

    if(!job) {
        free(request);
        return;
    }

    lv_strlcpy(request->uuid, entry->info.uuid, sizeof(request->uuid));
    request->result      = UINT32_MAX;
    entry->favicon_tried = true;

    job->user = request;
    job->work = favicon_work;
    job->done = favicon_done;
    http_worker_submit(job, false);
}

/**
 * Worker thread: decode and fit the favicon, and store it if it belongs to a
 * saved station. Nothing from LVGL in here.
 */
static void favicon_work(http_job_t * job)
{
    favicon_request_t * request = job->user;

    if(!job->ok) return;

    request->pixels = radio_favicon_convert((const uint8_t *)job->response.body, job->response.len,
                                            RADIO_FAVICON_SIZE);
    if(request->pixels && request->result == UINT32_MAX) radio_store_save_favicon(request->uuid, request->pixels);
}

static void favicon_done(http_job_t * job)
{
    favicon_request_t * request = job->user;
    int32_t             index   = saved_find(request->uuid);

    if(index < 0) {
        /*Removed while it downloaded: take back what the worker wrote.*/
        if(request->pixels) radio_store_remove_station(request->uuid);
        free(request->pixels);
    }
    else if(!request->pixels) {
        LV_LOG_WARN("radio: no usable favicon for %s at %s (HTTP %d)", saved[index]->info.name,
                    saved[index]->info.favicon_url, job->response.status);
    }
    else {
        entry_set_favicon(saved[index], request->pixels);
        entry_row_update((uint32_t)index);
    }

    free(request);
}

static void search_done(http_job_t * job)
{
    if(job->tag != search_generation || !page_radio_search_is_open()) return;

    if(!job->ok) {
        page_radio_search_set_status("Could not reach Radio Browser");
        return;
    }

    radio_station_t * found = malloc(sizeof(radio_station_t) * PAGE_RADIO_RESULT_MAX);
    if(!found) return;

    result_count = radio_station_parse_list(job->response.body, job->response.len, found, PAGE_RADIO_RESULT_MAX);

    for(uint32_t i = 0; i < result_count; i++) {
        lv_memzero(&results[i], sizeof(results[i]));
        results[i].info  = found[i];
        results[i].known = true;
        entry_describe(&results[i]);
        entry_row(&results[i], &result_rows[i]);
        result_saved[i] = saved_find(found[i].uuid) >= 0;
    }
    free(found);

    page_radio_search_set_results(result_rows, result_saved, result_count);
    page_radio_search_set_status(result_count ? NULL : "No stations found");

    result_favicons_fetch();
}

/** Empty the results -- on the page first, whose rows may be showing the favicons freed here. */
static void results_clear(void)
{
    page_radio_search_set_results(NULL, NULL, 0);

    for(uint32_t i = 0; i < result_count; i++) entry_set_favicon(&results[i], NULL);
    result_count = 0;
}

/** The results' favicons, into memory only. One reaches the card if its station is added. */
static void result_favicons_fetch(void)
{
    for(uint32_t i = 0; i < result_count; i++) {
        const char * url = results[i].info.favicon_url;
        if(strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) continue;

        favicon_request_t * request = calloc(1, sizeof(favicon_request_t));
        http_job_t *        job     = request ? http_job_create(url, FAVICON_MAX_BYTES) : NULL;

        if(!job) {
            free(request);
            return;
        }

        lv_strlcpy(request->uuid, results[i].info.uuid, sizeof(request->uuid));
        request->result = i;

        job->user = request;
        job->tag  = search_generation;
        job->work = favicon_work;
        job->done = result_favicon_done;
        http_worker_submit(job, false);
    }
}

static void result_favicon_done(http_job_t * job)
{
    favicon_request_t * request = job->user;
    uint32_t            index   = request->result;

    /*A favicon for an earlier search, or for results since replaced, is dropped.*/
    if(request->pixels && job->tag == search_generation && index < result_count &&
       strcmp(results[index].info.uuid, request->uuid) == 0) {
        entry_set_favicon(&results[index], request->pixels);
        entry_row(&results[index], &result_rows[index]);
        page_radio_search_update_result(index, &result_rows[index]);
    }
    else {
        free(request->pixels);
    }

    free(request);
}

static void poll_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    http_worker_poll();
    player_poll();
}

static void player_start(void)
{
    const entry_t * entry = saved[current];

    track[0] = '\0';
    now_playing_publish();

    if(!entry->info.stream_url[0]) {
        playing = false;
        radio_player_stop();
        page_radio_set_state(PAGE_RADIO_STATE_ERROR, "Station details not loaded yet");
        return;
    }

    playing = true;
    LV_LOG_USER("radio: play %s <%s>", entry->info.name, entry->info.stream_url);
    radio_player_play(entry->info.stream_url, entry->info.hls);
    page_radio_set_state(PAGE_RADIO_STATE_BUFFERING, "Connecting...");

    /*Radio Browser ranks stations by how often they are played, and asks
     *clients to tell it.*/
    char url[128];
    if(radio_browser_click_url(entry->info.uuid, url, sizeof(url))) {
        http_job_t * job = http_job_create(url, 4096);
        if(job) http_worker_submit(job, false);
    }
}

static void player_stop(void)
{
    if(playing) LV_LOG_USER("radio: stop");

    playing = false;
    radio_player_stop();
    page_radio_set_state(PAGE_RADIO_STATE_STOPPED, NULL);

    if(track[0]) {
        track[0] = '\0';
        if(current >= 0) now_playing_publish();
    }
}

/** Show what the player is doing -- connecting, buffering, playing, or why it stopped -- and the track. */
static void player_poll(void)
{
    radio_player_status_t status;
    uint32_t              changes = radio_player_get_status(&status);

    if(changes == player_changes) return;
    player_changes = changes;
    if(!playing) return;

    switch(status.state) {
        case RADIO_PLAYER_CONNECTING:
            page_radio_set_state(PAGE_RADIO_STATE_BUFFERING, "Connecting...");
            break;
        case RADIO_PLAYER_BUFFERING:
            page_radio_set_state(PAGE_RADIO_STATE_BUFFERING, NULL);
            break;
        case RADIO_PLAYER_PLAYING:
            page_radio_set_state(PAGE_RADIO_STATE_PLAYING, NULL);
            break;
        case RADIO_PLAYER_ERROR:
            playing = false;
            LV_LOG_WARN("radio: %s", status.error);
            page_radio_set_state(PAGE_RADIO_STATE_ERROR, status.error);
            break;
        case RADIO_PLAYER_STOPPED:
        default:
            playing = false;
            page_radio_set_state(PAGE_RADIO_STATE_STOPPED, NULL);
            break;
    }

    if(strcmp(track, status.title) != 0) {
        lv_strlcpy(track, status.title, sizeof(track));
        if(current >= 0) now_playing_publish();
    }
}

static void station_selected(uint32_t index)
{
    if(index >= saved_count) return;

    current = (int32_t)index;
    player_start();
}

static void play_requested(bool play)
{
    if(!play) {
        player_stop();
        return;
    }

    if(current < 0) {
        if(saved_count == 0) {
            player_stop();
            return;
        }
        current = 0;
        page_radio_set_current_station(0);
    }

    player_start();
}

static void volume_changed(int32_t value)
{
    volume = value;
    radio_player_set_volume(value);
}

static void station_removed(uint32_t index)
{
    saved_remove(index);
}

/** Take a saved station off the list and the card, stopping it if it is playing. */
static void saved_remove(uint32_t index)
{
    if(index >= saved_count) return;

    entry_t * entry = saved[index];

    if((int32_t)index == current) {
        player_stop();
        current = -1;
        /*Before the favicon is freed: the artwork may be showing it.*/
        now_playing_publish();
    }
    else if(current > (int32_t)index) {
        current--;
    }

    memmove(&saved[index], &saved[index + 1], (saved_count - index - 1) * sizeof(saved[0]));
    saved_count--;
    alarm_stations_publish();

    list_save();
    radio_store_remove_station(entry->info.uuid);

    for(uint32_t i = 0; i < result_count; i++) {
        if(result_saved[i] && strcmp(results[i].info.uuid, entry->info.uuid) == 0) {
            result_saved[i] = false;
            page_radio_search_set_added(i, false);
        }
    }

    free(entry->pixels);
    free(entry);
}

static void station_moved(uint32_t from, uint32_t to)
{
    if(from >= saved_count || to >= saved_count || from == to) return;

    entry_t * moving = saved[from];

    if(from < to) memmove(&saved[from], &saved[from + 1], (to - from) * sizeof(saved[0]));
    else memmove(&saved[to + 1], &saved[to], (from - to) * sizeof(saved[0]));
    saved[to] = moving;

    if(current == (int32_t)from) current = (int32_t)to;
    else if(from < to && current > (int32_t)from && current <= (int32_t)to) current--;
    else if(from > to && current >= (int32_t)to && current < (int32_t)from) current++;

    list_save();
    alarm_stations_publish();
}

static void search_requested(page_radio_search_field_t field, const char * query)
{
    static const radio_search_field_t fields[PAGE_RADIO_SEARCH_COUNT] = {
        RADIO_SEARCH_NAME, RADIO_SEARCH_TAG, RADIO_SEARCH_COUNTRY,
    };
    char url[512];

    /*Whatever is still on its way for the last search is dropped when it lands.*/
    search_generation++;
    results_clear();

    if(field >= PAGE_RADIO_SEARCH_COUNT ||
       !radio_browser_search_url(fields[field], query, PAGE_RADIO_RESULT_MAX, url, sizeof(url))) return;

    http_job_t * job = http_job_create(url, LIST_MAX_BYTES);
    if(!job) return;

    job->tag  = search_generation;
    job->done = search_done;

    page_radio_search_set_status("Searching...");
    http_worker_submit(job, true);
}

static void result_add(uint32_t index)
{
    if(index >= result_count) return;

    const radio_station_t * info = &results[index].info;

    if(saved_find(info->uuid) >= 0) {
        result_saved[index] = true;
        page_radio_search_set_added(index, true);
        return;
    }

    if(saved_count >= RADIO_STORE_MAX) {
        page_radio_search_set_status("The station list is full");
        return;
    }

    entry_t * entry = calloc(1, sizeof(entry_t));
    if(!entry) return;

    entry->info  = *info;
    entry->known = true;
    entry_describe(entry);

    if(!radio_store_save_station(&entry->info)) {
        LV_LOG_WARN("radio: could not write station %s to the card", entry->info.uuid);
    }

    /*The result's favicon has usually arrived already: keep a copy rather
     *than download it again.*/
    const uint8_t * shown = results[index].pixels;
    uint8_t *       copy  = shown ? malloc(RADIO_FAVICON_SIZE * RADIO_FAVICON_SIZE * 4) : NULL;
    if(copy) {
        memcpy(copy, shown, RADIO_FAVICON_SIZE * RADIO_FAVICON_SIZE * 4);
        entry_set_favicon(entry, copy);
        radio_store_save_favicon(entry->info.uuid, copy);
    }

    saved[saved_count++] = entry;
    list_save();
    list_publish();

    result_saved[index] = true;
    page_radio_search_set_added(index, true);

    favicon_fetch(entry);
}

/** The tick on a result tapped again: the station comes off the list, without asking. */
static void result_remove(uint32_t index)
{
    if(index >= result_count) return;

    int32_t found = saved_find(results[index].info.uuid);
    if(found < 0) {
        result_saved[index] = false;
        page_radio_search_set_added(index, false);
        return;
    }

    /*Also clears the result's tick. The page's list is the feed's to change here.*/
    saved_remove((uint32_t)found);
    list_publish();
}
