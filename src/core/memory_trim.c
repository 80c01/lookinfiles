/* Give the memory malloc keeps free back to the system (see memory_trim.h) */
#include "core/memory_trim.h"
#include "core/profile.h"

#include <gio/gio.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

static guint delay_ms = MEMORY_TRIM_DELAY_MS;
static guint timer = 0;
static gboolean running = FALSE; /* a trim on its worker thread */
static gboolean wanted = FALSE;  /* asked again while it ran */
static guint done = 0;

static void trim_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)data;
    (void)cancellable;
    gint64 start = profile_start();
    gint64 before = -1, after = -1, peak = -1;
    if (start != 0) profile_memory(&before, &peak);
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
    if (start != 0) {
        profile_memory(&after, &peak);
        profile_done(start, "malloc_trim: %" G_GINT64_FORMAT " MB given back (%" G_GINT64_FORMAT " MB resident)",
                     (before - after) / 1024, after / 1024);
    }
    g_task_return_boolean(task, TRUE);
}

static gboolean on_timer(gpointer data);

static void on_trimmed(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)res;
    (void)data;
    running = FALSE;
    done++;
    if (wanted) {
        wanted = FALSE;
        memory_trim_soon();
    }
}

static gboolean on_timer(gpointer data) {
    (void)data;
    timer = 0;
    if (running) {
        wanted = TRUE;
        return G_SOURCE_REMOVE;
    }
    running = TRUE;
    GTask* task = g_task_new(NULL, NULL, on_trimmed, NULL);
    g_task_run_in_thread(task, trim_thread);
    g_object_unref(task);
    return G_SOURCE_REMOVE;
}

void memory_trim_soon(void) {
#if defined(__GLIBC__)
    if (timer != 0) {
        g_source_remove(timer);
    }
    timer = g_timeout_add_full(G_PRIORITY_LOW, delay_ms, on_timer, NULL, NULL);
#endif
}

guint memory_trim_count(void) {
    return done;
}

void memory_trim_set_delay_ms(guint ms) {
    delay_ms = ms;
}
