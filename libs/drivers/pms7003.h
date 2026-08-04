/*
 * File   : pms7003.h
 * Purpose: Public interface for the PMS7003 particulate matter sensor.
 *          Exposes a non-blocking poll that consumes the UART FIFO and
 *          reports only fully validated 32-byte frames.
 * Author : jihoonkimtech
 */

#ifndef PMS7003_H
#define PMS7003_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/uart.h"

typedef struct {
    uart_inst_t *uart;
    bool     ready;
    uint16_t pm1;    // PM1.0 under atmospheric environment (ug/m^3)
    uint16_t pm25;   // PM2.5 under atmospheric environment
    uint16_t pm10;   // PM10 under atmospheric environment

    // Internal parser state
    uint8_t  buf[32];
    uint8_t  idx;
} pms7003_t;

void pms7003_init(pms7003_t *d, uart_inst_t *uart);

// Non-blocking. Drains the UART FIFO and parses it.
// Returns true when a complete valid frame was decoded.
bool pms7003_poll(pms7003_t *d);

#endif // PMS7003_H
