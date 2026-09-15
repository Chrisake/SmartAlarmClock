/**
 * @file radio_store.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "radio/radio_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <direct.h>
  #define make_dir(path)   _mkdir(path)
  #define remove_dir(path) _rmdir(path)
#else
  #include <sys/stat.h>
  #include <unistd.h>
  #define make_dir(path)   mkdir(path, 0775)
  #define remove_dir(path) rmdir(path)
#endif

/*********************
 *      DEFINES
 *********************/

#define LIST_PATH      RADIO_STORE_ROOT "/stations.json"
#define STATION_FILE   "station.json"
#define FAVICON_FILE   "favicon.bin"

/** Largest station.json or stations.json read back. */
#define JSON_MAX_BYTES (64 * 1024)

#define PATH_LEN 192

/* LVGL 9's image file header, as lv_image_header_t lays it out: magic, colour
 * format, flags (2 bytes), width, height, stride, reserved (2 bytes each). */
#define IMAGE_HEADER_LEN   12
#define IMAGE_MAGIC        0x19  /* LV_IMAGE_HEADER_MAGIC */
#define IMAGE_CF_ARGB8888  0x10  /* LV_COLOR_FORMAT_ARGB8888 */

#define FAVICON_BYTES ((size_t)RADIO_FAVICON_SIZE * RADIO_FAVICON_SIZE * 4)

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void   make_dirs(const char * path);
static bool   station_path(const char * uuid, const char * file, char * buf, size_t size);
static char * read_file(const char * path, size_t max_bytes, size_t * len);
static bool   write_file(const char * path, const void * data, size_t len);
static bool   write_json(const char * path, const cJSON * root);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool radio_store_has_list(void)
{
    FILE * file = fopen(LIST_PATH, "rb");
    if(!file) return false;
    fclose(file);
    return true;
}

uint32_t radio_store_load_list(char uuids[][RADIO_UUID_LEN], uint32_t max)
{
    size_t len  = 0;
    char * json = read_file(LIST_PATH, JSON_MAX_BYTES, &len);
    if(!json) return 0;

    cJSON * root = cJSON_ParseWithLength(json, len);
    free(json);

    const cJSON * list  = cJSON_GetObjectItemCaseSensitive(root, "stations");
    const cJSON * item  = NULL;
    uint32_t      count = 0;

    cJSON_ArrayForEach(item, list) {
        if(count >= max) break;
        if(!cJSON_IsString(item) || !radio_station_uuid_valid(item->valuestring)) continue;

        bool seen = false;
        for(uint32_t i = 0; i < count && !seen; i++) seen = strcmp(uuids[i], item->valuestring) == 0;
        if(seen) continue;

        snprintf(uuids[count++], RADIO_UUID_LEN, "%s", item->valuestring);
    }

    cJSON_Delete(root);
    return count;
}

bool radio_store_save_list(const char * const uuids[], uint32_t count)
{
    cJSON * root = cJSON_CreateObject();
    cJSON * list = root ? cJSON_AddArrayToObject(root, "stations") : NULL;
    bool    ok   = list != NULL;

    for(uint32_t i = 0; ok && i < count; i++) {
        cJSON * uuid = cJSON_CreateString(uuids[i]);
        ok = uuid && cJSON_AddItemToArray(list, uuid);
    }

    if(ok) {
        make_dirs(RADIO_STORE_ROOT);
        ok = write_json(LIST_PATH, root);
    }

    cJSON_Delete(root);
    return ok;
}

bool radio_store_load_station(const char * uuid, radio_station_t * out)
{
    char path[PATH_LEN];
    if(!station_path(uuid, STATION_FILE, path, sizeof(path))) return false;

    size_t len  = 0;
    char * json = read_file(path, JSON_MAX_BYTES, &len);
    if(!json) return false;

    cJSON *         root = cJSON_ParseWithLength(json, len);
    radio_station_t station;
    bool            ok = radio_station_from_json(root, &station) && strcmp(station.uuid, uuid) == 0;

    cJSON_Delete(root);
    free(json);

    if(ok) *out = station;
    return ok;
}

bool radio_store_save_station(const radio_station_t * station)
{
    char dir[PATH_LEN];
    char path[PATH_LEN];

    if(!station_path(station->uuid, NULL, dir, sizeof(dir))) return false;
    if(!station_path(station->uuid, STATION_FILE, path, sizeof(path))) return false;

    cJSON * root = radio_station_to_json(station);
    if(!root) return false;

    make_dirs(dir);
    bool ok = write_json(path, root);
    cJSON_Delete(root);
    return ok;
}

void radio_store_remove_station(const char * uuid)
{
    static const char * const files[] = {STATION_FILE, STATION_FILE ".tmp", FAVICON_FILE, FAVICON_FILE ".tmp"};
    char path[PATH_LEN];

    /*The UUID becomes part of a path: never act on one that is not a UUID.*/
    if(!radio_station_uuid_valid(uuid)) return;

    for(size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if(station_path(uuid, files[i], path, sizeof(path))) remove(path);
    }
    if(station_path(uuid, NULL, path, sizeof(path))) remove_dir(path);
}

bool radio_store_save_favicon(const char * uuid, const uint8_t * pixels)
{
    char dir[PATH_LEN];
    char path[PATH_LEN];

    if(!station_path(uuid, NULL, dir, sizeof(dir))) return false;
    if(!station_path(uuid, FAVICON_FILE, path, sizeof(path))) return false;

    uint8_t * file = malloc(IMAGE_HEADER_LEN + FAVICON_BYTES);
    if(!file) return false;

    uint32_t size   = RADIO_FAVICON_SIZE;
    uint32_t stride = RADIO_FAVICON_SIZE * 4;
    uint8_t  header[IMAGE_HEADER_LEN] = {
        IMAGE_MAGIC, IMAGE_CF_ARGB8888, 0, 0,
        (uint8_t)size, (uint8_t)(size >> 8),
        (uint8_t)size, (uint8_t)(size >> 8),
        (uint8_t)stride, (uint8_t)(stride >> 8),
        0, 0,
    };

    memcpy(file, header, IMAGE_HEADER_LEN);
    memcpy(file + IMAGE_HEADER_LEN, pixels, FAVICON_BYTES);

    make_dirs(dir);
    bool ok = write_file(path, file, IMAGE_HEADER_LEN + FAVICON_BYTES);
    free(file);
    return ok;
}

uint8_t * radio_store_load_favicon(const char * uuid)
{
    char path[PATH_LEN];
    if(!station_path(uuid, FAVICON_FILE, path, sizeof(path))) return NULL;

    size_t len  = 0;
    char * file = read_file(path, IMAGE_HEADER_LEN + FAVICON_BYTES, &len);
    if(!file) return NULL;

    const uint8_t * h  = (const uint8_t *)file;
    bool            ok = len == IMAGE_HEADER_LEN + FAVICON_BYTES && h[0] == IMAGE_MAGIC &&
                         h[1] == IMAGE_CF_ARGB8888 && (h[4] | h[5] << 8) == RADIO_FAVICON_SIZE &&
                         (h[6] | h[7] << 8) == RADIO_FAVICON_SIZE;

    if(!ok) {
        free(file);
        return NULL;
    }

    /*Hand back the pixels alone, in the same allocation.*/
    memmove(file, file + IMAGE_HEADER_LEN, FAVICON_BYTES);
    return (uint8_t *)file;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** mkdir -p. Directories that already exist are fine. */
static void make_dirs(const char * path)
{
    char partial[PATH_LEN];
    snprintf(partial, sizeof(partial), "%s", path);

    for(char * p = partial + 1; *p; p++) {
        if(*p != '/') continue;
        *p = '\0';
        make_dir(partial);
        *p = '/';
    }
    make_dir(partial);
}

/** A station's directory, or a file in it when `file` is set. */
static bool station_path(const char * uuid, const char * file, char * buf, size_t size)
{
    if(!radio_station_uuid_valid(uuid)) return false;

    int n = file ? snprintf(buf, size, RADIO_STORE_ROOT "/%s/%s", uuid, file)
                 : snprintf(buf, size, RADIO_STORE_ROOT "/%s", uuid);
    return n > 0 && (size_t)n < size;
}

static char * read_file(const char * path, size_t max_bytes, size_t * len)
{
    FILE * file = fopen(path, "rb");
    if(!file) return NULL;

    /*One byte over the limit is read to tell a file that fits from one that does not.*/
    char * buf = malloc(max_bytes + 1);
    size_t n   = buf ? fread(buf, 1, max_bytes + 1, file) : 0;
    fclose(file);

    if(!buf || n == 0 || n > max_bytes) {
        free(buf);
        return NULL;
    }

    buf[n] = '\0';
    *len   = n;
    return buf;
}

/** Write through a temporary file, so a card pulled mid-write keeps the old one. */
static bool write_file(const char * path, const void * data, size_t len)
{
    char temp[PATH_LEN + 8];
    snprintf(temp, sizeof(temp), "%s.tmp", path);

    FILE * file = fopen(temp, "wb");
    if(!file) return false;

    bool ok = fwrite(data, 1, len, file) == len;
    ok      = fclose(file) == 0 && ok;

    if(!ok) {
        remove(temp);
        return false;
    }

    /*rename() will not replace an existing file on Windows.*/
    remove(path);
    return rename(temp, path) == 0;
}

static bool write_json(const char * path, const cJSON * root)
{
    char * text = cJSON_Print(root);
    if(!text) return false;

    bool ok = write_file(path, text, strlen(text));
    cJSON_free(text);
    return ok;
}
