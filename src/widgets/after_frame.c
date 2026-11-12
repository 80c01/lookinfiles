/* Run a function after a widget's next frame (see after_frame.h). */
#include "widgets/after_frame.h"

struct AfterFrame {
    GSourceFunc fn;
    gpointer data;
    GDestroyNotify destroy;
    GdkFrameClock* clock;
    gulong paint_id;
    guint wait_id;
    guint idle_id;
};

static void after_frame_free(AfterFrame* af) {
    if (af->paint_id != 0) {
        g_signal_handler_disconnect(af->clock, af->paint_id);
    }
    g_object_unref(af->clock);
    if (af->wait_id != 0) {
        g_source_remove(af->wait_id);
    }
    if (af->idle_id != 0) {
        g_source_remove(af->idle_id);
    }
    if (af->destroy != NULL && af->data != NULL) {
        af->destroy(af->data);
    }
    g_free(af);
}

static gboolean on_idle(gpointer data) {
    AfterFrame* af = data;
    af->idle_id = 0;
    af->fn(af->data);
    after_frame_free(af);
    return G_SOURCE_REMOVE;
}

/* Painted (or waited enough): run out of the frame clock */
static void schedule(AfterFrame* af) {
    if (af->paint_id != 0) {
        g_signal_handler_disconnect(af->clock, af->paint_id);
        af->paint_id = 0;
    }
    if (af->wait_id != 0) {
        g_source_remove(af->wait_id);
        af->wait_id = 0;
    }
    if (af->idle_id == 0) {
        /* Above idle priority: a text view validating a large buffer keeps
         * the main loop busy just below the redraw priority */
        af->idle_id = g_idle_add_full(G_PRIORITY_DEFAULT, on_idle, af, NULL);
    }
}

static void on_after_paint(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    schedule(data);
}

static gboolean on_wait_over(gpointer data) {
    AfterFrame* af = data;
    af->wait_id = 0;
    schedule(af);
    return G_SOURCE_REMOVE;
}

AfterFrame* after_frame_new(GtkWidget* widget, guint max_wait_ms, GSourceFunc fn, gpointer data,
                            GDestroyNotify destroy) {
    GdkFrameClock* clock = (widget != NULL) ? gtk_widget_get_frame_clock(widget) : NULL;
    if (clock == NULL) {
        return NULL;
    }
    AfterFrame* af = g_new0(AfterFrame, 1);
    af->fn = fn;
    af->data = data;
    af->destroy = destroy;
    af->clock = g_object_ref(clock);
    af->paint_id = g_signal_connect(clock, "after-paint", G_CALLBACK(on_after_paint), af);
    af->wait_id = g_timeout_add(max_wait_ms, on_wait_over, af);
    gtk_widget_queue_draw(widget);
    return af;
}

void after_frame_cancel(AfterFrame* af) {
    if (af != NULL) {
        after_frame_free(af);
    }
}
