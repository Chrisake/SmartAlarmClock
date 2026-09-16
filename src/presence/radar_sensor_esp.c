/**
 * @file radar_sensor_esp.c
 *
 * The presence board on the clock: an HLK-LD2410 mmWave module on a UART, and
 * a BH1750 ambient light sensor on the board's I2C bus.
 *
 * TODO: untested on the clock. The module is left in its default basic
 * reporting mode, where it sends a frame about ten times a second on its own;
 * nothing is ever configured over the UART, so a module someone has already
 * tuned keeps its tuning. The frame layout below is from HLK's LD2410 serial
 * protocol document; check it against the firmware on the module fitted.
 *
 * Pins come from the board profile, so a board that wires the module
 * elsewhere only changes boards.cmake.
 */

#if defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "presence/radar_sensor.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

#ifndef BOARD_RADAR_UART
  #define BOARD_RADAR_UART 1
#endif
#ifndef BOARD_RADAR_RX_PIN
  #define BOARD_RADAR_RX_PIN 16
#endif
#ifndef BOARD_RADAR_TX_PIN
  #define BOARD_RADAR_TX_PIN 17
#endif

/** The LD2410 leaves the factory at this rate. */
#define UART_BAUD    256000
#define UART_BUF     512

/** Frames come about every 100 ms; wait a little over two of them. */
#define FRAME_WAIT_MS 250

/** Report frame: header, little-endian length, payload, tail. */
static const uint8_t FRAME_HEAD[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t FRAME_TAIL[4] = {0xF8, 0xF7, 0xF6, 0xF5};

/** Payload kinds, and the marks around a target report. */
#define PAYLOAD_BASIC       0x02
#define PAYLOAD_ENGINEERING 0x01
#define TARGET_HEAD         0xAA
#define TARGET_TAIL         0x55

/** Target state, as bits: moving, still, or both. */
#define STATE_MOVING 0x01
#define STATE_STILL  0x02

/** Longest payload worth reading: the engineering report, gate energies and all. */
#define PAYLOAD_MAX 64

/** BH1750: continuous high resolution, 1 lx per count over 1.2. */
#define BH1750_ADDRESS     0x23
#define BH1750_CONTINUOUS  0x10
#define BH1750_FIRST_WAIT  180
#define I2C_SPEED_HZ       100000
#define I2C_TIMEOUT_MS     100

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool frame_read(uint8_t * payload, size_t * len);
static bool light_open(void);

/**********************
 *  STATIC VARIABLES
 **********************/

static bool                   uart_open;
static i2c_master_dev_handle_t light;
static bool                   light_started;
static bool                   light_missing;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool radar_sensor_open(void)
{
    if(uart_open) return true;

    const uart_config_t config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if(uart_driver_install(BOARD_RADAR_UART, UART_BUF, 0, 0, NULL, 0) != ESP_OK) return false;

    if(uart_param_config(BOARD_RADAR_UART, &config) != ESP_OK ||
       uart_set_pin(BOARD_RADAR_UART, BOARD_RADAR_TX_PIN, BOARD_RADAR_RX_PIN,
                    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        uart_driver_delete(BOARD_RADAR_UART);
        return false;
    }

    uart_flush_input(BOARD_RADAR_UART);
    uart_open = true;

    /*A module that is there reports on its own; one that is not, or is wired
     *wrongly, says nothing. Either way the first read settles it.*/
    radar_reading_t reading;
    if(!radar_sensor_read(&reading)) {
        radar_sensor_close();
        return false;
    }

    return true;
}

bool radar_sensor_read(radar_reading_t * out)
{
    uint8_t payload[PAYLOAD_MAX];
    size_t  len = 0;

    memset(out, 0, sizeof(*out));

    if(!uart_open || !frame_read(payload, &len)) return false;

    /*Basic and engineering reports start the same; the gate energies an
     *engineering one adds sit past everything read here.*/
    if(len < 13) return false;
    if(payload[0] != PAYLOAD_BASIC && payload[0] != PAYLOAD_ENGINEERING) return false;
    if(payload[1] != TARGET_HEAD) return false;

    uint8_t  state          = payload[2];
    uint16_t moving_cm      = (uint16_t)(payload[3] | (payload[4] << 8));
    uint8_t  moving_energy  = payload[5];
    uint16_t still_cm       = (uint16_t)(payload[6] | (payload[7] << 8));
    uint8_t  still_energy   = payload[8];
    uint16_t detected_cm    = (uint16_t)(payload[9] | (payload[10] << 8));

    out->present = (state & (STATE_MOVING | STATE_STILL)) != 0;
    out->moving  = (state & STATE_MOVING) != 0;

    /*The moving target's distance while there is one: someone walking up
     *matters more than the still reflection behind them.*/
    out->distance_cm = out->moving ? moving_cm : (state & STATE_STILL ? still_cm : detected_cm);
    out->motion      = out->moving ? moving_energy : (float)still_energy / 4.0f;

    return true;
}

void radar_sensor_close(void)
{
    if(!uart_open) return;

    uart_driver_delete(BOARD_RADAR_UART);
    uart_open = false;
}

bool radar_sensor_light(float * lux)
{
    if(!light_open()) return false;

    uint8_t raw[2];
    if(i2c_master_receive(light, raw, sizeof(raw), I2C_TIMEOUT_MS) != ESP_OK) return false;

    *lux = (float)((raw[0] << 8) | raw[1]) / 1.2f;
    return true;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * The next report frame, without its header, length and tail.
 * @param payload   receives the payload, up to PAYLOAD_MAX bytes of it
 * @param len       receives how much of it was kept
 * @return          false if no whole frame arrived in FRAME_WAIT_MS
 */
static bool frame_read(uint8_t * payload, size_t * len)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(FRAME_WAIT_MS);
    uint32_t   matched  = 0;

    /*Byte by byte to the next header: the module writes continuously, so the
     *buffer is usually mid-frame when a read starts.*/
    while(matched < sizeof(FRAME_HEAD)) {
        uint8_t byte;
        if(xTaskGetTickCount() >= deadline) return false;
        if(uart_read_bytes(BOARD_RADAR_UART, &byte, 1, pdMS_TO_TICKS(FRAME_WAIT_MS)) != 1) return false;

        matched = byte == FRAME_HEAD[matched] ? matched + 1 : (byte == FRAME_HEAD[0] ? 1 : 0);
    }

    uint8_t length[2];
    if(uart_read_bytes(BOARD_RADAR_UART, length, sizeof(length), pdMS_TO_TICKS(FRAME_WAIT_MS)) != sizeof(length)) {
        return false;
    }

    size_t size = (size_t)(length[0] | (length[1] << 8));
    if(size == 0 || size > PAYLOAD_MAX * 4) return false;

    size_t kept = size < PAYLOAD_MAX ? size : PAYLOAD_MAX;
    if(uart_read_bytes(BOARD_RADAR_UART, payload, kept, pdMS_TO_TICKS(FRAME_WAIT_MS)) != (int)kept) return false;

    /*The rest of a longer payload, and the tail, thrown away.*/
    for(size_t i = kept; i < size; i++) {
        uint8_t byte;
        if(uart_read_bytes(BOARD_RADAR_UART, &byte, 1, pdMS_TO_TICKS(FRAME_WAIT_MS)) != 1) return false;
    }

    uint8_t tail[4];
    if(uart_read_bytes(BOARD_RADAR_UART, tail, sizeof(tail), pdMS_TO_TICKS(FRAME_WAIT_MS)) != sizeof(tail)) {
        return false;
    }
    if(memcmp(tail, FRAME_TAIL, sizeof(tail)) != 0) return false;

    *len = kept;
    return true;
}

/** The light sensor, started on its first reading. */
static bool light_open(void)
{
    if(light_started) return true;
    if(light_missing) return false;

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if(!bus) {
        light_missing = true;
        return false;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BH1750_ADDRESS,
        .scl_speed_hz    = I2C_SPEED_HZ,
    };

    if(i2c_master_bus_add_device(bus, &config, &light) != ESP_OK) {
        light_missing = true;
        return false;
    }

    uint8_t command = BH1750_CONTINUOUS;
    if(i2c_master_transmit(light, &command, 1, I2C_TIMEOUT_MS) != ESP_OK) {
        i2c_master_bus_rm_device(light);
        light         = NULL;
        light_missing = true;
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(BH1750_FIRST_WAIT));
    light_started = true;
    return true;
}

#endif /*defined(ESP_PLATFORM)*/
