/* Performance probe (see profile.h) */
#include "core/profile.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <sys/resource.h>
#endif

static FILE* out = NULL;      /* NULL: profiling off */
static gboolean initialized = FALSE;
static gint64 origin_us = 0;  /* profile_init() */
static gint64 last_mark_us = 0;
static gboolean startup_over = FALSE;
static gint64 startup_cpu_us = -1; /* CPU time at profile_startup_done() */
static ProfileFrameStats startup_frames;
static ProfileFrameStats run_frames;
static GMutex lock;

static gdouble ms(gint64 us) {
    return us / 1000.0;
}

void profile_init_file(const gchar* dest) {
    if (initialized) {
        return;
    }
    initialized = TRUE;
    origin_us = g_get_monotonic_time();
    last_mark_us = origin_us;
    if (dest == NULL || dest[0] == '\0') {
        return;
    }
    if (strcmp(dest, "1") == 0 || strcmp(dest, "-") == 0) {
        out = stderr;
        return;
    }
    out = fopen(dest, "w");
    if (out == NULL) {
        g_printerr("profile: cannot write %s, timings go to stderr\n", dest);
        out = stderr;
    }
}

void profile_init(void) {
    profile_init_file(g_getenv("LOOKIN_PROFILE"));
}

gboolean profile_enabled(void) {
    return out != NULL;
}

/* One line, under the lock */
static void write_line(const gchar* format, va_list args) {
    g_mutex_lock(&lock);
    fputs("profile: ", out);
    vfprintf(out, format, args);
    fputc('\n', out);
    fflush(out);
    g_mutex_unlock(&lock);
}

static void write_linef(const gchar* format, ...) G_GNUC_PRINTF(1, 2);
static void write_linef(const gchar* format, ...) {
    va_list args;
    va_start(args, format);
    write_line(format, args);
    va_end(args);
}

void profile_log(const gchar* format, ...) {
    if (out == NULL) {
        return;
    }
    va_list args;
    va_start(args, format);
    write_line(format, args);
    va_end(args);
}

void profile_mark(const gchar* label) {
    if (out == NULL) {
        return;
    }
    gint64 now = g_get_monotonic_time();
    g_mutex_lock(&lock);
    gint64 since = now - last_mark_us;
    last_mark_us = now;
    g_mutex_unlock(&lock);
    write_linef("mark %9.1f ms +%.1f %s", ms(now - origin_us), ms(since), label);
}

void profile_startup_done(void) {
    if (out == NULL || startup_over) {
        return;
    }
    startup_over = TRUE;
    startup_cpu_us = profile_cpu_us();
    profile_mark("startup done");
}

gint64 profile_start(void) {
    return out != NULL ? g_get_monotonic_time() : 0;
}

void profile_done(gint64 start_us, const gchar* format, ...) {
    if (out == NULL || start_us == 0) {
        return;
    }
    gint64 elapsed = g_get_monotonic_time() - start_us;
    va_list args;
    va_start(args, format);
    gchar* label = g_strdup_vprintf(format, args);
    va_end(args);
    write_linef("time %8.1f ms %s", ms(elapsed), label);
    g_free(label);
}

void profile_frame_stats_add(ProfileFrameStats* stats, gint64 duration_us, gboolean painted) {
    if (!painted) {
        stats->unpainted++;
    } else {
        stats->count++;
    }
    stats->total_us += duration_us;
    if (duration_us > stats->worst_us) {
        stats->worst_us = duration_us;
    }
    if (duration_us > PROFILE_SLOW_FRAME_US) {
        stats->slow++;
    }
}

void profile_frame(gint64 duration_us, gboolean painted, const ProfileFramePhases* phases) {
    if (out == NULL) {
        return;
    }
    g_mutex_lock(&lock);
    profile_frame_stats_add(startup_over ? &run_frames : &startup_frames, duration_us, painted);
    g_mutex_unlock(&lock);
    gboolean slow = duration_us > PROFILE_SLOW_FRAME_US;
    gchar detail[96] = "";
    if (slow && phases != NULL) {
        g_snprintf(detail, sizeof(detail), " (update %.1f, layout %.1f, paint %.1f)",
                   phases->update_us >= 0 ? ms(phases->update_us) : 0.0,
                   phases->layout_us >= 0 ? ms(phases->layout_us) : 0.0,
                   phases->paint_us >= 0 ? ms(phases->paint_us) : 0.0);
    }
    write_linef("frame %6.1f ms at %.1f ms%s%s%s", ms(duration_us), ms(g_get_monotonic_time() - origin_us),
                slow ? " slow" : "", painted ? "" : " unpainted", detail);
}

/* The number after "key:" at the start of a line of text, in KB; -1 if absent */
static gint64 status_value(const gchar* text, const gchar* key) {
    gsize len = strlen(key);
    for (const gchar* line = text; line != NULL && *line != '\0';) {
        if (strncmp(line, key, len) == 0 && line[len] == ':') {
            return g_ascii_strtoll(line + len + 1, NULL, 10);
        }
        line = strchr(line, '\n');
        if (line != NULL) {
            line++;
        }
    }
    return -1;
}

gboolean profile_parse_status(const gchar* text, gint64* rss_kb, gint64* peak_kb) {
    *rss_kb = (text != NULL) ? status_value(text, "VmRSS") : -1;
    *peak_kb = (text != NULL) ? status_value(text, "VmHWM") : -1;
    return *rss_kb >= 0;
}

gboolean profile_memory(gint64* rss_kb, gint64* peak_kb) {
    gchar* text = NULL;
    if (!g_file_get_contents("/proc/self/status", &text, NULL, NULL)) {
        *rss_kb = -1;
        *peak_kb = -1;
        return FALSE;
    }
    gboolean ok = profile_parse_status(text, rss_kb, peak_kb);
    g_free(text);
    return ok;
}

gint64 profile_cpu_us(void) {
#ifdef G_OS_UNIX
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        return (gint64)ru.ru_utime.tv_sec * G_USEC_PER_SEC + ru.ru_utime.tv_usec +
               (gint64)ru.ru_stime.tv_sec * G_USEC_PER_SEC + ru.ru_stime.tv_usec;
    }
#endif
    return -1;
}

static gdouble mean_ms(const ProfileFrameStats* s) {
    guint n = s->count + s->unpainted;
    return n > 0 ? ms(s->total_us) / n : 0.0;
}

void profile_report(void) {
    if (out == NULL) {
        return;
    }
    gint64 rss = -1, peak = -1;
    profile_memory(&rss, &peak);
    gint64 cpu = profile_cpu_us();
    g_mutex_lock(&lock);
    ProfileFrameStats s = startup_frames;
    ProfileFrameStats r = run_frames;
    g_mutex_unlock(&lock);
    write_linef("report uptime_ms=%.0f cpu_ms=%.0f startup_cpu_ms=%.0f rss_kb=%" G_GINT64_FORMAT
                " peak_kb=%" G_GINT64_FORMAT
                " startup_frames=%u startup_slow=%u startup_worst_ms=%.1f"
                " frames=%u unpainted=%u slow=%u worst_ms=%.1f mean_ms=%.2f",
                ms(g_get_monotonic_time() - origin_us), cpu >= 0 ? ms(cpu) : -1.0,
                startup_cpu_us >= 0 ? ms(startup_cpu_us) : -1.0, rss, peak,
                s.count, s.slow, ms(s.worst_us), r.count, r.unpainted, r.slow, ms(r.worst_us), mean_ms(&r));
}
