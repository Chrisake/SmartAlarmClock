/**
 * @file radio_player.c
 *
 * Each play starts a session: a reader thread that fills a ring buffer from
 * the network, and a decoder thread that empties it into the audio sink. A
 * session that has been replaced or stopped is simply abandoned -- its threads
 * notice at their next wait, and free it between them -- so changing station
 * never waits for a slow server to let go.
 *
 * Only the current session reaches the sink: the decoder checks under
 * `player_lock`, the same lock that replacing the session takes, so nothing
 * from the old station plays after the change.
 */

/*********************
 *      INCLUDES
 *********************/

#include "audio/radio_player.h"
#include "audio/audio_sink.h"
#include "net/http_client.h"
#include "net/http_stream.h"
#include "os/os_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*minimp3 (CC0) and the Helix AAC decoder (RPSL), in third_party.*/
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "aacdec.h"

/*********************
 *      DEFINES
 *********************/

/** Compressed audio between the network and the decoder: about ten seconds at 192 kbps. */
#define RING_BYTES (256 * 1024)

/** Buffered before playing starts, and again after running dry: over a second at 128 kbps. */
#define PREBUFFER_BYTES (24 * 1024)

/** Network reads, and the decoder's input window. */
#define READ_BYTES  4096
#define INPUT_BYTES (16 * 1024)

/** Decoded samples a frame can produce: HE-AAC doubles AAC's 1024 per channel, in stereo. */
#define PCM_SAMPLES 4096

/** How much the sink is let hold before the decoder waits, and how much is written at once. */
#define QUEUE_MS      250
#define OUTPUT_FRAMES 1024

/** Tries, a second apart, before a stream that will not connect or keeps dropping is given up. */
#define RECONNECTS 3
#define RETRY_MS   1000

/** HLS: largest playlist, segments remembered from one, and how far behind live to start. */
#define PLAYLIST_MAX_BYTES (256 * 1024)
#define SEGMENTS_MAX       64
#define LIVE_EDGE_SEGMENTS 3
#define URL_LEN            768

/** minimp3 decodes with some 16 KB of scratch on the stack. */
#define READER_STACK  (16 * 1024)
#define DECODER_STACK (64 * 1024)

#define TS_PACKET 188

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    FORMAT_UNKNOWN,
    FORMAT_MP3,
    FORMAT_AAC,
} format_t;

typedef struct {
    uint8_t * data;
    size_t    size;
    size_t    head;    /**< Next byte to read */
    size_t    used;
    bool      closed;  /**< The reader has finished: nothing more will come */
} ring_t;

typedef struct {
    char *       url;
    bool         hls;
    os_mutex_t * lock;     /**< Guards everything below */
    os_cond_t *  cond;     /**< Broadcast on any change to it */
    ring_t       ring;
    format_t     format;   /**< What the reader learned from the headers or the transport stream */
    bool         ended;    /**< Stopped, replaced, or given up: both threads should finish */
    int          refs;     /**< The reader, the decoder, and the player while it is current */
} session_t;

typedef struct {
    format_t    format;
    mp3dec_t    mp3;
    HAACDecoder aac;
    bool        aac_synced;
    uint8_t     in[INPUT_BYTES];
    size_t      in_len;
    int16_t     pcm[PCM_SAMPLES];
    bool        configured;
    uint32_t    rate;
    uint8_t     channels;
} decoder_t;

/** Strips the metadata an Icecast or Shoutcast server interleaves every `metaint` bytes. */
typedef struct {
    uint32_t metaint;
    uint32_t until_meta;
    uint32_t meta_left;
    uint32_t meta_len;
    char     meta[16 * 255 + 1];
} icy_t;

/** Pulls the audio out of an MPEG transport stream. */
typedef struct {
    uint8_t packet[TS_PACKET];
    size_t  have;
    int     pmt_pid;
    int     audio_pid;
} ts_t;

typedef struct {
    bool     master;             /**< Lists variants rather than segments */
    bool     ended;              /**< Not live: no more segments will be added */
    bool     encrypted;
    uint32_t target_duration;    /**< Seconds */
    uint32_t count;
    char     variant[URL_LEN];
    struct {
        uint64_t sequence;
        char     url[URL_LEN];
    } segments[SEGMENTS_MAX];
} playlist_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static session_t * session_create(const char * url, bool hls);
static void        session_retain(session_t * s);
static void        session_release(session_t * s);
static void        session_end(session_t * s);
static bool        session_ended(session_t * s);
static void        session_set_format(session_t * s, format_t format);
static format_t    session_format(session_t * s);
static void        session_sleep(session_t * s, uint32_t ms);

static bool   ring_write(session_t * s, const uint8_t * data, size_t len);
static size_t ring_wait(session_t * s, size_t bytes);
static size_t ring_read(session_t * s, uint8_t * out, size_t max);
static bool   ring_empty(session_t * s);
static void   ring_close(session_t * s);

static void status_set(session_t * s, radio_player_state_t state, const char * error);
static void status_set_title(session_t * s, const char * title);

static void     reader_main(void * arg);
static void     icy_read(session_t * s);
static bool     icy_feed(session_t * s, icy_t * icy, const uint8_t * data, size_t len);
static void     icy_title(session_t * s, const char * meta);
static format_t format_from_type(const char * type);

static void hls_read(session_t * s);
static void playlist_parse(const char * text, const char * base, playlist_t * list);
static void url_resolve(const char * base, const char * ref, char * out, size_t size);
static bool hls_segment(session_t * s, const char * url, ts_t * ts, char * buf);
static bool ts_feed(session_t * s, ts_t * ts, const uint8_t * data, size_t len);
static bool ts_packet(session_t * s, ts_t * ts, const uint8_t * p);
static void ts_table(session_t * s, ts_t * ts, int pid, const uint8_t * payload, size_t len);

static void     decoder_main(void * arg);
static bool     decode(session_t * s, decoder_t * d, bool * played);
static bool     decoder_pick(session_t * s, decoder_t * d);
static format_t sniff(const uint8_t * p, size_t len);
static bool     output(session_t * s, decoder_t * d, const int16_t * pcm, size_t frames, uint32_t rate,
                       uint8_t channels);

static bool contains_ci(const char * haystack, const char * needle);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Guards `current` and `status`, and gates the sink. */
static os_mutex_t *          player_lock;
static session_t *           current;
static radio_player_status_t status;
static uint32_t              changes;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool radio_player_init(void)
{
    player_lock = os_mutex_create();
    if(!player_lock) return false;

    return audio_sink_init();
}

void radio_player_play(const char * url, bool hls)
{
    session_t * s = url && url[0] ? session_create(url, hls) : NULL;

    os_mutex_lock(player_lock);
    session_t * old = current;
    current         = s;
    if(old) session_end(old);
    audio_sink_clear();

    memset(&status, 0, sizeof(status));
    status.state = s ? RADIO_PLAYER_CONNECTING : RADIO_PLAYER_ERROR;
    if(!s) snprintf(status.error, sizeof(status.error), "Could not start the stream");
    changes++;
    os_mutex_unlock(player_lock);

    session_release(old);
    if(!s) return;

    bool started = false;

    session_retain(s);
    if(os_thread_start(reader_main, s, READER_STACK)) {
        session_retain(s);
        started = os_thread_start(decoder_main, s, DECODER_STACK);
        if(!started) session_release(s);
    }
    else {
        session_release(s);
    }

    if(!started) {
        status_set(s, RADIO_PLAYER_ERROR, "Could not start the stream");
        session_end(s);
    }
}

void radio_player_stop(void)
{
    if(!player_lock) return;

    os_mutex_lock(player_lock);
    session_t * old = current;
    current         = NULL;
    if(old) session_end(old);
    audio_sink_clear();

    memset(&status, 0, sizeof(status));
    status.state = RADIO_PLAYER_STOPPED;
    changes++;
    os_mutex_unlock(player_lock);

    session_release(old);
}

void radio_player_set_volume(int32_t percent)
{
    audio_sink_set_volume(percent);
}

uint32_t radio_player_get_status(radio_player_status_t * out)
{
    if(!player_lock) {
        memset(out, 0, sizeof(*out));
        return 0;
    }

    os_mutex_lock(player_lock);
    *out         = status;
    uint32_t now = changes;
    os_mutex_unlock(player_lock);
    return now;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static session_t * session_create(const char * url, bool hls)
{
    session_t * s = calloc(1, sizeof(*s));
    if(!s) return NULL;

    size_t len     = strlen(url) + 1;
    s->url         = malloc(len);
    s->ring.data   = malloc(RING_BYTES);
    s->lock        = os_mutex_create();
    s->cond        = os_cond_create();
    s->ring.size   = RING_BYTES;
    s->hls         = hls;
    s->refs        = 1;

    if(!s->url || !s->ring.data || !s->lock || !s->cond) {
        free(s->url);
        free(s->ring.data);
        os_mutex_delete(s->lock);
        os_cond_delete(s->cond);
        free(s);
        return NULL;
    }

    memcpy(s->url, url, len);
    return s;
}

static void session_retain(session_t * s)
{
    os_mutex_lock(s->lock);
    s->refs++;
    os_mutex_unlock(s->lock);
}

static void session_release(session_t * s)
{
    if(!s) return;

    os_mutex_lock(s->lock);
    bool last = --s->refs == 0;
    os_mutex_unlock(s->lock);

    if(!last) return;

    free(s->url);
    free(s->ring.data);
    os_mutex_delete(s->lock);
    os_cond_delete(s->cond);
    free(s);
}

/** Tell both threads to finish; they notice at their next wait or read. */
static void session_end(session_t * s)
{
    os_mutex_lock(s->lock);
    s->ended = true;
    os_cond_broadcast(s->cond);
    os_mutex_unlock(s->lock);
}

static bool session_ended(session_t * s)
{
    os_mutex_lock(s->lock);
    bool ended = s->ended;
    os_mutex_unlock(s->lock);
    return ended;
}

static void session_set_format(session_t * s, format_t format)
{
    if(format == FORMAT_UNKNOWN) return;

    os_mutex_lock(s->lock);
    s->format = format;
    os_mutex_unlock(s->lock);
}

static format_t session_format(session_t * s)
{
    os_mutex_lock(s->lock);
    format_t format = s->format;
    os_mutex_unlock(s->lock);
    return format;
}

/** Sleep, but wake early if the session ends. */
static void session_sleep(session_t * s, uint32_t ms)
{
    for(uint32_t slept = 0; slept < ms && !session_ended(s); slept += 50) os_sleep_ms(50);
}

/** Append to the ring, waiting for room. @return false once the session has ended. */
static bool ring_write(session_t * s, const uint8_t * data, size_t len)
{
    ring_t * r = &s->ring;

    os_mutex_lock(s->lock);
    while(len > 0 && !s->ended) {
        if(r->used == r->size) {
            os_cond_wait(s->cond, s->lock);
            continue;
        }

        size_t tail = (r->head + r->used) % r->size;
        size_t n    = r->size - r->used;
        if(n > r->size - tail) n = r->size - tail;
        if(n > len) n = len;

        memcpy(r->data + tail, data, n);
        r->used += n;
        data += n;
        len -= n;
        os_cond_broadcast(s->cond);
    }
    bool ok = !s->ended;
    os_mutex_unlock(s->lock);
    return ok;
}

/**
 * Wait until `bytes` are buffered, or the reader has finished.
 * @return   bytes buffered; 0 if the session ended or nothing more is coming
 */
static size_t ring_wait(session_t * s, size_t bytes)
{
    if(bytes > s->ring.size) bytes = s->ring.size;

    os_mutex_lock(s->lock);
    while(!s->ended && !s->ring.closed && s->ring.used < bytes) os_cond_wait(s->cond, s->lock);
    size_t used = s->ended ? 0 : s->ring.used;
    os_mutex_unlock(s->lock);
    return used;
}

static size_t ring_read(session_t * s, uint8_t * out, size_t max)
{
    ring_t * r    = &s->ring;
    size_t   done = 0;

    os_mutex_lock(s->lock);
    while(done < max && r->used > 0) {
        size_t n = r->size - r->head;
        if(n > r->used) n = r->used;
        if(n > max - done) n = max - done;

        memcpy(out + done, r->data + r->head, n);
        r->head = (r->head + n) % r->size;
        r->used -= n;
        done += n;
    }
    os_cond_broadcast(s->cond);
    os_mutex_unlock(s->lock);
    return done;
}

/** @return   true if the ring is empty but more is on its way */
static bool ring_empty(session_t * s)
{
    os_mutex_lock(s->lock);
    bool empty = s->ring.used == 0 && !s->ring.closed;
    os_mutex_unlock(s->lock);
    return empty;
}

static void ring_close(session_t * s)
{
    os_mutex_lock(s->lock);
    s->ring.closed = true;
    os_cond_broadcast(s->cond);
    os_mutex_unlock(s->lock);
}

/**
 * Report a session's state, if it is still the current one. An error stays:
 * nothing later from the same session replaces it.
 */
static void status_set(session_t * s, radio_player_state_t state, const char * error)
{
    os_mutex_lock(player_lock);
    if(s == current && status.state != RADIO_PLAYER_ERROR && (status.state != state || error)) {
        status.state = state;
        snprintf(status.error, sizeof(status.error), "%s", error ? error : "");
        changes++;
    }
    os_mutex_unlock(player_lock);
}

static void status_set_title(session_t * s, const char * title)
{
    os_mutex_lock(player_lock);
    if(s == current && strcmp(status.title, title) != 0) {
        snprintf(status.title, sizeof(status.title), "%s", title);
        changes++;
    }
    os_mutex_unlock(player_lock);
}

static void reader_main(void * arg)
{
    session_t * s = arg;

    if(s->hls) hls_read(s);
    else icy_read(s);

    ring_close(s);
    session_release(s);
}

/** An Icecast or Shoutcast stream: one endless response, reconnected if it drops. */
static void icy_read(session_t * s)
{
    char *  buf      = malloc(READ_BYTES);
    icy_t * icy      = malloc(sizeof(icy_t));
    int     failures = 0;

    if(!buf || !icy) {
        status_set(s, RADIO_PLAYER_ERROR, "Out of memory");
        goto done;
    }

    while(!session_ended(s)) {
        http_stream_info_t info;
        http_stream_t *    stream   = http_stream_open(s->url, true, &info);
        size_t             received = 0;

        if(stream) {
            session_set_format(s, format_from_type(info.content_type));
            status_set(s, RADIO_PLAYER_BUFFERING, NULL);

            memset(icy, 0, sizeof(*icy));
            icy->metaint    = info.icy_metaint;
            icy->until_meta = info.icy_metaint;

            for(;;) {
                int n = http_stream_read(stream, buf, READ_BYTES);
                if(n <= 0 || !icy_feed(s, icy, (uint8_t *)buf, (size_t)n)) break;
                received += (size_t)n;
            }
            http_stream_close(stream);
        }

        if(session_ended(s)) break;

        /*One that played for a while before dropping gets its retries back.*/
        if(received > PREBUFFER_BYTES) failures = 0;
        if(++failures > RECONNECTS) {
            status_set(s, RADIO_PLAYER_ERROR, received || stream ? "Stream ended" : "Could not connect");
            break;
        }
        session_sleep(s, RETRY_MS);
    }

done:
    free(icy);
    free(buf);
}

/** Pass the audio on to the ring, and pick the titles out of the metadata. */
static bool icy_feed(session_t * s, icy_t * icy, const uint8_t * data, size_t len)
{
    while(len > 0) {
        if(icy->metaint == 0) return ring_write(s, data, len);

        if(icy->meta_left > 0) {
            size_t n = len < icy->meta_left ? len : icy->meta_left;
            memcpy(icy->meta + icy->meta_len, data, n);
            icy->meta_len += (uint32_t)n;
            icy->meta_left -= (uint32_t)n;
            data += n;
            len -= n;

            if(icy->meta_left == 0) {
                icy->meta[icy->meta_len] = '\0';
                icy_title(s, icy->meta);
                icy->until_meta = icy->metaint;
            }
        }
        else if(icy->until_meta == 0) {
            /*A length byte, in sixteens; nearly always 0, for nothing new.*/
            icy->meta_left = (uint32_t)data[0] * 16u;
            icy->meta_len  = 0;
            data++;
            len--;
            if(icy->meta_left == 0) icy->until_meta = icy->metaint;
        }
        else {
            size_t n = len < icy->until_meta ? len : icy->until_meta;
            if(!ring_write(s, data, n)) return false;
            icy->until_meta -= (uint32_t)n;
            data += n;
            len -= n;
        }
    }
    return true;
}

/** StreamTitle='Artist - Track'; */
static void icy_title(session_t * s, const char * meta)
{
    const char * start = strstr(meta, "StreamTitle='");
    if(!start) return;
    start += strlen("StreamTitle='");

    const char * end = strstr(start, "';");
    if(!end) end = start + strlen(start);

    char   title[RADIO_PLAYER_TITLE_LEN];
    size_t len = (size_t)(end - start);
    if(len >= sizeof(title)) len = sizeof(title) - 1;

    memcpy(title, start, len);
    title[len] = '\0';
    status_set_title(s, title);
}

static format_t format_from_type(const char * type)
{
    /*audio/mpegurl is a playlist, not MPEG audio.*/
    if(contains_ci(type, "mpegurl")) return FORMAT_UNKNOWN;
    if(contains_ci(type, "mpeg") || contains_ci(type, "mp3")) return FORMAT_MP3;
    if(contains_ci(type, "aac")) return FORMAT_AAC;
    return FORMAT_UNKNOWN;
}

/**
 * HLS: fetch the playlist, follow a master playlist to its first variant,
 * start a few segments behind live, and keep fetching new segments as the
 * playlist grows.
 */
static void hls_read(session_t * s)
{
    playlist_t * list         = malloc(sizeof(playlist_t));
    char *       playlist_url = malloc(URL_LEN);
    char *       buf          = malloc(READ_BYTES);
    ts_t         ts           = {.pmt_pid = -1, .audio_pid = -1};
    uint64_t     next         = 0;
    bool         started      = false;
    int          failures     = 0;

    if(!list || !playlist_url || !buf) {
        status_set(s, RADIO_PLAYER_ERROR, "Out of memory");
        goto done;
    }

    snprintf(playlist_url, URL_LEN, "%s", s->url);

    while(!session_ended(s)) {
        http_response_t response;

        if(!http_get(playlist_url, PLAYLIST_MAX_BYTES, &response)) {
            if(++failures > RECONNECTS) {
                status_set(s, RADIO_PLAYER_ERROR, started ? "Stream ended" : "Could not connect");
                break;
            }
            session_sleep(s, RETRY_MS);
            continue;
        }

        playlist_parse(response.body, playlist_url, list);
        http_response_free(&response);

        if(list->master) {
            if(!list->variant[0]) {
                status_set(s, RADIO_PLAYER_ERROR, "Unsupported stream format");
                break;
            }
            snprintf(playlist_url, URL_LEN, "%s", list->variant);
            continue;
        }

        if(list->encrypted) {
            status_set(s, RADIO_PLAYER_ERROR, "Encrypted streams are not supported");
            break;
        }

        if(!started && list->count > 0) {
            uint32_t first = list->count > LIVE_EDGE_SEGMENTS ? list->count - LIVE_EDGE_SEGMENTS : 0;
            next    = list->segments[first].sequence;
            started = true;
            status_set(s, RADIO_PLAYER_BUFFERING, NULL);
        }

        bool fetched = false;
        for(uint32_t i = 0; i < list->count && !session_ended(s); i++) {
            if(list->segments[i].sequence < next) continue;

            if(hls_segment(s, list->segments[i].url, &ts, buf)) failures = 0;
            else failures++;

            next    = list->segments[i].sequence + 1;
            fetched = true;
        }

        if(failures > RECONNECTS) {
            status_set(s, RADIO_PLAYER_ERROR, "Stream ended");
            break;
        }
        if(list->ended && !fetched) break;

        /*Nothing new: look again in half a segment's time.*/
        if(!fetched) session_sleep(s, list->target_duration * 500u);
    }

done:
    free(buf);
    free(playlist_url);
    free(list);
}

static void playlist_parse(const char * text, const char * base, playlist_t * list)
{
    uint64_t sequence       = 0;
    uint64_t index          = 0;
    bool     expect_segment = false;
    bool     expect_variant = false;
    char     line[URL_LEN];

    list->master          = false;
    list->ended           = false;
    list->encrypted       = false;
    list->target_duration = 6;
    list->count           = 0;
    list->variant[0]      = '\0';

    while(*text) {
        const char * end = text;
        while(*end && *end != '\n' && *end != '\r') end++;

        size_t len = (size_t)(end - text);
        if(len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, text, len);
        line[len] = '\0';

        if(strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
            sequence = strtoull(line + 22, NULL, 10);
        }
        else if(strncmp(line, "#EXT-X-TARGETDURATION:", 22) == 0) {
            uint32_t seconds = (uint32_t)strtoul(line + 22, NULL, 10);
            if(seconds > 0) list->target_duration = seconds;
        }
        else if(strncmp(line, "#EXT-X-KEY:", 11) == 0) {
            if(!strstr(line, "METHOD=NONE")) list->encrypted = true;
        }
        else if(strncmp(line, "#EXT-X-ENDLIST", 14) == 0) {
            list->ended = true;
        }
        else if(strncmp(line, "#EXT-X-STREAM-INF", 17) == 0) {
            list->master   = true;
            expect_variant = true;
        }
        else if(strncmp(line, "#EXTINF", 7) == 0) {
            expect_segment = true;
        }
        else if(line[0] != '\0' && line[0] != '#') {
            if(expect_variant && !list->variant[0]) {
                url_resolve(base, line, list->variant, URL_LEN);
            }
            else if(expect_segment) {
                /*A long playlist keeps its newest segments.*/
                if(list->count == SEGMENTS_MAX) {
                    memmove(&list->segments[0], &list->segments[1], (SEGMENTS_MAX - 1) * sizeof(list->segments[0]));
                    list->count--;
                }
                list->segments[list->count].sequence = sequence + index;
                url_resolve(base, line, list->segments[list->count].url, URL_LEN);
                list->count++;
                index++;
            }
            expect_segment = false;
            expect_variant = false;
        }

        text = end;
        while(*text == '\n' || *text == '\r') text++;
    }
}

/** A playlist entry's URL, made absolute against the playlist's own. */
static void url_resolve(const char * base, const char * ref, char * out, size_t size)
{
    if(strncmp(ref, "http://", 7) == 0 || strncmp(ref, "https://", 8) == 0) {
        snprintf(out, size, "%s", ref);
        return;
    }

    const char * scheme   = strstr(base, "://");
    const char * host_end = scheme ? strchr(scheme + 3, '/') : NULL;
    if(!host_end) host_end = base + strlen(base);

    size_t keep = (size_t)(host_end - base);

    if(ref[0] != '/') {
        /*Everything up to the last slash of the path, before any query.*/
        const char * query = strchr(base, '?');
        const char * slash = query ? query : base + strlen(base);
        while(slash > host_end && slash[-1] != '/') slash--;
        if(slash > host_end) keep = (size_t)(slash - base);
    }

    bool separator = ref[0] != '/' && keep == (size_t)(host_end - base);
    snprintf(out, size, "%.*s%s%s", (int)keep, base, separator ? "/" : "", ref);
}

/** Download one segment into the ring: a transport stream demuxed, packed audio as it is. */
static bool hls_segment(session_t * s, const char * url, ts_t * ts, char * buf)
{
    http_stream_info_t info;
    http_stream_t *    stream = http_stream_open(url, false, &info);
    bool               first  = true;
    bool               is_ts  = false;
    bool               ok     = true;

    if(!stream) return false;
    ts->have = 0;

    for(;;) {
        int n = http_stream_read(stream, buf, READ_BYTES);
        if(n < 0) ok = false;
        if(n <= 0) break;

        if(first) {
            is_ts = (uint8_t)buf[0] == 0x47;
            first = false;
        }

        bool more = is_ts ? ts_feed(s, ts, (uint8_t *)buf, (size_t)n) : ring_write(s, (uint8_t *)buf, (size_t)n);
        if(!more) break;
    }

    http_stream_close(stream);
    return ok;
}

static bool ts_feed(session_t * s, ts_t * ts, const uint8_t * data, size_t len)
{
    while(len > 0) {
        /*Find the sync byte first, should the stream have lost it.*/
        if(ts->have == 0 && data[0] != 0x47) {
            data++;
            len--;
            continue;
        }

        size_t n = TS_PACKET - ts->have;
        if(n > len) n = len;
        memcpy(ts->packet + ts->have, data, n);
        ts->have += n;
        data += n;
        len -= n;

        if(ts->have == TS_PACKET) {
            ts->have = 0;
            if(!ts_packet(s, ts, ts->packet)) return false;
        }
    }
    return true;
}

static bool ts_packet(session_t * s, ts_t * ts, const uint8_t * p)
{
    int    pid     = ((p[1] & 0x1F) << 8) | p[2];
    bool   start   = (p[1] & 0x40) != 0;
    int    control = (p[3] >> 4) & 0x03;
    size_t offset  = 4;

    if(control & 0x02) offset += 1u + p[4];
    if(!(control & 0x01) || offset >= TS_PACKET) return true;

    const uint8_t * payload = p + offset;
    size_t          len     = TS_PACKET - offset;

    if(pid == 0 || pid == ts->pmt_pid) {
        if(start) ts_table(s, ts, pid, payload, len);
        return true;
    }
    if(pid != ts->audio_pid) return true;

    if(start) {
        /*A PES header, then the audio.*/
        if(len < 9 || payload[0] != 0 || payload[1] != 0 || payload[2] != 1) return true;

        size_t header = 9u + payload[8];
        if(header >= len) return true;
        payload += header;
        len -= header;
    }

    return ring_write(s, payload, len);
}

/** The program association and program map tables: which packets carry the audio, and in what. */
static void ts_table(session_t * s, ts_t * ts, int pid, const uint8_t * payload, size_t len)
{
    const uint8_t * end   = payload + len;
    const uint8_t * table = payload + 1 + payload[0];

    if(table + 12 > end) return;

    size_t          section = ((size_t)(table[1] & 0x0F) << 8) | table[2];
    const uint8_t * last    = table + 3 + section;
    if(last > end) last = end;
    last -= 4;  /*CRC*/

    if(pid == 0 && table[0] == 0x00) {
        for(const uint8_t * e = table + 8; e + 4 <= last; e += 4) {
            int program = (e[0] << 8) | e[1];
            if(program != 0) {
                ts->pmt_pid = ((e[2] & 0x1F) << 8) | e[3];
                break;
            }
        }
    }
    else if(table[0] == 0x02 && ts->audio_pid < 0) {
        size_t info = ((size_t)(table[10] & 0x0F) << 8) | table[11];

        for(const uint8_t * e = table + 12 + info; e + 5 <= last;) {
            int      type     = e[0];
            int      es_pid   = ((e[1] & 0x1F) << 8) | e[2];
            size_t   es_info  = ((size_t)(e[3] & 0x0F) << 8) | e[4];
            format_t format   = type == 0x0F ? FORMAT_AAC : (type == 0x03 || type == 0x04) ? FORMAT_MP3 : FORMAT_UNKNOWN;

            if(format != FORMAT_UNKNOWN) {
                ts->audio_pid = es_pid;
                session_set_format(s, format);
                break;
            }
            e += 5 + es_info;
        }
    }
}

static void decoder_main(void * arg)
{
    session_t * s       = arg;
    decoder_t * d       = calloc(1, sizeof(decoder_t));
    bool        playing = false;

    if(!d) {
        status_set(s, RADIO_PLAYER_ERROR, "Out of memory");
        goto done;
    }
    mp3dec_init(&d->mp3);

    while(!session_ended(s)) {
        /*Fill up before starting, and again after running dry, so a hiccup in
         *the connection becomes one pause rather than a stutter.*/
        if(playing && ring_empty(s)) {
            playing = false;
            status_set(s, RADIO_PLAYER_BUFFERING, NULL);
        }
        if(ring_wait(s, playing ? 1 : PREBUFFER_BYTES) == 0) break;

        d->in_len += ring_read(s, d->in + d->in_len, sizeof(d->in) - d->in_len);

        bool played = false;
        if(!decode(s, d, &played)) break;
        if(played) playing = true;
    }

    /*Ended without being stopped or replaced: the reader gave up, or the
     *stream could not be decoded. Say so, unless one of those already has.*/
    if(!session_ended(s)) status_set(s, RADIO_PLAYER_ERROR, "Stream ended");

done:
    session_end(s);
    if(d && d->aac) AACFreeDecoder(d->aac);
    free(d);
    session_release(s);
}

/**
 * Decode every whole frame in the input window and play it, keeping any
 * partial frame for next time.
 * @param played   set if any audio reached the sink
 * @return         false to give up on the stream
 */
static bool decode(session_t * s, decoder_t * d, bool * played)
{
    size_t pos = 0;

    if(d->format == FORMAT_UNKNOWN && !decoder_pick(s, d)) {
        if(d->in_len < sizeof(d->in)) return true;  /*Not enough to tell yet.*/
        status_set(s, RADIO_PLAYER_ERROR, "Unsupported stream format");
        return false;
    }

    while(pos < d->in_len) {
        size_t left     = d->in_len - pos;
        size_t used     = 0;
        int    frames   = 0;
        int    channels = 0;
        int    rate     = 0;

        if(d->format == FORMAT_MP3) {
            mp3dec_frame_info_t info;
            frames = mp3dec_decode_frame(&d->mp3, d->in + pos, (int)left, d->pcm, &info);
            if(info.frame_bytes == 0) break;  /*Needs more data.*/

            used     = (size_t)info.frame_bytes;
            channels = info.channels;
            rate     = info.hz;
        }
        else {
            if(!d->aac_synced) {
                int sync = AACFindSyncWord(d->in + pos, (int)left);
                if(sync < 0) {
                    /*Keep the last byte: it may be half a sync word.*/
                    pos = d->in_len - 1;
                    break;
                }
                pos += (size_t)sync;
                left -= (size_t)sync;
                d->aac_synced = true;
            }

            /*Helix does not check a frame against the bytes it is given, and
             *reads on past them when a frame is cut short. So read the ADTS
             *header here and hand it exactly one whole frame, or wait.*/
            if(left < 7) break;

            const uint8_t * h     = d->in + pos;
            size_t          frame = ((size_t)(h[3] & 0x03) << 11) | ((size_t)h[4] << 3) | ((size_t)h[5] >> 5);

            /*A sync word can turn up inside a frame's data. Believe one only
             *if the next frame starts where this one says it ends.*/
            bool valid = h[0] == 0xFF && (h[1] & 0xF6) == 0xF0 && frame >= 7;
            if(valid && left >= frame + 2) {
                valid = d->in[pos + frame] == 0xFF && (d->in[pos + frame + 1] & 0xF6) == 0xF0;
            }
            if(!valid) {
                pos++;
                d->aac_synced = false;
                continue;
            }
            if(left < frame) break;

            unsigned char * p     = d->in + pos;
            int             bytes = (int)frame;
            int             err   = AACDecode(d->aac, &p, &bytes, d->pcm);

            used = frame;
            if(err != ERR_AAC_NONE) {
                /*A damaged frame: skip it.*/
                pos += used;
                continue;
            }

            AACFrameInfo info;
            AACGetLastFrameInfo(d->aac, &info);

            channels = info.nChans;
            rate     = info.sampRateOut;
            frames   = info.nChans > 0 ? info.outputSamps / info.nChans : 0;
        }

        pos += used;

        if(frames > 0 && channels > 0 && channels <= 2 && rate > 0) {
            if(!output(s, d, d->pcm, (size_t)frames, (uint32_t)rate, (uint8_t)channels)) return false;
            *played = true;
        }
    }

    /*A full window with no frame in it: drop half and look again.*/
    if(pos == 0 && d->in_len == sizeof(d->in)) pos = sizeof(d->in) / 2;

    memmove(d->in, d->in + pos, d->in_len - pos);
    d->in_len -= pos;
    return true;
}

/** Choose the decoder: from what the reader learned, else from the data itself. */
static bool decoder_pick(session_t * s, decoder_t * d)
{
    format_t format = session_format(s);
    if(format == FORMAT_UNKNOWN) format = sniff(d->in, d->in_len);
    if(format == FORMAT_UNKNOWN) return false;

    if(format == FORMAT_AAC && !d->aac) {
        d->aac = AACInitDecoder();
        if(!d->aac) return false;
    }

    d->format = format;
    return true;
}

static format_t sniff(const uint8_t * p, size_t len)
{
    size_t i = 0;

    /*Packed audio often starts with an ID3 tag.*/
    if(len >= 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3') {
        i = 10u + ((size_t)(p[6] & 0x7F) << 21 | (size_t)(p[7] & 0x7F) << 14 | (size_t)(p[8] & 0x7F) << 7 |
                   (size_t)(p[9] & 0x7F));
    }

    for(; i + 1 < len; i++) {
        if(p[i] != 0xFF) continue;
        if((p[i + 1] & 0xF6) == 0xF0) return FORMAT_AAC;                               /*ADTS: layer 00*/
        if((p[i + 1] & 0xE0) == 0xE0 && (p[i + 1] & 0x06) != 0) return FORMAT_MP3;    /*MPEG audio: layer set*/
    }
    return FORMAT_UNKNOWN;
}

/**
 * Hand decoded audio to the sink, pacing to it. Only the current session gets
 * through.
 * @return   false once this session is no longer the one playing
 */
static bool output(session_t * s, decoder_t * d, const int16_t * pcm, size_t frames, uint32_t rate,
                   uint8_t channels)
{
    while(frames > 0) {
        while(audio_sink_queued_ms() > QUEUE_MS) {
            if(session_ended(s)) return false;
            os_sleep_ms(10);
        }

        size_t n    = frames < OUTPUT_FRAMES ? frames : OUTPUT_FRAMES;
        bool   live = false;

        os_mutex_lock(player_lock);
        if(s == current) {
            live = true;

            if(!d->configured || d->rate != rate || d->channels != channels) {
                d->configured = audio_sink_configure(rate, channels);
                d->rate       = rate;
                d->channels   = channels;
            }

            if(!d->configured) {
                live = false;
                if(status.state != RADIO_PLAYER_ERROR) {
                    status.state = RADIO_PLAYER_ERROR;
                    snprintf(status.error, sizeof(status.error), "No audio output");
                    changes++;
                }
            }
            else {
                audio_sink_write(pcm, n);
                if(status.state != RADIO_PLAYER_PLAYING && status.state != RADIO_PLAYER_ERROR) {
                    status.state    = RADIO_PLAYER_PLAYING;
                    status.error[0] = '\0';
                    changes++;
                }
            }
        }
        os_mutex_unlock(player_lock);

        if(!live) return false;

        pcm += n * channels;
        frames -= n;
    }
    return true;
}

static bool contains_ci(const char * haystack, const char * needle)
{
    size_t n = strlen(needle);

    for(; *haystack; haystack++) {
        size_t i = 0;
        while(i < n && haystack[i]) {
            char a = haystack[i];
            char b = needle[i];
            if(a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if(b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if(a != b) break;
            i++;
        }
        if(i == n) return true;
    }
    return false;
}
