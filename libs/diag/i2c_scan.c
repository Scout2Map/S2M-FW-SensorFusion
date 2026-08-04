/*
 * File   : i2c_scan.c
 * Purpose: Startup I2C bus scanner implementation. Walks the 0x08..0x77
 *          range, probes each address with a 1-byte read, and reports the
 *          responders together with a guess at which sensor each one is.
 * Author : jihoonkimtech
 */

#include "diag/i2c_scan.h"
#include "queue/line_queue.h"

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#define SCAN_TIMEOUT_US   2000
#define SCAN_ADDR_FIRST   0x08   // below this is reserved
#define SCAN_ADDR_LAST    0x77   // above this is reserved

// Map a responding address to a human readable guess.
// Helps spot a sensor that came up on an unexpected address.
static const char *guess_device(uint8_t addr) {
    switch (addr) {
        case 0x23: return "BH1750";
        case 0x5C: return "BH1750(alt)";
        case 0x38: return "AHT21";
        case 0x52: return "ENS160(alt)";
        case 0x53: return "ENS160";
        case 0x28: return "BNO055(alt)";
        case 0x29: return "BNO055";
        default:   return "unknown";
    }
}

void i2c_scan(i2c_inst_t *i2c, uint8_t bus_id) {
    char list[128];
    int  used  = 0;
    int  found = 0;

    list[0] = '\0';

    for (uint8_t addr = SCAN_ADDR_FIRST; addr <= SCAN_ADDR_LAST; addr++) {
        uint8_t dummy;

        // A device that ACKs its address returns the requested byte count.
        // Anything negative means no ACK or a bus fault.
        int r = i2c_read_timeout_us(i2c, addr, &dummy, 1, false, SCAN_TIMEOUT_US);
        if (r < 0) continue;

        // Emit one detail line per hit so the guess travels with the address
        lq_push("{\"src\":\"sys\",\"event\":\"i2c_found\",\"bus\":%u,"
                "\"addr\":\"0x%02X\",\"guess\":\"%s\"}",
                bus_id, addr, guess_device(addr));

        // Accumulate a compact summary list, guarding against overflow
        int n = snprintf(&list[used], sizeof(list) - used,
                         "%s\"0x%02X\"", found ? "," : "", addr);
        if (n > 0 && n < (int)(sizeof(list) - used)) used += n;

        found++;
    }

    lq_push("{\"src\":\"sys\",\"event\":\"i2c_scan\",\"bus\":%u,"
            "\"count\":%d,\"addrs\":[%s]}",
            bus_id, found, list);
}
