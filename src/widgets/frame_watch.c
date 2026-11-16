/* Frame timings for the performance probe (see frame_watch.h) */
#include "widgets/frame_watch.h"
#include "core/profile.h"

static gint64 frame_start_us = 0;
static gboolean frame_painted = FALSE;
static gboolean first_frame_seen = FALSE;

static gboolean on_startup_idle(gpointer data) {
    (void)data;
    profile_startup_done();
    return G_SOURCE_REMOVE;
}

static void on_before_paint(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    frame_start_us = g_get_monotonic_time();
    frame_painted = FALSE;
}

static void on_paint(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    frame_painted = TRUE;
}

static void on_after_paint(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    if (frame_start_us == 0) {
        return;
    }
    profile_frame(g_get_monotonic_time() - frame_start_us, frame_painted);
    frame_start_us = 0;
    if (!first_frame_seen) {
        first_frame_seen = TRUE;
        profile_mark("first frame");
        /* Below the redraws and the results already queued */
        g_idle_add_full(G_PRIORITY_LOW, on_startup_idle, NULL, NULL);
    }
}

static void on_realize(GtkWidget* window, gpointer data) {
    (void)data;
    GdkFrameClock* clock = gtk_widget_get_frame_clock(window);
    if (clock == NULL) {
        return;
    }
    g_signal_connect(clock, "before-paint", G_CALLBACK(on_before_paint), NULL);
    g_signal_connect(clock, "paint", G_CALLBACK(on_paint), NULL);
    g_signal_connect(clock, "after-paint", G_CALLBACK(on_after_paint), NULL);
}

void frame_watch_attach(GtkWidget* window) {
    if (!profile_enabled()) {
        return;
    }
    g_signal_connect(window, "realize", G_CALLBACK(on_realize), NULL);
}
