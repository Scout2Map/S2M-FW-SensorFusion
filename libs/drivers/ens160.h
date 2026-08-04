/*
 * File   : ens160.h
 * Purpose: Public interface for the ENS160 digital multi-gas sensor.
 *          Exposes the temperature and humidity compensation entry point that
 *          AHT21 readings must be fed into for accurate eCO2 and TVOC output.
 * Author : jihoonkimtech
 */

#ifndef ENS160_H
#define ENS160_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"

#define ENS160_ADDR 0x53

typedef struct {
    i2c_inst_t *i2c;
    bool     ready;
    uint8_t  aqi;      // air quality index, 1..5
    uint16_t tvoc;     // ppb
    uint16_t eco2;     // ppm
    uint8_t  validity; // 0=normal, 1=warm-up, 2=initial start-up, 3=invalid
} ens160_t;

bool ens160_init(ens160_t *d, i2c_inst_t *i2c);

// Feed measured temperature and humidity into the compensation registers.
// This directly affects eCO2 and TVOC accuracy.
bool ens160_set_compensation(ens160_t *d, float temp_c, float humidity);

// Updates the values only when new data is available.
bool ens160_read(ens160_t *d);

#endif // ENS160_H
