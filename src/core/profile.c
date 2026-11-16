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
static ProfileFrameStats sample_frames; /* since the last sample line */
static gint64 sample_at_us = 0;         /* time and CPU time of the last sample */
static gint64 sample_cpu_us = 0;
static GMutex lock;

gchar* profile_ms(gchar* buf, gint64 us, guint decimals) {
    static const gint64 unit[] = {1000, 100, 10, 1}; /* us per last digit */
    decimals = MIN(decimals, 3u);
    gint64 step = unit[decimals];
    gboolean negative = us < 0;
    guint64 v = (guint64)(negative ? -us : us);
    v = (v + (guint64)step / 2) / (guint64)step; /* rounded, in last digits */
    guint64 scale = 1;
    for (guint i = 0; i < decimals; i++) {
        scale *= 10;
    }
    if (decimals == 0) {
        g_snprintf(buf, PROFILE_MS_LEN, "%s%" G_GUINT64_FORMAT, negative && v > 0 ? "-" : "", v);
    } else {
        g_snprintf(buf, PROFILE_MS_LEN, "%s%" G_GUINT64_FORMAT ".%0*" G_GUINT64_FORMAT,
                   negative && v > 0 ? "-" : "", v / scale, (int)decimals, v % scale);
    }
    return buf;
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
    gchar total[PROFILE_MS_LEN], delta[PROFILE_MS_LEN];
    write_linef("mark %9s ms +%s %s", profile_ms(total, now - origin_us, 1), profile_ms(delta, since, 1), label);
}

static gboolean on_sample_timeout(gpointer data) {
    (void)data;
    profile_sample();
    return G_SOURCE_CONTINUE;
}

void profile_startup_done(void) {
    if (out == NULL || startup_over) {
        return;
    }
    startup_over = TRUE;
    startup_cpu_us = profile_cpu_us();
    g_mutex_lock(&lock);
    sample_frames = (ProfileFrameStats){0};
    sample_at_us = g_get_monotonic_time();
    sample_cpu_us = startup_cpu_us;
    g_mutex_unlock(&lock);
    profile_mark("startup done");
    g_timeout_add_seconds(PROFILE_SAMPLE_SECONDS, on_sample_timeout, NULL);
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
    gchar t[PROFILE_MS_LEN];
    write_linef("time %8s ms %s", profile_ms(t, elapsed, 1), label);
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
    profile_frame_stats_add(&sample_frames, duration_us, painted);
    g_mutex_unlock(&lock);
    gboolean slow = duration_us > PROFILE_SLOW_FRAME_US;
    gchar detail[128] = "";
    if (slow && phases != NULL) {
        gchar u[PROFILE_MS_LEN], l[PROFILE_MS_LEN], p[PROFILE_MS_LEN];
        g_snprintf(detail, sizeof(detail), " (update %s, layout %s, paint %s)",
                   profile_ms(u, MAX(phases->update_us, 0), 1), profile_ms(l, MAX(phases->layout_us, 0), 1),
                   profile_ms(p, MAX(phases->paint_us, 0), 1));
    }
    gchar d[PROFILE_MS_LEN], at[PROFILE_MS_LEN];
    write_linef("frame %6s ms at %s ms%s%s%s", profile_ms(d, duration_us, 1),
                profile_ms(at, g_get_monotonic_time() - origin_us, 1), slow ? " slow" : "",
                painted ? "" : " unpainted", detail);
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

static gint64 mean_us(const ProfileFrameStats* s) {
    guint n = s->count + s->unpainted;
    return n > 0 ? s->total_us / n : 0;
}

/* Threads of this process (/proc/self/status), -1 when unknown */
static gint64 thread_count(void) {
    gchar* text = NULL;
    if (!g_file_get_contents("/proc/self/status", &text, NULL, NULL)) {
        return -1;
    }
    gint64 n = status_value(text, "Threads");
    g_free(text);
    return n;
}

void profile_sample(void) {
    if (out == NULL) {
        return;
    }
    gint64 now = g_get_monotonic_time();
    gint64 cpu = profile_cpu_us();
    gint64 rss = -1, peak = -1;
    profile_memory(&rss, &peak);
    g_mutex_lock(&lock);
    ProfileFrameStats f = sample_frames;
    sample_frames = (ProfileFrameStats){0};
    gint64 wall = now - sample_at_us;
    gint64 used = (cpu >= 0 && sample_cpu_us >= 0) ? cpu - sample_cpu_us : -1;
    sample_at_us = now;
    sample_cpu_us = cpu;
    g_mutex_unlock(&lock);
    /* CPU in % of one core: used / wall * 100, as ms with one decimal */
    gchar pct[PROFILE_MS_LEN], worst[PROFILE_MS_LEN], at[PROFILE_MS_LEN];
    if (used >= 0 && wall > 0) {
        profile_ms(pct, used * 100000 / wall, 1);
    } else {
        g_strlcpy(pct, "-1", sizeof(pct));
    }
    write_linef("sample at %s s cpu=%s%% rss_mb=%" G_GINT64_FORMAT " threads=%" G_GINT64_FORMAT
                " frames=%u unpainted=%u slow=%u worst_ms=%s",
                profile_ms(at, (now - origin_us) / 1000, 0), pct, rss >= 0 ? rss / 1024 : -1, thread_count(),
                f.count, f.unpainted, f.slow, profile_ms(worst, f.worst_us, 1));
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
    gchar up[PROFILE_MS_LEN], cpu_ms[PROFILE_MS_LEN], scpu[PROFILE_MS_LEN], sworst[PROFILE_MS_LEN];
    gchar worst[PROFILE_MS_LEN], mean[PROFILE_MS_LEN];
    write_linef("report uptime_ms=%s cpu_ms=%s startup_cpu_ms=%s rss_kb=%" G_GINT64_FORMAT
                " peak_kb=%" G_GINT64_FORMAT
                " startup_frames=%u startup_slow=%u startup_worst_ms=%s"
                " frames=%u unpainted=%u slow=%u worst_ms=%s mean_ms=%s",
                profile_ms(up, g_get_monotonic_time() - origin_us, 0), cpu >= 0 ? profile_ms(cpu_ms, cpu, 0) : "-1",
                startup_cpu_us >= 0 ? profile_ms(scpu, startup_cpu_us, 0) : "-1", rss, peak, s.count, s.slow,
                profile_ms(sworst, s.worst_us, 1), r.count, r.unpainted, r.slow, profile_ms(worst, r.worst_us, 1),
                profile_ms(mean, mean_us(&r), 2));
}
