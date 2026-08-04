/*
 * File   : line_queue.c
 * Purpose: Spinlock protected ring buffer that serializes all USB CDC output.
 *          Producers call lq_push from anywhere; lq_flush is the sole writer
 *          and runs only in the main loop.
 * Author : jihoonkimtech
 */

#include "queue/line_queue.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/sync.h"

typedef struct {
    char     buf[LQ_SLOTS][LQ_LINE_MAX];
    uint16_t len[LQ_SLOTS];
    volatile uint32_t head;   // push index
    volatile uint32_t tail;   // flush index
} lq_t;

static lq_t s_q;
static spin_lock_t *s_lock;
static uint32_t s_dropped;    // lines discarded due to queue overflow

void lq_init(void) {
    memset(&s_q, 0, sizeof(s_q));
    s_dropped = 0;
    int sl = spin_lock_claim_unused(true);
    s_lock = spin_lock_instance((uint)sl);
}

bool lq_push(const char *fmt, ...) {
    char tmp[LQ_LINE_MAX];

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp) - 1, fmt, ap);
    va_end(ap);

    if (n < 0) return false;
    if (n > (int)(sizeof(tmp) - 2)) n = (int)(sizeof(tmp) - 2);

    // Append the newline here so framing never breaks
    // even if the caller forgets it
    tmp[n]     = '\n';
    tmp[n + 1] = '\0';
    int total = n + 1;

    uint32_t save = spin_lock_blocking(s_lock);

    uint32_t next = (s_q.head + 1) & (LQ_SLOTS - 1);
    if (next == s_q.tail) {
        // Queue full: drop the new line to preserve ordering
        s_dropped++;
        spin_unlock(s_lock, save);
        return false;
    }

    memcpy(s_q.buf[s_q.head], tmp, (size_t)total);
    s_q.len[s_q.head] = (uint16_t)total;
    s_q.head = next;

    spin_unlock(s_lock, save);
    return true;
}

void lq_flush(void) {
    for (;;) {
        char     line[LQ_LINE_MAX];
        uint16_t len = 0;

        uint32_t save = spin_lock_blocking(s_lock);
        if (s_q.tail == s_q.head) {
            spin_unlock(s_lock, save);
            break;
        }
        len = s_q.len[s_q.tail];
        memcpy(line, s_q.buf[s_q.tail], len);
        s_q.tail = (s_q.tail + 1) & (LQ_SLOTS - 1);
        spin_unlock(s_lock, save);

        fwrite(line, 1, len, stdout);
    }
    fflush(stdout);

    // Report drops once so the host can detect data loss
    if (s_dropped) {
        uint32_t d;
        uint32_t save = spin_lock_blocking(s_lock);
        d = s_dropped;
        s_dropped = 0;
        spin_unlock(s_lock, save);
        printf("{\"src\":\"sys\",\"dropped\":%lu}\n", (unsigned long)d);
        fflush(stdout);
    }
}
