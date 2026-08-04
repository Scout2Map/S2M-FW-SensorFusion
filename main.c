/*
 * File   : main.c
 * Purpose: Scout2Map sensor fusion MCU entry point (Raspberry Pi Pico 2).
 *          Runs a cooperative scheduler that polls each sensor on its own
 *          independent period, injects AHT21 readings into ENS160
 *          compensation, and emits one JSON line per reading over USB CDC.
 * Author : jihoonkimtech
 *
 * Pin map
 *   I2C0 : SDA=GP4, SCL=GP5  -> ENS160(0x53) + AHT21(0x38)   [3.3V only]
 *   I2C1 : SDA=GP2, SCL=GP3  -> BH1750(0x23)                  [3.3V]
 *   UART0: TX =GP0, RX =GP1  -> PMS7003 (9600bps)             [VBUS 5V]
 *
 * Note: UART0 belongs to the PMS7003, so stdio_uart must stay disabled.
 * See pico_enable_stdio_uart(... 0) in CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"

#include "queue/line_queue.h"
#include "diag/i2c_scan.h"
#include "drivers/aht21.h"
#include "drivers/ens160.h"
#include "drivers/bh1750.h"
#include "drivers/pms7003.h"

// ---------------- Pin and bus configuration ----------------
#define I2C_ENV        i2c0
#define PIN_ENV_SDA    4
#define PIN_ENV_SCL    5

#define I2C_LUX        i2c1
#define PIN_LUX_SDA    2
#define PIN_LUX_SCL    3

#define I2C_BAUD       100000   // 100kHz. Do not raise this on long wiring.

#define UART_PMS       uart0
#define PIN_PMS_TX     0
#define PIN_PMS_RX     1
#define PMS_BAUD       9600

// ---------------- Feature flags ----------------
// Run a bus scan at startup before touching any sensor.
// Keep this on during bring-up, turn it off once wiring is settled.
#define ENABLE_I2C_SCAN  1

// ---------------- Scheduling periods (ms) ----------------
// Chassis speed is about 0.228 m/s. BH1750 runs fast, the rest are
// capped by their own physical response limits.
#define PERIOD_BH1750      200   // 5Hz, conversion takes 120ms
#define PERIOD_AHT21      1000   // 1Hz
#define AHT21_CONV_MS       85   // settling delay after the trigger
#define PERIOD_ENS160     1000   // 1Hz, matches the internal algorithm rate
#define PMS_MIN_INTERVAL  1000   // rate limit even if frames arrive faster
#define PERIOD_HEARTBEAT  5000   // liveness ping

// ---------------- Global state ----------------
static aht21_t   g_aht;
static ens160_t  g_ens;
static bh1750_t  g_lux;
static pms7003_t g_pms;

static bool g_aht_ok, g_ens_ok, g_lux_ok;

// AHT21 measurement state machine
typedef enum { AHT_IDLE, AHT_WAIT } aht_state_t;
static aht_state_t g_aht_state = AHT_IDLE;

static void bus_init(void) {
    // I2C0 for the environment combo board (ENS160 + AHT21)
    i2c_init(I2C_ENV, I2C_BAUD);
    gpio_set_function(PIN_ENV_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_ENV_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_ENV_SDA);
    gpio_pull_up(PIN_ENV_SCL);

    // I2C1 for the ambient light sensor
    i2c_init(I2C_LUX, I2C_BAUD);
    gpio_set_function(PIN_LUX_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_LUX_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_LUX_SDA);
    gpio_pull_up(PIN_LUX_SCL);

    // UART0 for the dust sensor
    uart_init(UART_PMS, PMS_BAUD);
    gpio_set_function(PIN_PMS_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_PMS_RX, GPIO_FUNC_UART);
    uart_set_format(UART_PMS, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_PMS, true);
}

static void sensors_init(void) {
    g_aht_ok = aht21_init(&g_aht, I2C_ENV);
    g_ens_ok = ens160_init(&g_ens, I2C_ENV);
    g_lux_ok = bh1750_init(&g_lux, I2C_LUX, BH1750_ADDR);
    pms7003_init(&g_pms, UART_PMS);

    // Report which sensors came up so the host can flag wiring problems
    lq_push("{\"src\":\"sys\",\"event\":\"boot\","
            "\"aht21\":%s,\"ens160\":%s,\"bh1750\":%s}",
            g_aht_ok ? "true" : "false",
            g_ens_ok ? "true" : "false",
            g_lux_ok ? "true" : "false");
}

int main(void) {
    stdio_init_all();
    lq_init();

    // Give USB CDC a moment to enumerate, but proceed either way
    sleep_ms(1500);

    bus_init();

#if ENABLE_I2C_SCAN
    // Scan before sensor init: the scan sees the raw bus state, and the
    // result still reaches the host even if an init call later stalls.
    i2c_scan(I2C_ENV, 0);
    i2c_scan(I2C_LUX, 1);
    lq_flush();
#endif

    sensors_init();
    lq_flush();

    absolute_time_t t_lux      = get_absolute_time();
    absolute_time_t t_aht      = get_absolute_time();
    absolute_time_t t_ens      = get_absolute_time();
    absolute_time_t t_hb       = get_absolute_time();
    absolute_time_t t_aht_conv = get_absolute_time();
    absolute_time_t t_pms_last = get_absolute_time();

    for (;;) {
        absolute_time_t now = get_absolute_time();

        // ---------- BH1750 ambient light ----------
        if (g_lux_ok && absolute_time_diff_us(t_lux, now) >= PERIOD_BH1750 * 1000) {
            t_lux = now;
            if (bh1750_read(&g_lux)) {
                lq_push("{\"src\":\"bh1750\",\"lux\":%.1f}", g_lux.lux);
            }
        }

        // ---------- AHT21 temp/humidity: trigger, wait 85ms, read ----------
        if (g_aht_ok) {
            if (g_aht_state == AHT_IDLE &&
                absolute_time_diff_us(t_aht, now) >= PERIOD_AHT21 * 1000) {
                if (aht21_trigger(&g_aht)) {
                    g_aht_state = AHT_WAIT;
                    t_aht_conv  = now;
                } else {
                    t_aht = now;   // retry on the next cycle
                }
            } else if (g_aht_state == AHT_WAIT &&
                       absolute_time_diff_us(t_aht_conv, now) >= AHT21_CONV_MS * 1000) {
                if (aht21_read(&g_aht)) {
                    lq_push("{\"src\":\"aht21\",\"temp\":%.2f,\"hum\":%.2f}",
                            g_aht.temp_c, g_aht.humidity);

                    // Feed real readings into ENS160 compensation registers
                    if (g_ens_ok) {
                        ens160_set_compensation(&g_ens, g_aht.temp_c, g_aht.humidity);
                    }
                }
                g_aht_state = AHT_IDLE;
                t_aht = now;
            }
        }

        // ---------- ENS160 gas and air quality ----------
        if (g_ens_ok && absolute_time_diff_us(t_ens, now) >= PERIOD_ENS160 * 1000) {
            t_ens = now;
            if (ens160_read(&g_ens)) {
                lq_push("{\"src\":\"ens160\",\"eco2\":%u,\"tvoc\":%u,"
                        "\"aqi\":%u,\"valid\":%u}",
                        g_ens.eco2, g_ens.tvoc, g_ens.aqi, g_ens.validity);
            }
        }

        // ---------- PMS7003 particulate matter, event driven ----------
        if (pms7003_poll(&g_pms)) {
            if (absolute_time_diff_us(t_pms_last, now) >= PMS_MIN_INTERVAL * 1000) {
                t_pms_last = now;
                lq_push("{\"src\":\"pms7003\",\"pm1\":%u,\"pm25\":%u,\"pm10\":%u}",
                        g_pms.pm1, g_pms.pm25, g_pms.pm10);
            }
        }

        // ---------- Heartbeat ----------
        if (absolute_time_diff_us(t_hb, now) >= PERIOD_HEARTBEAT * 1000) {
            t_hb = now;
            lq_push("{\"src\":\"sys\",\"uptime_ms\":%llu}",
                    (unsigned long long)(to_us_since_boot(now) / 1000));
        }

        // ---------- Single writer: the only place that touches USB ----------
        lq_flush();

        sleep_ms(2);
    }
}
