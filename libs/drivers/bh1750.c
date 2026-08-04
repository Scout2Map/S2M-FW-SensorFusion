/*
 * File   : bh1750.c
 * Purpose: BH1750 driver implementation. Powers the device on, resets the
 *          data register, selects continuous high resolution mode, and
 *          converts the raw 16-bit count into lux.
 * Author : jihoonkimtech
 */

#include "drivers/bh1750.h"

#include <string.h>
#include "pico/stdlib.h"

#define I2C_TIMEOUT_US 10000

#define CMD_POWER_ON       0x01
#define CMD_RESET          0x07
#define CMD_CONT_HIRES     0x10   // 1 lx resolution, about 120ms per conversion

bool bh1750_init(bh1750_t *d, i2c_inst_t *i2c, uint8_t addr) {
    memset(d, 0, sizeof(*d));
    d->i2c  = i2c;
    d->addr = addr;

    uint8_t c = CMD_POWER_ON;
    if (i2c_write_timeout_us(i2c, addr, &c, 1, false, I2C_TIMEOUT_US) != 1) return false;
    sleep_ms(5);

    c = CMD_RESET;
    if (i2c_write_timeout_us(i2c, addr, &c, 1, false, I2C_TIMEOUT_US) != 1) return false;
    sleep_ms(5);

    c = CMD_CONT_HIRES;
    if (i2c_write_timeout_us(i2c, addr, &c, 1, false, I2C_TIMEOUT_US) != 1) return false;
    sleep_ms(180);   // wait for the first conversion to finish

    return true;
}

bool bh1750_read(bh1750_t *d) {
    uint8_t r[2];
    if (i2c_read_timeout_us(d->i2c, d->addr, r, 2, false, I2C_TIMEOUT_US) != 2)
        return false;

    // Raw count maps to lux with a fixed 1.2 divisor
    uint16_t raw = ((uint16_t)r[0] << 8) | r[1];
    d->lux = (float)raw / 1.2f;
    d->ready = true;
    return true;
}
