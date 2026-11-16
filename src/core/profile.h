/* Performance probe (docs/performance.md): with LOOKIN_PROFILE set, the
 * application writes timings to stderr ("1" or "-") or to the file named by
 * the variable, one line each, all starting with "profile: ":
 *
 *   profile: mark   <total ms> +<since last mark> <label>    start-up steps
 *   profile: time   <ms> <label>                             timed operations
 *   profile: frame  <ms> at <total ms>[ slow][ unpainted]   each frame (a slow
 *                   one with "(update <ms>, layout <ms>, paint <ms>)")
 *   profile: sample at <s> s key=value...                    each minute after the
 *                   start-up: CPU %, memory, threads, frames of that minute
 *   profile: report key=value...                             summary at exit
 *
 * Numbers are written with a dot whatever the locale (profile_ms()), so that
 * scripts read the lines the same everywhere.
 *
 * Without the variable every call returns at once (profile_start() gives 0
 * and profile_done(0, ...) does nothing), so the probes stay in release
 * builds. Thread-safe: workers may time their own work. No AppData, no
 * widgets (the frame clock is watched by frame_watch). */
#ifndef PROFILE_H
#define PROFILE_H

#include <glib.h>

/* Frames longer than this count as slow (one frame at 60 Hz) */
#define PROFILE_SLOW_FRAME_US 16667

/* Read LOOKIN_PROFILE (first call of main(): its time is the origin) */
void profile_init(void);

/* Same with an explicit destination: NULL or "" off, "1" or "-" stderr,
 * otherwise a file (truncated). Only the first call counts. */
void profile_init_file(const gchar* dest);

gboolean profile_enabled(void);

/* A start-up step reached */
void profile_mark(const gchar* label);

/* Start-up over (the window is drawn and the main loop idle): marks it and
 * counts the following frames apart from the start-up ones */
void profile_startup_done(void);

/* Seconds between two sample lines */
#define PROFILE_SAMPLE_SECONDS 60

/* Time an operation: start = profile_start(); ...; profile_done(start, ...) */
gint64 profile_start(void);
void profile_done(gint64 start_us, const gchar* format, ...) G_GNUC_PRINTF(2, 3);

/* A free-form line ("profile: " + text) */
void profile_log(const gchar* format, ...) G_GNUC_PRINTF(1, 2);

/* Where a frame's time went: animations and tick callbacks, measuring and
 * allocating the widgets, drawing and rendering them (-1: phase not run) */
typedef struct {
    gint64 update_us;
    gint64 layout_us;
    gint64 paint_us;
} ProfileFramePhases;

/* One frame of the frame clock, the time its work took (main thread);
 * painted: something was drawn (else only animations or tick callbacks
 * ran); phases may be NULL */
void profile_frame(gint64 duration_us, gboolean painted, const ProfileFramePhases* phases);

/* A sample line now (also written each PROFILE_SAMPLE_SECONDS after the
 * start-up): what happened since the previous one */
void profile_sample(void);

/* us in milliseconds with that many decimals (0 to 3), a dot as separator
 * whatever the locale, into buf (PROFILE_MS_LEN bytes); returns buf */
#define PROFILE_MS_LEN 32
gchar* profile_ms(gchar* buf, gint64 us, guint decimals);

/* The summary line: frames of the start-up and after it, CPU time (in all,
 * and at the end of the start-up, loading of the libraries included), memory */
void profile_report(void);

/* Frame counters (exposed for the unit test) */
typedef struct {
    guint count;     /* frames painted */
    guint unpainted; /* frames where nothing was drawn (wake-ups all the same) */
    guint slow;      /* over PROFILE_SLOW_FRAME_US */
    gint64 total_us;
    gint64 worst_us;
} ProfileFrameStats;

void profile_frame_stats_add(ProfileFrameStats* stats, gint64 duration_us, gboolean painted);

/* Resident and peak resident memory in KB from the text of /proc/<pid>/status
 * (VmRSS, VmHWM); FALSE when VmRSS is missing (peak then -1 if missing) */
gboolean profile_parse_status(const gchar* text, gint64* rss_kb, gint64* peak_kb);

/* This process: memory (FALSE outside Linux) and CPU time in µs, user +
 * system (-1 when unknown) */
gboolean profile_memory(gint64* rss_kb, gint64* peak_kb);
gint64 profile_cpu_us(void);

#endif
