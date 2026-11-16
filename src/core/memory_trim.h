/* Give the memory malloc keeps free back to the system once activity stops
 * (docs/performance.md). Freeing a preview leaves free space between blocks
 * still in use, which glibc keeps: a real session browsing images kept
 * 536 MB free that way. malloc_trim() returns every whole free page.
 * glibc only; elsewhere these calls do nothing. No widgets. */
#ifndef MEMORY_TRIM_H
#define MEMORY_TRIM_H

#include <glib.h>

/* Delay after the last memory_trim_soon() before trimming */
#define MEMORY_TRIM_DELAY_MS 2000

/* Activity that freed memory (a new preview...): trim MEMORY_TRIM_DELAY_MS
 * after the last call, on a worker thread (the UI never waits for it).
 * Main thread only. */
void memory_trim_soon(void);

/* Trims done so far (for the unit test) */
guint memory_trim_count(void);

/* Change the delay (for the unit test) */
void memory_trim_set_delay_ms(guint delay_ms);

#endif
