/**
 * @file radar_wake.c
 *
 * One thread, started with the first radar_wake_start() and kept for good, so
 * that only it ever holds the sensor. Stopped, it closes the sensor and waits;
 * started, it reads a frame per period and decides whether what changed is
 * worth waking the screen for.
 *
 * Everything the decision needs is kept on that thread; the lock only carries
 * the settings in and the wake, the sensor's state and the room's light out.
 */

/*********************
 *      INCLUDES
 *********************/

#include "presence/radar_wake.h"
#include "presence/radar_sensor.h"
#include "os/os_port.h"

#include <time.h>

/*********************
 *      DEFINES
 *********************/

#define STACK_BYTES (8 * 1024)

/** The module reports about ten times a second; a frame is read per period. */
#define PERIOD_MS 100

/** How often a stopped thread looks to see whether it has been started. */
#define IDLE_POLL_MS 200

/** How long after the sensor would not start before trying it again. */
#define RETRY_MS 10000

/** Frames that may fail in a row before the sensor counts as gone. */
#define FAILURES_MAX 20

/** The room has to have been empty this long for the next person to be new. */
#define GONE_MS 3000

/** After a wake, nothing else wakes the screen for this long. */
#define REARM_MS 4000

/** Frames of distance kept, to see someone coming closer over them. */
#define APPROACH_FRAMES 20

/** How fast a person who stays becomes part of the room's own level: the
 *  weight one frame has in it, so about ten seconds to follow a change. */
#define BASELINE_RISE 0.01f

/** The level follows a quietening room faster than a stirring one, so that
 *  someone settling down is forgotten before they move again. */
#define BASELINE_FALL 0.05f

/** Movement below this is the module's own noise, whatever the level says. */
#define MOTION_FLOOR 4.0f

/** Light: below DARK_LUX the room is dark, above LIT_LUX it is not. Between
 *  them it stays as it was, so a candle cannot flicker the setting. */
#define DARK_LUX 1.0f
#define LIT_LUX  4.0f

/** Frames between light readings: it is an I2C reading, and dusk is slow. */
#define LIGHT_EVERY 20

/** In a dark room, reduced is this much of the sensitivity. */
#define DARK_FACTOR 0.5f

/**********************
 *      TYPEDEFS
 **********************/

/** What the sensitivity works out to, in the units the frames come in. */
typedef struct {
    float    motion_margin;   /**< How far above the room's level movement must be */
    float    approach_cm;     /**< How much nearer someone must get over the window */
    float    range_cm;        /**< Past this, nothing counts */
    uint32_t frames;          /**< Frames in a row before a wake */
} limits_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void     watch(void * arg);
static void     frame_apply(const radar_reading_t * reading, const limits_t * limits, bool may_wake);
static void     forget(void);
static limits_t limits_of(uint8_t sensitivity);
static uint32_t now_ms(void);

/**********************
 *  STATIC VARIABLES
 **********************/

static os_mutex_t * lock;
static bool         thread_started;

/*Under `lock`.*/
static bool                running;
static uint8_t             sensitivity = 50;
static radar_wake_dark_t   dark_mode   = RADAR_WAKE_DARK_REDUCED;
static bool                woken;
static radar_wake_sensor_t sensor_state = RADAR_WAKE_SENSOR_UNKNOWN;
static bool                room_dark;

/*The watching thread's own. What the room has been doing lately.*/
static uint32_t absent_ms;             /**< How long nobody has been seen */
static bool     arrival_armed;         /**< Empty long enough that the next person is a new one */
static float    level;                 /**< The movement the room makes anyway */
static bool     level_known;
static float    distances[APPROACH_FRAMES];
static uint32_t distance_count;
static uint32_t distance_next;
static uint32_t arrival_streak;
static uint32_t motion_streak;
static uint32_t woke_at;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool radar_wake_start(uint8_t value, radar_wake_dark_t dark)
{
    if(!lock) {
        lock = os_mutex_create();
        if(!lock) return false;
    }

    os_mutex_lock(lock);
    sensitivity = value ? value : 50;
    dark_mode   = dark;
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

void radar_wake_stop(void)
{
    if(!lock) return;

    os_mutex_lock(lock);
    running = false;
    woken   = false;
    os_mutex_unlock(lock);
}

bool radar_wake_is_running(void)
{
    if(!lock) return false;

    os_mutex_lock(lock);
    bool result = running;
    os_mutex_unlock(lock);
    return result;
}

bool radar_wake_take(void)
{
    if(!lock) return false;

    os_mutex_lock(lock);
    bool result = woken;
    woken       = false;
    os_mutex_unlock(lock);
    return result;
}

radar_wake_sensor_t radar_wake_sensor(void)
{
    if(!lock) return RADAR_WAKE_SENSOR_UNKNOWN;

    os_mutex_lock(lock);
    radar_wake_sensor_t result = sensor_state;
    os_mutex_unlock(lock);
    return result;
}

bool radar_wake_room_dark(void)
{
    if(!lock) return false;

    os_mutex_lock(lock);
    bool result = room_dark;
    os_mutex_unlock(lock);
    return result;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void watch(void * arg)
{
    (void)arg;

    bool     open     = false;
    uint32_t failures = 0;
    uint32_t frame    = 0;
    bool     dark     = false;

    for(;;) {
        os_mutex_lock(lock);
        bool              on   = running;
        uint8_t           want = sensitivity;
        radar_wake_dark_t mode = dark_mode;
        os_mutex_unlock(lock);

        if(!on) {
            if(open) radar_sensor_close();
            open     = false;
            failures = 0;
            forget();
            os_sleep_ms(IDLE_POLL_MS);
            continue;
        }

        if(!open) {
            open = radar_sensor_open();

            os_mutex_lock(lock);
            sensor_state = open ? RADAR_WAKE_SENSOR_READY : RADAR_WAKE_SENSOR_MISSING;
            os_mutex_unlock(lock);

            if(!open) {
                forget();
                os_sleep_ms(RETRY_MS);
                continue;
            }
            failures = 0;
        }

        uint32_t started = now_ms();

        /*How light the room is, now and then rather than every frame.*/
        if(frame % LIGHT_EVERY == 0) {
            float lux;
            if(radar_sensor_light(&lux)) {
                if(lux <= DARK_LUX)     dark = true;
                else if(lux >= LIT_LUX) dark = false;
            }
            else {
                /*No light sensor: the room counts as lit, so the wake works as
                 *it would with the setting left alone.*/
                dark = false;
            }

            os_mutex_lock(lock);
            room_dark = dark;
            os_mutex_unlock(lock);
        }
        frame++;

        radar_reading_t reading;
        if(!radar_sensor_read(&reading)) {
            if(++failures >= FAILURES_MAX) {
                radar_sensor_close();
                open = false;

                os_mutex_lock(lock);
                sensor_state = RADAR_WAKE_SENSOR_MISSING;
                os_mutex_unlock(lock);

                forget();
                os_sleep_ms(RETRY_MS);
            }
            else {
                os_sleep_ms(PERIOD_MS);
            }
            continue;
        }

        failures = 0;

        /*Dark reduces the sensitivity, or holds the wake back altogether. The
         *frame is still followed either way, so the room's level and who is in
         *it stay right for the moment the light comes back on.*/
        uint8_t effective = want;
        bool    may_wake  = true;

        if(dark) {
            if(mode == RADAR_WAKE_DARK_OFF) may_wake = false;
            else if(mode == RADAR_WAKE_DARK_REDUCED) {
                float reduced = (float)want * DARK_FACTOR;
                effective     = (uint8_t)(reduced < 5.0f ? 5.0f : reduced);
            }
        }

        limits_t limits = limits_of(effective);
        frame_apply(&reading, &limits, may_wake);

        uint32_t spent = now_ms() - started;
        if(spent < PERIOD_MS) os_sleep_ms(PERIOD_MS - spent);
    }
}

/**
 * One frame: follow the room, then see whether anything in it is worth waking
 * the screen for.
 * @param may_wake   false in a dark room the wake is switched off in
 */
static void frame_apply(const radar_reading_t * reading, const limits_t * limits, bool may_wake)
{
    uint32_t now      = now_ms();
    bool     in_range = reading->present && reading->distance_cm <= limits->range_cm;

    /*An empty room. Once it has been empty a while, whoever comes next is a
     *new arrival, and nothing of the last person is remembered.*/
    if(!reading->present) {
        absent_ms += PERIOD_MS;
        arrival_streak = 0;
        motion_streak  = 0;

        if(absent_ms >= GONE_MS) {
            arrival_armed  = true;
            level          = 0.0f;
            level_known    = false;
            distance_count = 0;
            distance_next  = 0;
        }
        return;
    }

    absent_ms = 0;

    /*The distances of the last couple of seconds, to see someone closing in.*/
    float farthest = reading->distance_cm;
    for(uint32_t i = 0; i < distance_count; i++) {
        if(distances[i] > farthest) farthest = distances[i];
    }

    distances[distance_next] = reading->distance_cm;
    distance_next            = (distance_next + 1) % APPROACH_FRAMES;
    if(distance_count < APPROACH_FRAMES) distance_count++;

    /*The room's own level of movement. It follows a frame that is quieter than
     *the level faster than a louder one, and stops following altogether while
     *the frame stands out, so that the movement being judged is never learnt
     *away as it happens.*/
    float above = level_known ? reading->motion - level : reading->motion;
    bool  stands_out = above >= limits->motion_margin && reading->motion >= MOTION_FLOOR;

    if(!level_known) {
        level       = reading->motion;
        level_known = true;
    }
    else if(!stands_out) {
        float weight = reading->motion < level ? BASELINE_FALL : BASELINE_RISE;
        level += (reading->motion - level) * weight;
    }

    if(now - woke_at < REARM_MS) {
        arrival_streak = 0;
        motion_streak  = 0;
        return;
    }

    /*Someone has arrived in a room that was empty.*/
    bool arrived = false;
    if(arrival_armed && in_range) {
        if(++arrival_streak >= limits->frames) arrived = true;
    }
    else {
        arrival_streak = 0;
    }

    /*Someone is coming closer: the distance has fallen by a good stretch
     *across the window, and they are near enough to mean it.*/
    bool approaching = in_range && reading->moving &&
                       farthest - reading->distance_cm >= limits->approach_cm;

    /*Somebody who was already there is doing more than they have been.*/
    bool stirred = false;
    if(in_range && stands_out) {
        if(++motion_streak >= limits->frames) stirred = true;
    }
    else {
        motion_streak = 0;
    }

    if(!arrived && !approaching && !stirred) return;

    /*Whatever it was, it has been noticed: nothing more is new until the room
     *empties again.*/
    arrival_armed  = false;
    arrival_streak = 0;
    motion_streak  = 0;
    woke_at        = now;

    if(!may_wake) return;

    os_mutex_lock(lock);
    if(running) woken = true;
    os_mutex_unlock(lock);
}

/** Forget the room: nothing is known until frames come again. */
static void forget(void)
{
    absent_ms      = 0;
    arrival_armed  = true;
    level          = 0.0f;
    level_known    = false;
    distance_count = 0;
    distance_next  = 0;
    arrival_streak = 0;
    motion_streak  = 0;
}

/**
 * The sensitivity as the frames measure things. Higher sensitivity takes less
 * of a change, and reaches further across the room.
 */
static limits_t limits_of(uint8_t value)
{
    float s = (float)(value < 5 ? 5 : value > 100 ? 100 : value);

    limits_t limits;
    limits.motion_margin = 45.0f - 0.40f * s;   /*43 down to 5 of the module's 0..100*/
    limits.approach_cm   = 90.0f - 0.65f * s;   /*87 cm down to 25 cm*/
    limits.range_cm      = 100.0f + 4.0f * s;   /*1.2 m out to 5 m*/
    limits.frames        = s >= 60.0f ? 2 : 3;
    return limits;
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U);
}
