/**
 * @file audio_sink_esp.c
 *
 * The clock's audio output: the ES8311 codec and NS4150B amplifier on the
 * Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, driven through esp_codec_dev.
 *
 * TODO: untested on the clock. Add the board's BSP component,
 * waveshare/esp32_p4_wifi6_touch_lcd_7b, which sets up I2S (MCLK 13, BCLK 12,
 * WS 10, DOUT 9), the codec on I2C (SDA 7, SCL 8) and the amplifier enable
 * (GPIO 53), and brings in espressif/esp_codec_dev.
 */

#if defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "audio/audio_sink.h"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

/*********************
 *      DEFINES
 *********************/

/** Frames mixed down to mono at a time, on the stack. */
#define BLOCK_FRAMES 512

/**********************
 *  STATIC VARIABLES
 **********************/

static esp_codec_dev_handle_t speaker;
static bool                   open;
static uint32_t               rate;
static uint8_t                channels;
static int32_t                volume = 100;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool audio_sink_init(void)
{
    speaker = bsp_audio_codec_speaker_init();
    return speaker != NULL;
}

bool audio_sink_configure(uint32_t sample_rate, uint8_t channel_count)
{
    if(!speaker || channel_count == 0 || channel_count > 2) return false;

    channels = channel_count;
    if(open && rate == sample_rate) return true;

    if(open) esp_codec_dev_close(speaker);

    /*One speaker: the codec is always fed mono, mixed down in write.*/
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel         = 1,
        .sample_rate     = sample_rate,
    };

    open = esp_codec_dev_open(speaker, &format) == ESP_CODEC_DEV_OK;
    rate = open ? sample_rate : 0;
    if(open) esp_codec_dev_set_out_vol(speaker, volume);
    return open;
}

void audio_sink_write(const int16_t * pcm, size_t frames)
{
    int16_t block[BLOCK_FRAMES];

    if(!open) return;

    while(frames > 0) {
        size_t n = frames < BLOCK_FRAMES ? frames : BLOCK_FRAMES;

        for(size_t i = 0; i < n; i++) {
            block[i] = channels == 2 ? (int16_t)((pcm[2 * i] + pcm[2 * i + 1]) / 2) : pcm[i];
        }
        esp_codec_dev_write(speaker, block, (int)(n * sizeof(int16_t)));

        pcm += n * channels;
        frames -= n;
    }
}

uint32_t audio_sink_queued_ms(void)
{
    /*esp_codec_dev_write() blocks while the I2S buffers are full, which paces
     *the player by itself.*/
    return 0;
}

void audio_sink_clear(void)
{
    /*The I2S buffers hold a few tens of milliseconds, gone before anyone
     *notices.*/
}

void audio_sink_set_volume(int32_t percent)
{
    volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if(open) esp_codec_dev_set_out_vol(speaker, volume);
}

#endif
