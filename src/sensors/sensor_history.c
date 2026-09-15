/**
 * @file sensor_history.c
 *
 * The file is a header, then each window's current bucket number, its ring's
 * head and every point, as the platform lays them out in memory. It is only
 * ever read back by the same build on the same machine, and a header that
 * does not match the layout is taken as no history at all.
 */

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensor_history.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
  #include <direct.h>
  #define make_dir(path) _mkdir(path)
#else
  #include <sys/stat.h>
  #define make_dir(path) mkdir(path, 0775)
#endif

/*********************
 *      DEFINES
 *********************/

#define FILE_MAGIC   0x31484153u   /**< "SAH1" */
#define PATH_LEN     128

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    int64_t  bucket;   /**< Number of the bucket being filled: milliseconds since the epoch / period; -1 before any */
    uint32_t head;     /**< Where the next finished bucket goes; also the oldest point */
    float    points[SENSOR_METRIC_COUNT][SENSOR_HISTORY_POINTS];
    double   sum[SENSOR_METRIC_COUNT];     /**< The bucket being filled */
    uint32_t count[SENSOR_METRIC_COUNT];
} track_t;

typedef struct {
    uint32_t magic;
    uint32_t ranges;
    uint32_t metrics;
    uint32_t points;
} file_header_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void track_clear(track_t * track);
static void track_close(track_t * track);
static void track_push(track_t * track, const float values[]);
static void dirs_make(const char * path);

/**********************
 *  STATIC VARIABLES
 **********************/

/** Window lengths, in milliseconds so the hour's 37.5 s buckets come out exact. */
static const int64_t window_ms[SENSOR_RANGE_COUNT] = {
    [SENSOR_RANGE_HOUR] = 3600LL * 1000,
    [SENSOR_RANGE_DAY]  = 86400LL * 1000,
    [SENSOR_RANGE_WEEK] = 7LL * 86400 * 1000,
};

static track_t tracks[SENSOR_RANGE_COUNT];
static bool    ready;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void sensor_history_init(void)
{
    for(int r = 0; r < SENSOR_RANGE_COUNT; r++) track_clear(&tracks[r]);
    ready = true;
}

bool sensor_history_add(time_t now, const sensor_reading_t * reading)
{
    if(!ready) sensor_history_init();

    int64_t ms     = (int64_t)now * 1000;
    bool    closed = false;

    for(int r = 0; r < SENSOR_RANGE_COUNT; r++) {
        track_t * track  = &tracks[r];
        int64_t   bucket = ms / (window_ms[r] / SENSOR_HISTORY_POINTS);

        if(track->bucket < 0) track->bucket = bucket;

        if(bucket != track->bucket) {
            track_close(track);

            /*Buckets that had no readings at all. A clock set back just carries on.*/
            int64_t gap = bucket - track->bucket - 1;
            if(gap > SENSOR_HISTORY_POINTS) gap = SENSOR_HISTORY_POINTS;

            float empty[SENSOR_METRIC_COUNT];
            for(int m = 0; m < SENSOR_METRIC_COUNT; m++) empty[m] = NAN;
            for(int64_t i = 0; i < gap; i++) track_push(track, empty);

            track->bucket = bucket;
            closed        = true;
        }

        for(int m = 0; m < SENSOR_METRIC_COUNT; m++) {
            float value = reading->values[m];
            if(isnan(value)) continue;
            track->sum[m] += value;
            track->count[m]++;
        }
    }

    return closed;
}

void sensor_history_get(sensor_range_t range, sensor_metric_t metric, float out[])
{
    if(!ready) sensor_history_init();
    if(range >= SENSOR_RANGE_COUNT || metric >= SENSOR_METRIC_COUNT) range = SENSOR_RANGE_HOUR;

    const track_t * track = &tracks[range];

    /*The newest finished buckets, oldest first, then the one being filled.*/
    for(uint32_t i = 0; i + 1 < SENSOR_HISTORY_POINTS; i++) {
        out[i] = track->points[metric][(track->head + 1 + i) % SENSOR_HISTORY_POINTS];
    }
    out[SENSOR_HISTORY_POINTS - 1] =
        track->count[metric] ? (float)(track->sum[metric] / track->count[metric]) : NAN;
}

uint32_t sensor_history_window(sensor_range_t range)
{
    return (uint32_t)(window_ms[range < SENSOR_RANGE_COUNT ? range : 0] / 1000);
}

bool sensor_history_load(const char * path)
{
    sensor_history_init();

    FILE * file = fopen(path, "rb");
    if(!file) return false;

    file_header_t header;
    bool          ok = fread(&header, sizeof(header), 1, file) == 1 && header.magic == FILE_MAGIC &&
                       header.ranges == SENSOR_RANGE_COUNT && header.metrics == SENSOR_METRIC_COUNT &&
                       header.points == SENSOR_HISTORY_POINTS;

    for(int r = 0; ok && r < SENSOR_RANGE_COUNT; r++) {
        track_t * track = &tracks[r];
        ok = fread(&track->bucket, sizeof(track->bucket), 1, file) == 1 &&
             fread(&track->head, sizeof(track->head), 1, file) == 1 &&
             fread(track->points, sizeof(track->points), 1, file) == 1 &&
             track->head < SENSOR_HISTORY_POINTS;
    }

    fclose(file);

    if(!ok) sensor_history_init();
    return ok;
}

bool sensor_history_save(const char * path)
{
    if(!ready) sensor_history_init();

    dirs_make(path);

    FILE * file = fopen(path, "wb");
    if(!file) return false;

    file_header_t header = {FILE_MAGIC, SENSOR_RANGE_COUNT, SENSOR_METRIC_COUNT, SENSOR_HISTORY_POINTS};
    bool          ok     = fwrite(&header, sizeof(header), 1, file) == 1;

    for(int r = 0; ok && r < SENSOR_RANGE_COUNT; r++) {
        const track_t * track = &tracks[r];
        ok = fwrite(&track->bucket, sizeof(track->bucket), 1, file) == 1 &&
             fwrite(&track->head, sizeof(track->head), 1, file) == 1 &&
             fwrite(track->points, sizeof(track->points), 1, file) == 1;
    }

    return fclose(file) == 0 && ok;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void track_clear(track_t * track)
{
    track->bucket = -1;
    track->head   = 0;

    for(int m = 0; m < SENSOR_METRIC_COUNT; m++) {
        for(int p = 0; p < SENSOR_HISTORY_POINTS; p++) track->points[m][p] = NAN;
        track->sum[m]   = 0.0;
        track->count[m] = 0;
    }
}

/** Finish the bucket being filled: its average becomes the newest point. */
static void track_close(track_t * track)
{
    float values[SENSOR_METRIC_COUNT];

    for(int m = 0; m < SENSOR_METRIC_COUNT; m++) {
        values[m]       = track->count[m] ? (float)(track->sum[m] / track->count[m]) : NAN;
        track->sum[m]   = 0.0;
        track->count[m] = 0;
    }

    track_push(track, values);
}

static void track_push(track_t * track, const float values[])
{
    for(int m = 0; m < SENSOR_METRIC_COUNT; m++) track->points[m][track->head] = values[m];
    track->head = (track->head + 1) % SENSOR_HISTORY_POINTS;
}

/** Create every directory above the file. Ones that exist are fine. */
static void dirs_make(const char * path)
{
    char partial[PATH_LEN];
    size_t len = strlen(path);
    if(len >= sizeof(partial)) return;

    memcpy(partial, path, len + 1);

    for(size_t i = 1; i < len; i++) {
        if(partial[i] != '/') continue;
        partial[i] = '\0';
        make_dir(partial);
        partial[i] = '/';
    }
}
