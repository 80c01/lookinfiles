/* "Rendering…" notice of the preview (see preview_busy.h). */
#include "preview/preview_busy.h"

#include "core/i18n.h"
#include "widgets/after_frame.h"

/* Previews ready sooner show no notice (it would only flicker) */
#define PREVIEW_BUSY_DELAY_MS 150
/* Opacity of the previous preview while the new one renders */
#define PREVIEW_BUSY_DIM 0.45
/* Displays expected to take longer show the notice first */
#define SLOW_DISPLAY_US (100 * 1000)
/* Longest wait for the frame drawing the notice (window hidden: none comes) */
#define NOTICE_FRAME_WAIT_MS 50
/* Displays of fewer bytes say little about the cost per byte */
#define LEARN_MIN_BYTES 4096

typedef struct PreviewBusy {
    GtkWidget* content;
    GtkWidget* notice;
    GtkWidget* spinner;
    GtkWidget* label;
    gchar* name;               /* file being rendered */
    GCancellable* cancellable; /* its rendering, or NULL */
    gulong cancelled_id;
    guint timer_id;
    guint hide_id;             /* hiding after a cancellation, or 0 */

    AfterFrame* then; /* display waiting for the notice to be drawn, or NULL */
} PreviewBusy;

/* A display of preview_busy_show_then() */
typedef struct {
    PreviewBusy* pb;
    GSourceFunc fn;
    gpointer data;
    GDestroyNotify destroy;
} BusyThen;

/* Microseconds per KB of each kind of display, learnt from the displays
 * shown; the start values are those of a slow machine */
static gdouble us_per_kb[PREVIEW_DISPLAY_RENDERED + 1] = {
    [PREVIEW_DISPLAY_TEXT] = 100.0,
    [PREVIEW_DISPLAY_DOC] = 500.0,
    [PREVIEW_DISPLAY_RENDERED] = 2000.0,
};

static void then_cancel(PreviewBusy* pb) {
    AfterFrame* af = pb->then;
    pb->then = NULL;
    after_frame_cancel(af);
}

static void busy_forget(PreviewBusy* pb) {
    if (pb->cancellable != NULL) {
        g_signal_handler_disconnect(pb->cancellable, pb->cancelled_id);
        pb->cancelled_id = 0;
        g_clear_object(&pb->cancellable);
    }
}

static void busy_hide(PreviewBusy* pb) {
    then_cancel(pb);
    if (pb->timer_id != 0) {
        g_source_remove(pb->timer_id);
        pb->timer_id = 0;
    }
    if (pb->hide_id != 0) {
        g_source_remove(pb->hide_id);
        pb->hide_id = 0;
    }
    busy_forget(pb);
    g_clear_pointer(&pb->name, g_free);
    if (gtk_widget_get_visible(pb->notice)) {
        gtk_spinner_stop(GTK_SPINNER(pb->spinner));
        gtk_widget_set_visible(pb->notice, FALSE);
        gtk_widget_set_opacity(pb->content, 1.0);
    }
}

static void preview_busy_free(gpointer data) {
    PreviewBusy* pb = data;
    then_cancel(pb);
    if (pb->timer_id != 0) {
        g_source_remove(pb->timer_id);
    }
    if (pb->hide_id != 0) {
        g_source_remove(pb->hide_id);
    }
    busy_forget(pb);
    g_free(pb->name);
    g_free(pb);
}

static gboolean on_delay(gpointer data) {
    PreviewBusy* pb = data;
    pb->timer_id = 0;
    /* TRANSLATORS: %s is the name of the file whose preview is being built */
    gchar* text = g_strdup_printf(_("Rendering %s…"), pb->name != NULL ? pb->name : "");
    gtk_label_set_text(GTK_LABEL(pb->label), text);
    g_free(text);
    gtk_widget_set_opacity(pb->content, PREVIEW_BUSY_DIM);
    gtk_spinner_start(GTK_SPINNER(pb->spinner));
    gtk_widget_set_visible(pb->notice, TRUE);
    return G_SOURCE_REMOVE;
}

static gboolean on_hide(gpointer data) {
    PreviewBusy* pb = data;
    pb->hide_id = 0;
    busy_hide(pb);
    return G_SOURCE_REMOVE;
}

/* Cancelled on the UI thread: another entry or a newer rendering replaced
 * it. Hidden once idle, so that a rendering started right after (the next
 * file) keeps the notice up instead of waiting the delay again. */
static void on_cancelled(GCancellable* cancellable, gpointer data) {
    (void)cancellable;
    PreviewBusy* pb = data;
    then_cancel(pb);
    busy_forget(pb);
    if (pb->timer_id != 0) {
        g_source_remove(pb->timer_id);
        pb->timer_id = 0;
    }
    if (pb->hide_id == 0) {
        /* Above idle priority: the text view validating a large preview
         * keeps the main loop busy just below the redraw priority */
        pb->hide_id = g_idle_add_full(G_PRIORITY_DEFAULT, on_hide, pb, NULL);
    }
}

GtkWidget* preview_busy_wrap(GtkWidget* content) {
    PreviewBusy* pb = g_new0(PreviewBusy, 1);
    pb->content = content;

    pb->notice = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(pb->notice, "preview-busy");
    gtk_widget_set_halign(pb->notice, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(pb->notice, GTK_ALIGN_START);
    gtk_widget_set_margin_top(pb->notice, 24);
    gtk_widget_set_margin_start(pb->notice, 16);
    gtk_widget_set_margin_end(pb->notice, 16);
    /* Clicks and scrolling still reach the previous preview */
    gtk_widget_set_can_target(pb->notice, FALSE);
    pb->spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(pb->notice), pb->spinner);
    pb->label = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(pb->label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(pb->notice), pb->label);
    gtk_widget_set_visible(pb->notice, FALSE);

    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), content);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), pb->notice);
    g_object_set_data_full(G_OBJECT(overlay), "preview-busy", pb, preview_busy_free);
    return overlay;
}

void preview_busy_attach(AppData* app, GtkWidget* wrap) {
    app->preview_busy = g_object_get_data(G_OBJECT(wrap), "preview-busy");
}

void preview_busy_start(AppData* app, const gchar* name, GCancellable* cancellable) {
    PreviewBusy* pb = app->preview_busy;
    if (pb == NULL || cancellable == NULL || g_cancellable_is_cancelled(cancellable)) {
        return;
    }
    gboolean shown = gtk_widget_get_visible(pb->notice);
    gchar* label = g_strdup(name);
    busy_hide(pb);
    pb->name = label;
    pb->cancellable = g_object_ref(cancellable);
    pb->cancelled_id = g_signal_connect(cancellable, "cancelled", G_CALLBACK(on_cancelled), pb);
    if (shown) {
        on_delay(pb); /* still rendering: the notice moves to the new file at once */
    } else {
        pb->timer_id = g_timeout_add(PREVIEW_BUSY_DELAY_MS, on_delay, pb);
    }
}

void preview_busy_stop(AppData* app) {
    if (app->preview_busy != NULL) {
        busy_hide(app->preview_busy);
    }
}

gboolean preview_busy_expect_slow(PreviewDisplay kind, gsize bytes) {
    return us_per_kb[kind] * ((gdouble)bytes / 1024.0) > SLOW_DISPLAY_US;
}

void preview_busy_learn(PreviewDisplay kind, gsize bytes, gint64 us) {
    if (bytes < LEARN_MIN_BYTES || us < 0) {
        return;
    }
    /* Half the last measure: one unusual display does not decide alone */
    gdouble measured = (gdouble)us / ((gdouble)bytes / 1024.0);
    us_per_kb[kind] = (us_per_kb[kind] + measured) / 2.0;
}

static void busy_then_free(gpointer data) {
    BusyThen* bt = data;
    if (bt->destroy != NULL && bt->data != NULL) {
        bt->destroy(bt->data);
    }
    g_free(bt);
}

static gboolean run_then(gpointer data) {
    BusyThen* bt = data;
    bt->pb->then = NULL; /* freed after this run */
    return bt->fn(bt->data);
}

void preview_busy_show_then(AppData* app, const gchar* name, GCancellable* cancellable,
                            GSourceFunc then, gpointer data, GDestroyNotify destroy) {
    PreviewBusy* pb = app->preview_busy;
    gboolean on_screen = pb != NULL && gtk_widget_get_mapped(pb->content);
    if (!on_screen || cancellable == NULL || g_cancellable_is_cancelled(cancellable)) {
        /* No notice to draw (preview not on screen): display now */
        if (cancellable == NULL || !g_cancellable_is_cancelled(cancellable)) {
            then(data);
        }
        if (destroy != NULL && data != NULL) {
            destroy(data);
        }
        return;
    }
    if (pb->cancellable != cancellable) {
        preview_busy_start(app, name, cancellable);
    } else {
        then_cancel(pb);
    }
    if (pb->timer_id != 0) {
        g_source_remove(pb->timer_id);
        pb->timer_id = 0;
    }
    if (pb->name == NULL) {
        pb->name = g_strdup(name);
    }
    on_delay(pb);
    BusyThen* bt = g_new0(BusyThen, 1);
    bt->pb = pb;
    bt->fn = then;
    bt->data = data;
    bt->destroy = destroy;
    pb->then = after_frame_new(pb->content, NOTICE_FRAME_WAIT_MS, run_then, bt, busy_then_free);
    if (pb->then == NULL) {
        run_then(bt);
        busy_then_free(bt);
    }
}
