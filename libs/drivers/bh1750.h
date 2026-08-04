/*
 * File   : bh1750.h
 * Purpose: Public interface for the BH1750 ambient light sensor.
 *          Configured for continuous high resolution mode so the caller only
 *          needs to poll at its own rate.
 * Author : jihoonkimtech
 */

#ifndef BH1750_H
#define BH1750_H

#include <stdbool.h>
#include "hardware/i2c.h"

// 0x23 when ADDR is tied low or left floating, 0x5C when tied to VCC
#define BH1750_ADDR 0x23

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    bool  ready;
    float lux;
} bh1750_t;

bool bh1750_init(bh1750_t *d, i2c_inst_t *i2c, uint8_t addr);

// Continuous high-res mode, so a periodic read is all that is needed.
// Conversion takes roughly 120ms.
bool bh1750_read(bh1750_t *d);

#endif // BH1750_H
