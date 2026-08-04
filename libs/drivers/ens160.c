/*
 * File   : ens160.c
 * Purpose: ENS160 driver implementation. Verifies part ID, walks the device
 *          through reset/idle/standard operating modes, writes compensation
 *          registers, and reads AQI, TVOC, and eCO2 gated on the NEWDAT flag.
 * Author : jihoonkimtech
 */

#include "drivers/ens160.h"

#include <string.h>
#include "pico/stdlib.h"

#define I2C_TIMEOUT_US 10000

// Register map
#define REG_PART_ID      0x00
#define REG_OPMODE       0x10
#define REG_TEMP_IN      0x13
#define REG_RH_IN        0x15
#define REG_DEVICE_STAT  0x20
#define REG_DATA_AQI     0x21
#define REG_DATA_TVOC    0x22
#define REG_DATA_ECO2    0x24

#define OPMODE_RESET     0xF0
#define OPMODE_IDLE      0x01
#define OPMODE_STANDARD  0x02

// Write n bytes to a register
static bool wr(ens160_t *d, uint8_t reg, const uint8_t *buf, size_t n) {
    uint8_t tmp[5];
    if (n > 4) return false;
    tmp[0] = reg;
    memcpy(&tmp[1], buf, n);
    return i2c_write_timeout_us(d->i2c, ENS160_ADDR, tmp, n + 1, false, I2C_TIMEOUT_US)
           == (int)(n + 1);
}

// Read n bytes from a register using a repeated start
static bool rd(ens160_t *d, uint8_t reg, uint8_t *buf, size_t n) {
    if (i2c_write_timeout_us(d->i2c, ENS160_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1)
        return false;
    return i2c_read_timeout_us(d->i2c, ENS160_ADDR, buf, n, false, I2C_TIMEOUT_US) == (int)n;
}

bool ens160_init(ens160_t *d, i2c_inst_t *i2c) {
    memset(d, 0, sizeof(*d));
    d->i2c = i2c;
    d->validity = 3;

    uint8_t id[2];
    if (!rd(d, REG_PART_ID, id, 2)) return false;
    uint16_t part = (uint16_t)id[0] | ((uint16_t)id[1] << 8);
    if (part != 0x0160) return false;   // not an ENS160

    // Reset, then idle, then standard operating mode
    uint8_t m = OPMODE_RESET;
    if (!wr(d, REG_OPMODE, &m, 1)) return false;
    sleep_ms(10);

    m = OPMODE_IDLE;
    if (!wr(d, REG_OPMODE, &m, 1)) return false;
    sleep_ms(10);

    m = OPMODE_STANDARD;
    if (!wr(d, REG_OPMODE, &m, 1)) return false;
    sleep_ms(30);

    return true;
}

bool ens160_set_compensation(ens160_t *d, float temp_c, float humidity) {
    // TEMP_IN: temperature in Kelvin * 64, 16-bit little-endian
    // RH_IN  : relative humidity * 512,   16-bit little-endian
    float k = temp_c + 273.15f;
    if (k < 0.0f) k = 0.0f;

    uint16_t t_raw = (uint16_t)(k * 64.0f + 0.5f);
    uint16_t h_raw = (uint16_t)(humidity * 512.0f + 0.5f);

    uint8_t tb[2] = {(uint8_t)(t_raw & 0xFF), (uint8_t)(t_raw >> 8)};
    uint8_t hb[2] = {(uint8_t)(h_raw & 0xFF), (uint8_t)(h_raw >> 8)};

    if (!wr(d, REG_TEMP_IN, tb, 2)) return false;
    if (!wr(d, REG_RH_IN,   hb, 2)) return false;
    return true;
}

bool ens160_read(ens160_t *d) {
    uint8_t stat;
    if (!rd(d, REG_DEVICE_STAT, &stat, 1)) return false;

    d->validity = (stat >> 2) & 0x03;

    // NEWDAT (bit1) indicates fresh data is available
    if (!(stat & 0x02)) return false;

    uint8_t buf[5];   // AQI(1) + TVOC(2) + eCO2(2) read back-to-back
    if (!rd(d, REG_DATA_AQI, buf, 5)) return false;

    d->aqi  = buf[0] & 0x07;
    d->tvoc = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    d->eco2 = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);

    // Trust the reading only in normal or warm-up state
    if (d->validity <= 1) d->ready = true;
    return true;
}
