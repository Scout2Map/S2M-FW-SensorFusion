/*
 * File   : aht21.h
 * Purpose: Public interface for the AHT21 temperature and humidity sensor.
 *          Split trigger/read API so the caller can stay non-blocking during
 *          the sensor's conversion time.
 * Author : jihoonkimtech
 */

#ifndef AHT21_H
#define AHT21_H

#include <stdbool.h>
#include "hardware/i2c.h"

#define AHT21_ADDR 0x38

typedef struct {
    i2c_inst_t *i2c;
    bool  ready;          // true after at least one valid measurement
    float temp_c;
    float humidity;
} aht21_t;

bool aht21_init(aht21_t *d, i2c_inst_t *i2c);

// Trigger a measurement. Returns true on success.
bool aht21_trigger(aht21_t *d);

// Call at least 80ms after the trigger.
// Returns true when the values were updated.
bool aht21_read(aht21_t *d);

#endif // AHT21_H
