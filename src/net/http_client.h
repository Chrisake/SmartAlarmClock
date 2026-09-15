/**
 * @file http_client.h
 *
 * A single blocking HTTP or HTTPS GET into memory, read through http_stream.
 *
 * Blocking on purpose: it only ever runs on background threads -- http_worker's,
 * the radio player's -- never on the LVGL thread.
 *
 * Plain C with no LVGL, so it is safe to call off the LVGL thread.
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "net/http_stream.h"

#include <stdbool.h>
#include <stddef.h>

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    int    status;   /**< HTTP status, 0 if no response arrived */
    char * body;     /**< malloc'd and NUL-terminated; NULL unless the GET succeeded */
    size_t len;      /**< Bytes in body, not counting the terminator */
} http_response_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Fetch a URL.
 * @param url         http:// or https:// URL
 * @param max_bytes   give up on bodies larger than this
 * @param out         receives the status and, on success, the body
 * @return            true on a 2xx response whose whole body fit in max_bytes
 */
bool http_get(const char * url, size_t max_bytes, http_response_t * out);

/**
 * Free a response's body. Safe on a zeroed or already freed response.
 * @param response   the response
 */
void http_response_free(http_response_t * response);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*HTTP_CLIENT_H*/
