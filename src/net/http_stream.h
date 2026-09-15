/**
 * @file http_stream.h
 *
 * An HTTP or HTTPS GET read as it arrives, for bodies with no end -- a radio
 * stream -- or too big to hold. http_get() is built on it.
 *
 * Blocking: only ever call it from a background thread. The simulator
 * implements it with WinHTTP; on the clock it uses ESP-IDF's esp_http_client
 * with the certificate bundle. Redirects are followed, including from HTTPS to
 * HTTP, which station streams and favicons often need.
 *
 * Plain C with no LVGL.
 */

#ifndef HTTP_STREAM_H
#define HTTP_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

/** Sent with every request. Radio Browser asks its clients to name themselves. */
#define HTTP_USER_AGENT "SmartAlarmClock/1.0"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct http_stream_s http_stream_t;

/** What the response said about itself. */
typedef struct {
    int      status;             /**< HTTP status; 0 if no response arrived */
    char     content_type[64];   /**< e.g. "audio/mpeg"; empty if not given */
    uint32_t icy_metaint;        /**< Audio bytes between ICY metadata blocks; 0 for none */
} http_stream_info_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Connect and read the response headers.
 * @param url    http:// or https:// URL
 * @param icy    ask a radio server to interleave track titles (Icy-MetaData: 1)
 * @param info   receives what the response said, whether or not it succeeded
 * @return       the stream, or NULL if it failed or the status was not 2xx
 */
http_stream_t * http_stream_open(const char * url, bool icy, http_stream_info_t * info);

/**
 * Read what has arrived, waiting for at least one byte.
 * @return   bytes read; 0 at the end of the body; -1 on an error or timeout
 */
int http_stream_read(http_stream_t * stream, char * buf, size_t len);

/** Close the connection and free the stream. Safe with NULL. */
void http_stream_close(http_stream_t * stream);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*HTTP_STREAM_H*/
