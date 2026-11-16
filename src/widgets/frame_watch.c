/* Frame timings for the performance probe (see frame_watch.h) */
#include "widgets/frame_watch.h"
#include "core/profile.h"

/* Our handlers run after GTK's own for each phase (connected after them, at
 * the window's realize): each one marks the end of its phase */
static gint64 frame_start_us = 0;
static gint64 phase_end_us = 0; /* end of the last phase run */
static ProfileFramePhases phases;
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
    phase_end_us = frame_start_us;
    phases = (ProfileFramePhases){-1, -1, -1};
    frame_painted = FALSE;
}

/* The time since the end of the previous phase */
static gint64 phase_done(void) {
    gint64 now = g_get_monotonic_time();
    gint64 spent = now - phase_end_us;
    phase_end_us = now;
    return spent;
}

static void on_update(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    if (frame_start_us != 0) phases.update_us = phase_done();
}

static void on_layout(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    if (frame_start_us != 0) phases.layout_us = phase_done();
}

static void on_paint(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    if (frame_start_us != 0) phases.paint_us = phase_done();
    frame_painted = TRUE;
}

static void on_after_paint(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    if (frame_start_us == 0) {
        return;
    }
    profile_frame(g_get_monotonic_time() - frame_start_us, frame_painted, &phases);
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
    g_signal_connect(clock, "update", G_CALLBACK(on_update), NULL);
    g_signal_connect(clock, "layout", G_CALLBACK(on_layout), NULL);
    g_signal_connect(clock, "paint", G_CALLBACK(on_paint), NULL);
    g_signal_connect(clock, "after-paint", G_CALLBACK(on_after_paint), NULL);
}

void frame_watch_attach(GtkWidget* window) {
    if (!profile_enabled()) {
        return;
    }
    g_signal_connect(window, "realize", G_CALLBACK(on_realize), NULL);
}
