/**
 * @file sensor_hw_esp.c
 *
 * The clock's sensors on I2C: a Sensirion SEN69C (particles, VOC and NOx
 * indices, formaldehyde, CO2) and an SHT45 (temperature, humidity).
 *
 * TODO: untested on the clock. Both parts sit on the board's I2C bus, the one
 * the BSP brings up for the codec and touch panel (SDA 7, SCL 8), taken here
 * with bsp_i2c_get_handle(). The command codes, word layouts and scalings are
 * from Sensirion's SEN6x and SHT4x datasheets; check them against the
 * revisions of the parts fitted, the SEN69C's read command above all.
 */

#if defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "sensors/sensor_hw.h"

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>

/*********************
 *      DEFINES
 *********************/

#define SEN69C_ADDRESS 0x6B
#define SHT45_ADDRESS  0x44

#define I2C_SPEED_HZ   100000
#define I2C_TIMEOUT_MS 100

#define SEN_START_MEASUREMENT 0x0021   /**< Then 50 ms before the next command */
#define SEN_READ_VALUES       0x04B5   /**< SEN69C: ten words, then 20 ms before reading */
#define SEN_WORDS             10

#define SHT_MEASURE_PRECISE   0xFD     /**< High repeatability; done within 10 ms */

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool    device_add(uint8_t address, i2c_master_dev_handle_t * device);
static bool    air_read(sensor_reading_t * out);
static bool    climate_read(sensor_reading_t * out);
static bool    sen_command(uint16_t command, uint32_t wait_ms);
static uint8_t crc8(const uint8_t * data, size_t len);

/**********************
 *  STATIC VARIABLES
 **********************/

static i2c_master_dev_handle_t sen;
static i2c_master_dev_handle_t sht;
static bool                    sen_measuring;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

uint32_t sensor_hw_read(sensor_reading_t * out)
{
    uint32_t parts = 0;

    if(air_read(out)) parts |= SENSOR_HW_AIR;
    if(climate_read(out)) parts |= SENSOR_HW_CLIMATE;

    return parts;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static bool device_add(uint8_t address, i2c_master_dev_handle_t * device)
{
    if(*device) return true;
    if(bsp_i2c_init() != ESP_OK) return false;

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = address,
        .scl_speed_hz    = I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, device) == ESP_OK;
}

static bool air_read(sensor_reading_t * out)
{
    if(!device_add(SEN69C_ADDRESS, &sen)) return false;

    if(!sen_measuring) {
        if(!sen_command(SEN_START_MEASUREMENT, 50)) return false;
        sen_measuring = true;
    }

    /*Each word is followed by its CRC.*/
    uint8_t  buf[SEN_WORDS * 3];
    uint16_t w[SEN_WORDS];

    if(!sen_command(SEN_READ_VALUES, 20) ||
       i2c_master_receive(sen, buf, sizeof(buf), I2C_TIMEOUT_MS) != ESP_OK) {
        /*Perhaps it lost power: start it again next time.*/
        sen_measuring = false;
        return false;
    }

    for(int i = 0; i < SEN_WORDS; i++) {
        if(crc8(&buf[i * 3], 2) != buf[i * 3 + 2]) return false;
        w[i] = (uint16_t)((buf[i * 3] << 8) | buf[i * 3 + 1]);
    }

    /*Unknown values -- warming up, or not measured yet -- read as all ones.*/
    out->values[SENSOR_PM1]  = w[0] == 0xFFFF ? NAN : w[0] / 10.0f;
    out->values[SENSOR_PM25] = w[1] == 0xFFFF ? NAN : w[1] / 10.0f;
    out->values[SENSOR_PM4]  = w[2] == 0xFFFF ? NAN : w[2] / 10.0f;
    out->values[SENSOR_PM10] = w[3] == 0xFFFF ? NAN : w[3] / 10.0f;
    /*w[4] and w[5] are the SEN69C's humidity and temperature: the SHT45's are used instead.*/
    out->values[SENSOR_VOC]  = w[6] == 0x7FFF ? NAN : (int16_t)w[6] / 10.0f;
    out->values[SENSOR_NOX]  = w[7] == 0x7FFF ? NAN : (int16_t)w[7] / 10.0f;
    out->values[SENSOR_HCHO] = w[8] == 0xFFFF ? NAN : w[8] / 10.0f;
    out->values[SENSOR_CO2]  = w[9] == 0xFFFF ? NAN : (float)w[9];

    return true;
}

static bool climate_read(sensor_reading_t * out)
{
    if(!device_add(SHT45_ADDRESS, &sht)) return false;

    uint8_t command = SHT_MEASURE_PRECISE;
    uint8_t buf[6];

    if(i2c_master_transmit(sht, &command, 1, I2C_TIMEOUT_MS) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if(i2c_master_receive(sht, buf, sizeof(buf), I2C_TIMEOUT_MS) != ESP_OK) return false;
    if(crc8(buf, 2) != buf[2] || crc8(&buf[3], 2) != buf[5]) return false;

    uint16_t t_raw  = (uint16_t)((buf[0] << 8) | buf[1]);
    uint16_t rh_raw = (uint16_t)((buf[3] << 8) | buf[4]);
    float    rh     = -6.0f + 125.0f * rh_raw / 65535.0f;

    out->values[SENSOR_TEMPERATURE] = -45.0f + 175.0f * t_raw / 65535.0f;
    out->values[SENSOR_HUMIDITY]    = rh < 0.0f ? 0.0f : rh > 100.0f ? 100.0f : rh;

    return true;
}

static bool sen_command(uint16_t command, uint32_t wait_ms)
{
    uint8_t buf[2] = {(uint8_t)(command >> 8), (uint8_t)command};

    if(i2c_master_transmit(sen, buf, sizeof(buf), I2C_TIMEOUT_MS) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
    return true;
}

/** Sensirion's CRC-8: polynomial 0x31, starting from 0xFF. */
static uint8_t crc8(const uint8_t * data, size_t len)
{
    uint8_t crc = 0xFF;

    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int bit = 0; bit < 8; bit++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

#endif
