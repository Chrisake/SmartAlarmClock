/**
 * @file http_client.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "net/http_client.h"

#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** First size of the body buffer, which then doubles up to the caller's limit. */
#define BODY_CHUNK 16384

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool body_reserve(http_response_t * out, size_t * cap, size_t need, size_t max_bytes);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool http_get(const char * url, size_t max_bytes, http_response_t * out)
{
    http_stream_info_t info;
    size_t             cap = 0;

    memset(out, 0, sizeof(*out));

    http_stream_t * stream = http_stream_open(url, false, &info);
    out->status = info.status;
    if(!stream) return false;

    bool ok = body_reserve(out, &cap, BODY_CHUNK < max_bytes ? BODY_CHUNK : max_bytes, max_bytes);

    while(ok) {
        if(out->len == cap) {
            ok = body_reserve(out, &cap, cap + 1, max_bytes);
            if(!ok) break;
        }

        int n = http_stream_read(stream, out->body + out->len, cap - out->len);
        if(n < 0) ok = false;
        else if(n == 0) break;
        else out->len += (size_t)n;
    }

    http_stream_close(stream);

    if(ok) {
        out->body[out->len] = '\0';
    }
    else {
        free(out->body);
        out->body = NULL;
        out->len  = 0;
    }
    return ok;
}

void http_response_free(http_response_t * response)
{
    free(response->body);
    response->body = NULL;
    response->len  = 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Make room for `need` body bytes plus a terminator, within `max_bytes`. */
static bool body_reserve(http_response_t * out, size_t * cap, size_t need, size_t max_bytes)
{
    if(need > max_bytes) return false;
    if(out->body && need <= *cap) return true;

    size_t next = *cap ? *cap : BODY_CHUNK;
    while(next < need) next *= 2;
    if(next > max_bytes) next = max_bytes;

    char * grown = realloc(out->body, next + 1);
    if(!grown) return false;

    out->body = grown;
    *cap      = next;
    return true;
}
