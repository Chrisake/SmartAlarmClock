/**
 * @file tone_player.c
 *
 * Every tone is a short cycle, computed sample by sample from the time into
 * the cycle. Keeping the time small keeps single-precision phase accurate,
 * however long the alarm rings. Each note fades in and out over a few
 * milliseconds, so nothing clicks where it starts, stops or wraps round.
 *
 * Stopping works like the radio player's change of station: the thread is
 * not waited for. A generation count under `lock` says which thread may
 * still write, and the thread checks it under the same lock before each
 * write, so nothing from a stopped tone reaches the sink afterwards.
 */

/*********************
 *      INCLUDES
 *********************/

#include "audio/tone_player.h"
#include "audio/audio_sink.h"
#include "os/os_port.h"

#include <math.h>
#include <stdlib.h>

/*********************
 *      DEFINES
 *********************/

#define SAMPLE_RATE  22050
#define BLOCK_FRAMES 512

/** How much the simulator's sink is let hold, so a stop is heard at once. */
#define QUEUE_MS 150

#define STACK_SIZE (8 * 1024)

/** Headroom under full scale: the sink's volume does the rest. */
#define LEVEL 0.6f

#define TWO_PI 6.2831853f

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    uint32_t generation;
    uint8_t  tone;
} run_t;

/** One chirp of the birdsong: a sweep from `from` to `to` Hz. */
typedef struct {
    float start;
    float length;
    float from;
    float to;
} chirp_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void  tone_main(void * arg);
static bool  run_live(const run_t * run);
static float tone_sample(uint8_t tone, uint32_t frame);

static float radar(float t);
static float chimes(float t);
static float beacon(float t);
static float signal_beeps(float t);
static float birdsong(float t);

static float fade(float u, float length, float edge);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Seconds each tone takes before it repeats. */
static const float cycle_seconds[TONE_PLAYER_COUNT] = {
    [TONE_PLAYER_RADAR]    = 1.2f,
    [TONE_PLAYER_CHIMES]   = 2.8f,
    [TONE_PLAYER_BEACON]   = 2.0f,
    [TONE_PLAYER_SIGNAL]   = 1.0f,
    [TONE_PLAYER_BIRDSONG] = 3.0f,
};

static const chirp_t chirps[] = {
    {0.00f, 0.07f, 3200.0f, 4800.0f},
    {0.12f, 0.07f, 3300.0f, 5000.0f},
    {0.24f, 0.10f, 4800.0f, 2800.0f},
    {0.90f, 0.05f, 4000.0f, 5200.0f},
    {1.00f, 0.05f, 4000.0f, 5200.0f},
    {1.10f, 0.05f, 4000.0f, 5200.0f},
    {1.20f, 0.12f, 5000.0f, 3000.0f},
    {2.00f, 0.08f, 3600.0f, 4400.0f},
    {2.15f, 0.15f, 4400.0f, 2600.0f},
};

static os_mutex_t * lock;
static uint32_t     generation;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool tone_player_start(uint8_t tone)
{
    if(!lock) lock = os_mutex_create();
    if(!lock) return false;

    run_t * run = malloc(sizeof(*run));
    if(!run) return false;

    os_mutex_lock(lock);
    run->generation = ++generation;
    run->tone       = tone < TONE_PLAYER_COUNT ? tone : TONE_PLAYER_RADAR;
    audio_sink_clear();
    os_mutex_unlock(lock);

    if(!os_thread_start(tone_main, run, STACK_SIZE)) {
        free(run);
        return false;
    }
    return true;
}

void tone_player_stop(void)
{
    if(!lock) return;

    os_mutex_lock(lock);
    generation++;
    audio_sink_clear();
    os_mutex_unlock(lock);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void tone_main(void * arg)
{
    run_t *  run   = arg;
    uint32_t frame = 0;
    int16_t  pcm[BLOCK_FRAMES];

    for(;;) {
        /*On the clock the write blocks, which paces this by itself.*/
        while(audio_sink_queued_ms() > QUEUE_MS) {
            if(!run_live(run)) goto done;
            os_sleep_ms(10);
        }

        for(uint32_t i = 0; i < BLOCK_FRAMES; i++) {
            float s = tone_sample(run->tone, frame++) * LEVEL;
            if(s > 1.0f) s = 1.0f;
            if(s < -1.0f) s = -1.0f;
            pcm[i] = (int16_t)(s * 32767.0f);
        }

        os_mutex_lock(lock);
        bool live = run->generation == generation;
        if(live && audio_sink_configure(SAMPLE_RATE, 1)) audio_sink_write(pcm, BLOCK_FRAMES);
        os_mutex_unlock(lock);

        if(!live) break;
    }

done:
    free(run);
}

static bool run_live(const run_t * run)
{
    os_mutex_lock(lock);
    bool live = run->generation == generation;
    os_mutex_unlock(lock);
    return live;
}

static float tone_sample(uint8_t tone, uint32_t frame)
{
    uint32_t cycle = (uint32_t)(cycle_seconds[tone] * SAMPLE_RATE);
    float    t     = (float)(frame % cycle) / SAMPLE_RATE;

    switch(tone) {
        case TONE_PLAYER_CHIMES:   return chimes(t);
        case TONE_PLAYER_BEACON:   return beacon(t);
        case TONE_PLAYER_SIGNAL:   return signal_beeps(t);
        case TONE_PLAYER_BIRDSONG: return birdsong(t);
        case TONE_PLAYER_RADAR:
        default:                   return radar(t);
    }
}

static float radar(float t)
{
    for(int k = 0; k < 4; k++) {
        float u = t - (float)k * 0.12f;
        if(u < 0.0f || u >= 0.08f) continue;

        float f = 1320.0f;
        return fade(u, 0.08f, 0.005f) * (sinf(TWO_PI * f * u) + 0.3f * sinf(TWO_PI * 2.0f * f * u)) / 1.3f;
    }
    return 0.0f;
}

/** Bell-like: each note's upper partials, which are not harmonic, die away before its fundamental. */
static float chimes(float t)
{
    static const float starts[] = {0.0f, 0.35f, 0.7f, 1.05f};
    static const float notes[]  = {1046.5f, 1318.5f, 1568.0f, 2093.0f};
    float              sum      = 0.0f;

    for(int k = 0; k < 4; k++) {
        float u = t - starts[k];
        if(u < 0.0f) continue;

        float f      = notes[k];
        float attack = u < 0.004f ? u / 0.004f : 1.0f;
        sum += attack * expf(-u / 0.6f) *
               (sinf(TWO_PI * f * u) + 0.5f * expf(-u / 0.15f) * sinf(TWO_PI * 2.76f * f * u) +
                0.25f * expf(-u / 0.08f) * sinf(TWO_PI * 5.4f * f * u));
    }

    /*What is left of the last notes as the cycle wraps round.*/
    float tail = cycle_seconds[TONE_PLAYER_CHIMES] - t;
    if(tail < 0.05f) sum *= tail / 0.05f;

    return sum / 2.2f;
}

static float beacon(float t)
{
    static const float starts[] = {0.0f, 0.5f};
    static const float notes[]  = {880.0f, 1174.7f};

    for(int k = 0; k < 2; k++) {
        float u = t - starts[k];
        if(u < 0.0f || u >= 0.45f) continue;

        float f = notes[k];
        return fade(u, 0.45f, 0.06f) * (sinf(TWO_PI * f * u) + 0.2f * sinf(TWO_PI * 3.0f * f * u)) / 1.2f;
    }
    return 0.0f;
}

/** Nearly square, from its first three odd harmonics: the hard edge of a piezo beeper without the aliasing. */
static float signal_beeps(float t)
{
    for(int k = 0; k < 4; k++) {
        float u = t - (float)k * 0.125f;
        if(u < 0.0f || u >= 0.07f) continue;

        float f = 2400.0f;
        return fade(u, 0.07f, 0.003f) *
               (sinf(TWO_PI * f * u) + sinf(TWO_PI * 3.0f * f * u) / 3.0f + sinf(TWO_PI * 5.0f * f * u) / 5.0f) /
               1.53f;
    }
    return 0.0f;
}

static float birdsong(float t)
{
    for(size_t k = 0; k < sizeof(chirps) / sizeof(chirps[0]); k++) {
        const chirp_t * c = &chirps[k];
        float           u = t - c->start;
        if(u < 0.0f || u >= c->length) continue;

        /*A Hann window, and a phase whose rate sweeps linearly between the two ends.*/
        float window = sinf(TWO_PI * 0.5f * u / c->length);
        float phase  = TWO_PI * (c->from * u + (c->to - c->from) * u * u / (2.0f * c->length));
        return window * window * sinf(phase);
    }
    return 0.0f;
}

/** 1 through a note, ramping from and to 0 over `edge` seconds at either end. */
static float fade(float u, float length, float edge)
{
    if(u < edge) return u / edge;
    if(u > length - edge) return (length - u) / edge;
    return 1.0f;
}
