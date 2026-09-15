/**
 * @file face_wake.c
 *
 * One thread, started with the first face_wake_start() and kept for good, so
 * that only it ever holds the camera -- a stop and a start in quick
 * succession cannot have two threads opening it. Stopped, it closes the
 * detector and waits; started, it opens it and looks at a frame per period.
 */

/*********************
 *      INCLUDES
 *********************/

#include "presence/face_wake.h"
#include "presence/face_detector.h"
#include "os/os_port.h"

#include <time.h>

/*********************
 *      DEFINES
 *********************/

/** The model's inference runs on this thread's stack. */
#define STACK_BYTES (32 * 1024)

/** How often a stopped thread looks to see whether it has been started. */
#define IDLE_POLL_MS 200

/** How long after the camera would not start before trying it again. */
#define RETRY_MS 10000

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void     watch(void * arg);
static uint32_t now_ms(void);

/**********************
 *  STATIC VARIABLES
 **********************/

static os_mutex_t * lock;
static bool         thread_started;

/*Under `lock`.*/
static bool     running;
static uint32_t period_ms = 200;
static uint32_t frames_needed = 4;
static bool     woken;
static bool     failed;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool face_wake_start(uint8_t fps, uint8_t frames)
{
    if(!lock) {
        lock = os_mutex_create();
        if(!lock) return false;
    }

    os_mutex_lock(lock);
    period_ms     = 1000U / (fps ? fps : 5);
    frames_needed = frames ? frames : 1;
    if(!running) woken = false;
    running = true;
    os_mutex_unlock(lock);

    if(!thread_started) {
        if(!os_thread_start(watch, NULL, STACK_BYTES)) {
            os_mutex_lock(lock);
            running = false;
            os_mutex_unlock(lock);
            return false;
        }
        thread_started = true;
    }

    return true;
}

void face_wake_stop(void)
{
    if(!lock) return;

    os_mutex_lock(lock);
    running = false;
    woken   = false;
    os_mutex_unlock(lock);
}

bool face_wake_is_running(void)
{
    if(!lock) return false;

    os_mutex_lock(lock);
    bool result = running;
    os_mutex_unlock(lock);
    return result;
}

bool face_wake_take(void)
{
    if(!lock) return false;

    os_mutex_lock(lock);
    bool result = woken;
    woken       = false;
    os_mutex_unlock(lock);
    return result;
}

bool face_wake_failed(void)
{
    if(!lock) return false;

    os_mutex_lock(lock);
    bool result = failed;
    os_mutex_unlock(lock);
    return result;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void watch(void * arg)
{
    (void)arg;

    bool     open   = false;
    uint32_t streak = 0;

    for(;;) {
        os_mutex_lock(lock);
        bool     on     = running;
        uint32_t period = period_ms;
        uint32_t needed = frames_needed;
        os_mutex_unlock(lock);

        if(!on) {
            if(open) face_detector_close();
            open   = false;
            streak = 0;
            os_sleep_ms(IDLE_POLL_MS);
            continue;
        }

        if(!open) {
            open = face_detector_open();

            os_mutex_lock(lock);
            failed = !open;
            os_mutex_unlock(lock);

            if(!open) {
                os_sleep_ms(RETRY_MS);
                continue;
            }
        }

        uint32_t started = now_ms();
        bool     face    = false;

        /*A frame that could not be had breaks the run, as one without a face does.*/
        if(!face_detector_detect(&face)) face = false;
        streak = face ? streak + 1 : 0;

        if(streak >= needed) {
            streak = 0;

            os_mutex_lock(lock);
            if(running) woken = true;
            os_mutex_unlock(lock);
        }

        /*The rest of the period; none if the frame took longer.*/
        uint32_t spent = now_ms() - started;
        if(spent < period) os_sleep_ms(period - spent);
    }
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U);
}
