/*
 * File   : i2c_scan.h
 * Purpose: Public interface for the startup I2C bus scanner.
 *          Diagnostic aid for bring-up: reports every responding address on a
 *          bus so wiring, power, and address faults can be told apart quickly.
 * Author : jihoonkimtech
 */

#ifndef I2C_SCAN_H
#define I2C_SCAN_H

#include <stdint.h>
#include "hardware/i2c.h"

// Probe every valid 7-bit address on the bus and emit one JSON line
// listing the addresses that responded. bus_id is echoed back in the
// output so the host can tell the two buses apart.
void i2c_scan(i2c_inst_t *i2c, uint8_t bus_id);

#endif // I2C_SCAN_H
