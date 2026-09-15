/**
 * @file http_worker.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "net/http_worker.h"
#include "os/os_port.h"

#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** Downloads at once. Two keeps a search from waiting behind a slow favicon. */
#define WORKER_THREADS 2

/** Stack for each worker; TLS on the clock needs several kilobytes. */
#define WORKER_STACK (16 * 1024)

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    http_job_t * head;
    http_job_t * tail;
} queue_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool         workers_start(void);
static void         worker_main(void * arg);
static void         queue_push(queue_t * queue, http_job_t * job, bool front);
static http_job_t * queue_pop(queue_t * queue);

/**********************
 *  STATIC VARIABLES
 **********************/

static os_mutex_t * lock;
static os_cond_t *  wake;
static queue_t      pending;
static queue_t      finished;
static bool         started;
static bool         running;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

http_job_t * http_job_create(const char * url, size_t max_bytes)
{
    http_job_t * job = calloc(1, sizeof(*job));
    if(!job) return NULL;

    size_t len = strlen(url) + 1;
    job->url   = malloc(len);
    if(!job->url) {
        free(job);
        return NULL;
    }

    memcpy(job->url, url, len);
    job->max_bytes = max_bytes;
    return job;
}

void http_worker_submit(http_job_t * job, bool urgent)
{
    if(!job) return;

    if(!workers_start()) {
        /*Nowhere to run it: hand it straight back, failed.*/
        if(job->done) job->done(job);
        free(job->url);
        free(job);
        return;
    }

    os_mutex_lock(lock);
    queue_push(&pending, job, urgent);
    os_cond_broadcast(wake);
    os_mutex_unlock(lock);
}

void http_worker_poll(void)
{
    if(!running) return;

    for(;;) {
        os_mutex_lock(lock);
        http_job_t * job = queue_pop(&finished);
        os_mutex_unlock(lock);

        if(!job) break;

        if(job->done) job->done(job);

        http_response_free(&job->response);
        free(job->url);
        free(job);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static bool workers_start(void)
{
    if(started) return running;

    started = true;
    lock    = os_mutex_create();
    wake    = os_cond_create();
    if(!lock || !wake) return false;

    for(int i = 0; i < WORKER_THREADS; i++) {
        if(os_thread_start(worker_main, NULL, WORKER_STACK)) running = true;
    }

    return running;
}

static void worker_main(void * arg)
{
    (void)arg;

    for(;;) {
        os_mutex_lock(lock);
        while(!pending.head) os_cond_wait(wake, lock);
        http_job_t * job = queue_pop(&pending);
        os_mutex_unlock(lock);

        job->ok = http_get(job->url, job->max_bytes, &job->response);
        if(job->work) job->work(job);

        os_mutex_lock(lock);
        queue_push(&finished, job, false);
        os_mutex_unlock(lock);
    }
}

static void queue_push(queue_t * queue, http_job_t * job, bool front)
{
    job->next = NULL;

    if(!queue->head) {
        queue->head = job;
        queue->tail = job;
    }
    else if(front) {
        job->next   = queue->head;
        queue->head = job;
    }
    else {
        queue->tail->next = job;
        queue->tail       = job;
    }
}

static http_job_t * queue_pop(queue_t * queue)
{
    http_job_t * job = queue->head;
    if(!job) return NULL;

    queue->head = job->next;
    if(!queue->head) queue->tail = NULL;
    job->next = NULL;
    return job;
}
