/*
 * File   : aht21.c
 * Purpose: AHT21 driver implementation. Handles calibration check on init,
 *          measurement trigger, and 20-bit raw value decoding with range
 *          sanity checks against I2C glitches.
 * Author : jihoonkimtech
 */

#include "drivers/aht21.h"

#include <string.h>
#include "pico/stdlib.h"

#define I2C_TIMEOUT_US 10000

bool aht21_init(aht21_t *d, i2c_inst_t *i2c) {
    memset(d, 0, sizeof(*d));
    d->i2c = i2c;

    sleep_ms(40);  // power-on settling time

    // Check status: bit3 (calibrated) must be set
    uint8_t st = 0;
    if (i2c_read_timeout_us(i2c, AHT21_ADDR, &st, 1, false, I2C_TIMEOUT_US) != 1)
        return false;

    if (!(st & 0x08)) {
        // Initialization command (0xBE, 0x08, 0x00)
        const uint8_t cmd[3] = {0xBE, 0x08, 0x00};
        if (i2c_write_timeout_us(i2c, AHT21_ADDR, cmd, 3, false, I2C_TIMEOUT_US) != 3)
            return false;
        sleep_ms(10);
    }
    return true;
}

bool aht21_trigger(aht21_t *d) {
    const uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    return i2c_write_timeout_us(d->i2c, AHT21_ADDR, cmd, 3, false, I2C_TIMEOUT_US) == 3;
}

bool aht21_read(aht21_t *d) {
    uint8_t r[6];
    if (i2c_read_timeout_us(d->i2c, AHT21_ADDR, r, 6, false, I2C_TIMEOUT_US) != 6)
        return false;

    if (r[0] & 0x80) return false;  // still busy

    // Humidity and temperature are 20-bit values packed across r[1]..r[5]
    uint32_t raw_h = ((uint32_t)r[1] << 12) | ((uint32_t)r[2] << 4) | (r[3] >> 4);
    uint32_t raw_t = (((uint32_t)r[3] & 0x0F) << 16) | ((uint32_t)r[4] << 8) | r[5];

    d->humidity = ((float)raw_h / 1048576.0f) * 100.0f;
    d->temp_c   = ((float)raw_t / 1048576.0f) * 200.0f - 50.0f;

    // Reject clearly out-of-range values to guard against I2C glitches
    if (d->humidity < 0.0f || d->humidity > 100.0f) return false;
    if (d->temp_c < -40.0f || d->temp_c > 85.0f)    return false;

    d->ready = true;
    return true;
}
