/**
 * @file os_port.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "os/os_port.h"

#include <stdlib.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <pthread.h>
  #include <time.h>
#endif

/**********************
 *      TYPEDEFS
 **********************/

/** What a new thread is to run, handed across to it. */
typedef struct {
    os_thread_fn_t fn;
    void *         arg;
} start_t;

#if defined(_WIN32)

struct os_mutex_s {
    CRITICAL_SECTION section;
};

struct os_cond_s {
    CONDITION_VARIABLE variable;
};

/**********************
 *   STATIC FUNCTIONS
 **********************/

static DWORD WINAPI thread_main(LPVOID arg)
{
    start_t start = *(start_t *)arg;
    free(arg);
    start.fn(start.arg);
    return 0;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

os_mutex_t * os_mutex_create(void)
{
    os_mutex_t * mutex = malloc(sizeof(*mutex));
    if(mutex) InitializeCriticalSection(&mutex->section);
    return mutex;
}

void os_mutex_delete(os_mutex_t * mutex)
{
    if(!mutex) return;
    DeleteCriticalSection(&mutex->section);
    free(mutex);
}

void os_mutex_lock(os_mutex_t * mutex)
{
    EnterCriticalSection(&mutex->section);
}

void os_mutex_unlock(os_mutex_t * mutex)
{
    LeaveCriticalSection(&mutex->section);
}

os_cond_t * os_cond_create(void)
{
    os_cond_t * cond = malloc(sizeof(*cond));
    if(cond) InitializeConditionVariable(&cond->variable);
    return cond;
}

void os_cond_delete(os_cond_t * cond)
{
    free(cond);
}

void os_cond_wait(os_cond_t * cond, os_mutex_t * mutex)
{
    SleepConditionVariableCS(&cond->variable, &mutex->section, INFINITE);
}

void os_cond_broadcast(os_cond_t * cond)
{
    WakeAllConditionVariable(&cond->variable);
}

bool os_thread_start(os_thread_fn_t fn, void * arg, size_t stack_size)
{
    start_t * start = malloc(sizeof(*start));
    if(!start) return false;

    start->fn  = fn;
    start->arg = arg;

    HANDLE thread = CreateThread(NULL, stack_size, thread_main, start, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if(!thread) {
        free(start);
        return false;
    }

    CloseHandle(thread);
    return true;
}

void os_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}

#else

struct os_mutex_s {
    pthread_mutex_t mutex;
};

struct os_cond_s {
    pthread_cond_t cond;
};

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void * thread_main(void * arg)
{
    start_t start = *(start_t *)arg;
    free(arg);
    start.fn(start.arg);
    return NULL;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

os_mutex_t * os_mutex_create(void)
{
    os_mutex_t * mutex = malloc(sizeof(*mutex));
    if(mutex) pthread_mutex_init(&mutex->mutex, NULL);
    return mutex;
}

void os_mutex_delete(os_mutex_t * mutex)
{
    if(!mutex) return;
    pthread_mutex_destroy(&mutex->mutex);
    free(mutex);
}

void os_mutex_lock(os_mutex_t * mutex)
{
    pthread_mutex_lock(&mutex->mutex);
}

void os_mutex_unlock(os_mutex_t * mutex)
{
    pthread_mutex_unlock(&mutex->mutex);
}

os_cond_t * os_cond_create(void)
{
    os_cond_t * cond = malloc(sizeof(*cond));
    if(cond) pthread_cond_init(&cond->cond, NULL);
    return cond;
}

void os_cond_delete(os_cond_t * cond)
{
    if(!cond) return;
    pthread_cond_destroy(&cond->cond);
    free(cond);
}

void os_cond_wait(os_cond_t * cond, os_mutex_t * mutex)
{
    pthread_cond_wait(&cond->cond, &mutex->mutex);
}

void os_cond_broadcast(os_cond_t * cond)
{
    pthread_cond_broadcast(&cond->cond);
}

bool os_thread_start(os_thread_fn_t fn, void * arg, size_t stack_size)
{
    start_t * start = malloc(sizeof(*start));
    if(!start) return false;

    start->fn  = fn;
    start->arg = arg;

    pthread_attr_t attr;
    pthread_t      thread;

    pthread_attr_init(&attr);
    if(stack_size) pthread_attr_setstacksize(&attr, stack_size);
    int err = pthread_create(&thread, &attr, thread_main, start);
    pthread_attr_destroy(&attr);

    if(err != 0) {
        free(start);
        return false;
    }

    pthread_detach(thread);
    return true;
}

void os_sleep_ms(uint32_t ms)
{
    struct timespec delay = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}

#endif
