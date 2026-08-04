/*
 * File   : pms7003.c
 * Purpose: PMS7003 driver implementation. Byte-wise state machine that
 *          resynchronizes on the 0x42 0x4D header, validates the frame
 *          checksum, and extracts atmospheric PM1.0, PM2.5, and PM10.
 * Author : jihoonkimtech
 */

#include "drivers/pms7003.h"

#include <string.h>
#include "pico/stdlib.h"

// Frame layout: 0x42 0x4D | len(2) | data(26) | checksum(2) = 32 bytes total
#define PMS_FRAME_LEN 32

void pms7003_init(pms7003_t *d, uart_inst_t *uart) {
    memset(d, 0, sizeof(*d));
    d->uart = uart;
}

static bool parse_frame(pms7003_t *d) {
    uint16_t frame_len = ((uint16_t)d->buf[2] << 8) | d->buf[3];
    if (frame_len != 28) return false;

    // Checksum is the sum of every byte before the checksum field
    uint16_t sum = 0;
    for (int i = 0; i < PMS_FRAME_LEN - 2; i++) sum += d->buf[i];

    uint16_t chk = ((uint16_t)d->buf[30] << 8) | d->buf[31];
    if (sum != chk) return false;

    // Bytes 10..15 hold the atmospheric-environment PM1.0 / PM2.5 / PM10
    d->pm1  = ((uint16_t)d->buf[10] << 8) | d->buf[11];
    d->pm25 = ((uint16_t)d->buf[12] << 8) | d->buf[13];
    d->pm10 = ((uint16_t)d->buf[14] << 8) | d->buf[15];

    d->ready = true;
    return true;
}

bool pms7003_poll(pms7003_t *d) {
    bool got = false;

    while (uart_is_readable(d->uart)) {
        uint8_t b = (uint8_t)uart_getc(d->uart);

        // Header synchronization
        if (d->idx == 0) {
            if (b != 0x42) continue;
        } else if (d->idx == 1) {
            if (b != 0x4D) {
                // If another 0x42 arrived, treat it as a new frame start
                d->idx = (b == 0x42) ? 1 : 0;
                if (d->idx == 1) d->buf[0] = 0x42;
                continue;
            }
        }

        d->buf[d->idx++] = b;

        if (d->idx >= PMS_FRAME_LEN) {
            if (parse_frame(d)) got = true;
            d->idx = 0;
        }
    }

    return got;
}
