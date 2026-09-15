/**
 * @file os_port.h
 *
 * The little the background work needs from the operating system: a mutex, a
 * condition variable, a detached thread and a sleep.
 *
 * Win32 in the simulator; POSIX threads elsewhere, which ESP-IDF provides on
 * top of FreeRTOS, so the downloads and the radio player run unchanged on the
 * clock. Plain C with no LVGL.
 */

#ifndef OS_PORT_H
#define OS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**********************
 *      TYPEDEFS
 **********************/

typedef struct os_mutex_s os_mutex_t;
typedef struct os_cond_s  os_cond_t;

typedef void (*os_thread_fn_t)(void * arg);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/** @return   a new mutex, or NULL if out of memory */
os_mutex_t * os_mutex_create(void);

/** Free a mutex. Safe with NULL. */
void os_mutex_delete(os_mutex_t * mutex);

void os_mutex_lock(os_mutex_t * mutex);
void os_mutex_unlock(os_mutex_t * mutex);

/** @return   a new condition variable, or NULL if out of memory */
os_cond_t * os_cond_create(void);

/** Free a condition variable. Safe with NULL. */
void os_cond_delete(os_cond_t * cond);

/**
 * Release `mutex`, wait for a broadcast, and take `mutex` again. Can wake
 * without one, so wait in a loop on the condition itself.
 */
void os_cond_wait(os_cond_t * cond, os_mutex_t * mutex);

/** Wake everything waiting on `cond`. */
void os_cond_broadcast(os_cond_t * cond);

/**
 * Start a thread that runs `fn(arg)` and then ends; nothing waits for it.
 * @param stack_size   bytes of stack; 0 for the platform's default
 * @return             false if it could not be started
 */
bool os_thread_start(os_thread_fn_t fn, void * arg, size_t stack_size);

/** Sleep the calling thread. */
void os_sleep_ms(uint32_t ms);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*OS_PORT_H*/
