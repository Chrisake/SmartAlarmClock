/**
 * @file http_worker.h
 *
 * Runs HTTP GETs on background threads and hands the results back on the
 * thread that polls -- the LVGL thread -- so a slow station directory or a
 * dead favicon host never stalls the screen.
 *
 * A job carries a URL and two callbacks. `work`, if set, runs on the worker
 * thread straight after the download, for anything slow that touches no LVGL
 * state: decoding an image, writing it to the SD card. `done` runs later,
 * inside http_worker_poll(), where it is safe to update the UI. The job and
 * its body are freed once `done` returns.
 *
 * LVGL's allocator is not thread-safe, so a `work` callback must not call
 * lv_malloc() or any other LVGL function.
 */

#ifndef HTTP_WORKER_H
#define HTTP_WORKER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "net/http_client.h"

#include <stdbool.h>
#include <stdint.h>

/**********************
 *      TYPEDEFS
 **********************/

typedef struct http_job_t http_job_t;

typedef void (*http_job_cb_t)(http_job_t * job);

struct http_job_t {
    char *          url;
    size_t          max_bytes;
    http_job_cb_t   work;      /**< Worker thread, after the GET; @nullable */
    http_job_cb_t   done;      /**< Inside http_worker_poll(); @nullable */
    void *          user;      /**< Yours; free it in `done` if you allocated it */
    uint32_t        tag;       /**< Yours, e.g. to recognise a search that was superseded */
    bool            ok;        /**< Set before `work`: whether the GET succeeded */
    http_response_t response;  /**< Set before `work` */
    http_job_t *    next;      /**< Queue link, internal */
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @param url         the URL to fetch; copied
 * @param max_bytes   largest body accepted
 * @return            a zeroed job to fill in and submit, or NULL if out of memory
 */
http_job_t * http_job_create(const char * url, size_t max_bytes);

/**
 * Queue a job. It is always delivered to `done`: if the worker threads could
 * not be started, it arrives there straight away with `ok` false.
 * @param job      from http_job_create(); ownership passes to the worker
 * @param urgent   jump the queue, for what the user is waiting on
 */
void http_worker_submit(http_job_t * job, bool urgent);

/**
 * Run `done` for every finished job, then free them. Call regularly from the
 * LVGL thread.
 */
void http_worker_poll(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*HTTP_WORKER_H*/
