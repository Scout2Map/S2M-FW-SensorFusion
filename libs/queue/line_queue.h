/*
 * File   : line_queue.h
 * Purpose: Public interface for the single-writer output line queue.
 *          Any producer may enqueue a formatted line; only the main loop
 *          drains it to USB CDC, which guarantees lines never interleave.
 * Author : jihoonkimtech
 */

#ifndef LINE_QUEUE_H
#define LINE_QUEUE_H

#include <stdbool.h>

// Max length of a single line including the newline
#define LQ_LINE_MAX   160
// Number of queued lines (must be a power of two)
#define LQ_SLOTS      16

void lq_init(void);

// Format one line and push it into the queue.
// Returns false if the queue is full (the line is discarded).
// Safe to call from any core or timer callback.
bool lq_push(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Call from the main loop only.
// Drains the queue to USB CDC in order.
// This is the single writer, so lines can never interleave.
void lq_flush(void);

#endif // LINE_QUEUE_H
