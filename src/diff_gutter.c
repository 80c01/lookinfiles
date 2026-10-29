/* Change bars in the left gutter of a text view: lines added, modified or
 * deleted since the last git commit, like the VS Code editor gutter. The HEAD
 * version is read by git in a worker thread; the line diff runs on the UI
 * thread (debounced while editing). */
#include "diff_gutter.h"
#include "git_utils.h"
#include "line_diff.h"

#include <string.h>

#define DIFF_GUTTER_KEY "diff-gutter"
#define DIFF_GUTTER_DEBOUNCE_MS 250
/* Larger texts are not compared */
#define DIFF_GUTTER_MAX_BYTES (4 * 1024 * 1024)

typedef struct {
    GtkTextView* view;
    GtkWidget* area;
    GtkTextBuffer* buffer; /* ref held */
    gulong changed_id;
    GtkAdjustment* vadj; /* ref held */
    gulong vadj_value_id;
    gulong vadj_changed_id;
    gboolean live;
    gchar* path;      /* file compared, NULL when none */
    gchar* base;      /* HEAD version, NULL when unknown */
    gsize base_len;
    guint8* marks;    /* one LineDiffMark set per buffer line */
    guint n_marks;
    guint generation; /* bumped when path changes: drops stale reads */
    guint debounce_id;
    GCancellable* cancellable;
} DiffGutter;

typedef struct {
    gchar* path;
    guint generation;
    gchar* blob;
    gsize blob_len;
} HeadReadData;

static GList* gutters = NULL;

static void head_read_data_free(gpointer data) {
    HeadReadData* hd = data;
    g_free(hd->path);
    g_free(hd->blob);
    g_free(hd);
}

static void clear_marks(DiffGutter* g) {
    g_clear_pointer(&g->marks, g_free);
    g->n_marks = 0;
    gtk_widget_queue_draw(g->area);
}

static void recompute(DiffGutter* g) {
    if (g->path == NULL || g->base == NULL) {
        clear_marks(g);
        return;
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(g->buffer, &start, &end);
    if (gtk_text_iter_get_offset(&end) > DIFF_GUTTER_MAX_BYTES) {
        clear_marks(g);
        return;
    }
    gchar* text = gtk_text_buffer_get_text(g->buffer, &start, &end, TRUE);
    g_free(g->marks);
    g->marks = line_diff_marks(g->base, g->base_len, text, strlen(text), &g->n_marks);
    g_free(text);
    gtk_widget_queue_draw(g->area);
}

static gboolean on_debounce(gpointer data) {
    DiffGutter* g = data;
    g->debounce_id = 0;
    recompute(g);
    return G_SOURCE_REMOVE;
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    DiffGutter* g = data;
    if (g->path == NULL) {
        return;
    }
    if (!g->live) {
        /* Preview replaced by other content: marks no longer apply */
        diff_gutter_set_file(GTK_WIDGET(g->view), NULL);
        return;
    }
    if (g->debounce_id != 0) {
        g_source_remove(g->debounce_id);
    }
    g->debounce_id = g_timeout_add(DIFF_GUTTER_DEBOUNCE_MS, on_debounce, g);
}

static void head_read_thread(GTask* task, gpointer source, gpointer task_data,
                             GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    HeadReadData* hd = task_data;
    hd->blob = git_read_head_blob(hd->path, &hd->blob_len);
    if (hd->blob != NULL && hd->blob_len > DIFF_GUTTER_MAX_BYTES) {
        g_clear_pointer(&hd->blob, g_free);
    }
    g_task_return_boolean(task, TRUE);
}

static void on_head_read(GObject* source, GAsyncResult* res, gpointer user_data) {
    (void)user_data;
    GTask* task = G_TASK(res);
    if (g_cancellable_is_cancelled(g_task_get_cancellable(task))) {
        return;
    }
    DiffGutter* g = g_object_get_data(source, DIFF_GUTTER_KEY);
    HeadReadData* hd = g_task_get_task_data(task);
    if (g == NULL || hd->generation != g->generation) {
        return;
    }
    g_free(g->base);
    g->base = g_steal_pointer(&hd->blob);
    g->base_len = hd->blob_len;
    recompute(g);
}

static void start_head_read(DiffGutter* g) {
    if (g->cancellable != NULL) {
        g_cancellable_cancel(g->cancellable);
        g_clear_object(&g->cancellable);
    }
    if (g->path == NULL) {
        return;
    }
    g->cancellable = g_cancellable_new();
    HeadReadData* hd = g_new0(HeadReadData, 1);
    hd->path = g_strdup(g->path);
    hd->generation = g->generation;
    GTask* task = g_task_new(g->view, g->cancellable, on_head_read, NULL);
    g_task_set_task_data(task, hd, head_read_data_free);
    g_task_run_in_thread(task, head_read_thread);
    g_object_unref(task);
}

void diff_gutter_set_file(GtkWidget* text_view, const gchar* path) {
    DiffGutter* g = g_object_get_data(G_OBJECT(text_view), DIFF_GUTTER_KEY);
    if (g == NULL) {
        return;
    }
    gboolean same = (g_strcmp0(path, g->path) == 0);
    if (!same) {
        g->generation++;
        g_free(g->path);
        g->path = g_strdup(path);
        g_clear_pointer(&g->base, g_free);
        g->base_len = 0;
        clear_marks(g);
    }
    if (g->debounce_id != 0) {
        g_source_remove(g->debounce_id);
        g->debounce_id = 0;
    }
    start_head_read(g);
}

void diff_gutter_refresh_all(void) {
    for (GList* l = gutters; l != NULL; l = l->next) {
        DiffGutter* g = l->data;
        if (g->path != NULL) {
            start_head_read(g);
        }
    }
}

static void fill_bar(cairo_t* cr, double y, double h, double r, double gr, double b) {
    cairo_set_source_rgb(cr, r, gr, b);
    cairo_rectangle(cr, 0, y, DIFF_GUTTER_WIDTH - 1, h);
    cairo_fill(cr);
}

/* Red triangle pointing into the text at y (between two lines) */
static void fill_deleted(cairo_t* cr, double y) {
    cairo_set_source_rgb(cr, 0xf8 / 255.0, 0x51 / 255.0, 0x49 / 255.0);
    cairo_move_to(cr, 0, y - 4);
    cairo_line_to(cr, DIFF_GUTTER_WIDTH, y);
    cairo_line_to(cr, 0, y + 4);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    (void)width;
    DiffGutter* g = data;
    if (g->marks == NULL || g->n_marks == 0) {
        return;
    }
    GdkRectangle vis;
    gtk_text_view_get_visible_rect(g->view, &vis);
    GtkTextIter it;
    gtk_text_view_get_line_at_y(g->view, &it, vis.y, NULL);
    do {
        gint line = gtk_text_iter_get_line(&it);
        if (line < 0 || (guint)line >= g->n_marks) {
            break;
        }
        gint by = 0;
        gint bh = 0;
        gtk_text_view_get_line_yrange(g->view, &it, &by, &bh);
        gint wy = 0;
        gtk_text_view_buffer_to_window_coords(g->view, GTK_TEXT_WINDOW_LEFT, 0, by, NULL, &wy);
        if (wy > height) {
            break;
        }
        guint8 m = g->marks[line];
        if (m & LINE_DIFF_ADDED) {
            fill_bar(cr, wy, bh, 0x2e / 255.0, 0xa0 / 255.0, 0x43 / 255.0);
        } else if (m & LINE_DIFF_MODIFIED) {
            fill_bar(cr, wy, bh, 0x00 / 255.0, 0x78 / 255.0, 0xd4 / 255.0);
        }
        if (m & LINE_DIFF_DELETED_ABOVE) {
            fill_deleted(cr, wy);
        }
        if (m & LINE_DIFF_DELETED_BELOW) {
            fill_deleted(cr, wy + bh);
        }
    } while (gtk_text_iter_forward_line(&it));
}

static void on_scrolled(GtkAdjustment* adj, gpointer data) {
    (void)adj;
    gtk_widget_queue_draw(((DiffGutter*)data)->area);
}

static void track_vadjustment(DiffGutter* g) {
    if (g->vadj != NULL) {
        g_signal_handler_disconnect(g->vadj, g->vadj_value_id);
        g_signal_handler_disconnect(g->vadj, g->vadj_changed_id);
        g_clear_object(&g->vadj);
    }
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(g->view));
    if (adj != NULL) {
        g->vadj = g_object_ref(adj);
        g->vadj_value_id = g_signal_connect(adj, "value-changed", G_CALLBACK(on_scrolled), g);
        g->vadj_changed_id = g_signal_connect(adj, "changed", G_CALLBACK(on_scrolled), g);
    }
}

static void on_vadjustment_notify(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    track_vadjustment(data);
}

static void diff_gutter_free(gpointer data) {
    DiffGutter* g = data;
    gutters = g_list_remove(gutters, g);
    if (g->cancellable != NULL) {
        g_cancellable_cancel(g->cancellable);
        g_object_unref(g->cancellable);
    }
    if (g->debounce_id != 0) {
        g_source_remove(g->debounce_id);
    }
    g_signal_handler_disconnect(g->buffer, g->changed_id);
    g_object_unref(g->buffer);
    if (g->vadj != NULL) {
        g_signal_handler_disconnect(g->vadj, g->vadj_value_id);
        g_signal_handler_disconnect(g->vadj, g->vadj_changed_id);
        g_object_unref(g->vadj);
    }
    g_free(g->path);
    g_free(g->base);
    g_free(g->marks);
    g_free(g);
}

void diff_gutter_attach(GtkWidget* text_view, gboolean live) {
    if (g_object_get_data(G_OBJECT(text_view), DIFF_GUTTER_KEY) != NULL) {
        return;
    }
    DiffGutter* g = g_new0(DiffGutter, 1);
    g->view = GTK_TEXT_VIEW(text_view);
    g->live = live;
    g->buffer = g_object_ref(gtk_text_view_get_buffer(g->view));
    g->changed_id = g_signal_connect(g->buffer, "changed", G_CALLBACK(on_buffer_changed), g);

    g->area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(g->area), DIFF_GUTTER_WIDTH);
    gtk_widget_set_size_request(g->area, DIFF_GUTTER_WIDTH, -1);
    gtk_widget_set_can_target(g->area, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g->area), on_draw, g, NULL);
    gtk_text_view_set_gutter(g->view, GTK_TEXT_WINDOW_LEFT, g->area);

    track_vadjustment(g);
    g_signal_connect(text_view, "notify::vadjustment", G_CALLBACK(on_vadjustment_notify), g);

    g_object_set_data_full(G_OBJECT(text_view), DIFF_GUTTER_KEY, g, diff_gutter_free);
    gutters = g_list_prepend(gutters, g);
}
