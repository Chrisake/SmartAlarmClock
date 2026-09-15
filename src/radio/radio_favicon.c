/**
 * @file radio_favicon.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "radio/radio_favicon.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*stb_image v2.30 (public domain), in third_party/stb. Decoding from memory
 *only, and only the formats favicons come in.*/
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/*********************
 *      DEFINES
 *********************/

/** Refuse, before decoding, anything wider or taller than this. */
#define SOURCE_MAX 4096

/**********************
 *  STATIC PROTOTYPES
 **********************/

static uint8_t * decode(const uint8_t * data, size_t len, int * w, int * h);
static uint8_t * decode_ico(const uint8_t * data, size_t len, int * w, int * h);
static uint8_t * fit(const uint8_t * rgba, int w, int h, uint32_t size);
static uint32_t  read_u16(const uint8_t * p);
static uint32_t  read_u32(const uint8_t * p);
static void      write_u32(uint8_t * p, uint32_t value);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

uint8_t * radio_favicon_convert(const uint8_t * data, size_t len, uint32_t size)
{
    int w = 0;
    int h = 0;

    if(!data || len < 8 || size == 0) return NULL;

    bool      ico  = data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0;
    uint8_t * rgba = ico ? decode_ico(data, len, &w, &h) : decode(data, len, &w, &h);
    if(!rgba) return NULL;

    uint8_t * out = fit(rgba, w, h, size);
    stbi_image_free(rgba);
    return out;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint8_t * decode(const uint8_t * data, size_t len, int * w, int * h)
{
    int channels = 0;

    if(len > 0x7FFFFFFF) return NULL;
    if(!stbi_info_from_memory(data, (int)len, w, h, &channels)) return NULL;
    if(*w <= 0 || *h <= 0 || *w > SOURCE_MAX || *h > SOURCE_MAX) return NULL;

    return stbi_load_from_memory(data, (int)len, w, h, &channels, 4);
}

/** The largest picture in an .ico file. */
static uint8_t * decode_ico(const uint8_t * data, size_t len, int * w, int * h)
{
    static const uint8_t png_magic[] = {0x89, 'P', 'N', 'G'};

    const uint8_t * best       = NULL;
    uint32_t        best_len   = 0;
    uint32_t        best_score = 0;
    uint32_t        count      = read_u16(data + 4);

    for(uint32_t i = 0; i < count; i++) {
        size_t entry = 6 + 16 * (size_t)i;
        if(entry + 16 > len) break;

        uint32_t width  = data[entry] ? data[entry] : 256;
        uint32_t bpp    = read_u16(data + entry + 6);
        uint32_t bytes  = read_u32(data + entry + 8);
        uint32_t offset = read_u32(data + entry + 12);
        uint32_t score  = width * 64 + bpp;

        if(offset >= len || bytes > len - offset || score <= best_score) continue;

        best       = data + offset;
        best_len   = bytes;
        best_score = score;
    }

    if(!best || best_len < 40) return NULL;
    if(memcmp(best, png_magic, sizeof(png_magic)) == 0) return decode(best, best_len, w, h);

    /*A BMP without its file header, twice as tall as the icon: the colour
     *rows, then a one-bit transparency mask. Put a file header back and halve
     *the height, and stb_image reads the colour rows as an ordinary BMP.*/
    uint32_t header_len = read_u32(best);
    uint32_t bpp        = read_u16(best + 14);
    uint32_t colors     = read_u32(best + 32);

    if(header_len < 40 || header_len > best_len) return NULL;
    if(bpp <= 8 && colors == 0) colors = 1u << bpp;

    size_t    total = 14 + (size_t)best_len;
    uint8_t * bmp   = malloc(total);
    if(!bmp) return NULL;

    bmp[0] = 'B';
    bmp[1] = 'M';
    write_u32(bmp + 2, (uint32_t)total);
    write_u32(bmp + 6, 0);
    write_u32(bmp + 10, 14 + header_len + (bpp <= 8 ? colors * 4 : 0));
    memcpy(bmp + 14, best, best_len);

    int32_t height = (int32_t)read_u32(bmp + 14 + 8);
    write_u32(bmp + 14 + 8, (uint32_t)(height / 2));

    uint8_t * rgba = decode(bmp, total, w, h);
    free(bmp);
    return rgba;
}

/**
 * Fit an RGBA picture into a size x size square of B, G, R, A, keeping its
 * shape. Shrinking averages every source pixel under each target pixel;
 * growing, as a 32 px favicon does, interpolates between neighbours. Both
 * weight colour by alpha, so transparent edges do not bleed dark.
 */
static uint8_t * fit(const uint8_t * rgba, int w, int h, uint32_t size)
{
    uint8_t * out = calloc((size_t)size * size, 4);
    if(!out) return NULL;

    uint32_t dw = w >= h ? size : (uint32_t)((uint64_t)size * (uint32_t)w / (uint32_t)h);
    uint32_t dh = h >= w ? size : (uint32_t)((uint64_t)size * (uint32_t)h / (uint32_t)w);
    if(dw == 0) dw = 1;
    if(dh == 0) dh = 1;

    uint32_t ox     = (size - dw) / 2;
    uint32_t oy     = (size - dh) / 2;
    bool     shrink = dw < (uint32_t)w;

    for(uint32_t y = 0; y < dh; y++) {
        for(uint32_t x = 0; x < dw; x++) {
            double r = 0, g = 0, b = 0, a = 0, weight = 0;

            if(shrink) {
                uint32_t sx0 = x * (uint32_t)w / dw;
                uint32_t sx1 = ((x + 1) * (uint32_t)w + dw - 1) / dw;
                uint32_t sy0 = y * (uint32_t)h / dh;
                uint32_t sy1 = ((y + 1) * (uint32_t)h + dh - 1) / dh;

                if(sx1 > (uint32_t)w) sx1 = (uint32_t)w;
                if(sy1 > (uint32_t)h) sy1 = (uint32_t)h;

                for(uint32_t sy = sy0; sy < sy1; sy++) {
                    for(uint32_t sx = sx0; sx < sx1; sx++) {
                        const uint8_t * p     = rgba + ((size_t)sy * (uint32_t)w + sx) * 4;
                        double          alpha = p[3];

                        r += p[0] * alpha;
                        g += p[1] * alpha;
                        b += p[2] * alpha;
                        a += alpha;
                        weight += 1;
                    }
                }
            }
            else {
                double fx = (x + 0.5) * w / dw - 0.5;
                double fy = (y + 0.5) * h / dh - 0.5;
                if(fx < 0) fx = 0;
                if(fy < 0) fy = 0;

                int    x0 = (int)fx;
                int    y0 = (int)fy;
                int    x1 = x0 + 1 < w ? x0 + 1 : w - 1;
                int    y1 = y0 + 1 < h ? y0 + 1 : h - 1;
                double tx = fx - x0;
                double ty = fy - y0;

                const int    xs[4] = {x0, x1, x0, x1};
                const int    ys[4] = {y0, y0, y1, y1};
                const double ws[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};

                for(int i = 0; i < 4; i++) {
                    const uint8_t * p     = rgba + ((size_t)ys[i] * (uint32_t)w + (uint32_t)xs[i]) * 4;
                    double          alpha = p[3] * ws[i];

                    r += p[0] * alpha;
                    g += p[1] * alpha;
                    b += p[2] * alpha;
                    a += alpha;
                }
                weight = 1;
            }

            uint8_t * q = out + ((size_t)(oy + y) * size + ox + x) * 4;
            if(a > 0 && weight > 0) {
                q[0] = (uint8_t)(b / a + 0.5);
                q[1] = (uint8_t)(g / a + 0.5);
                q[2] = (uint8_t)(r / a + 0.5);
                q[3] = (uint8_t)(a / weight + 0.5);
            }
        }
    }

    return out;
}

static uint32_t read_u16(const uint8_t * p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t read_u32(const uint8_t * p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void write_u32(uint8_t * p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
