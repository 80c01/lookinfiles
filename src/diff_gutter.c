/* Left gutter of a text view: optional line numbers, then change bars for
 * the lines added, modified or deleted since the last git commit, like the
 * VS Code editor gutter. The HEAD version is read by git in a worker thread;
 * the line diff runs on the UI thread (debounced while editing). */
#include "diff_gutter.h"
#include "git_utils.h"
#include "core/line_diff.h"
#include "theme/palette.h"

#include <pango/pangocairo.h>
#include <string.h>

#define DIFF_GUTTER_KEY "diff-gutter"
#define DIFF_GUTTER_DEBOUNCE_MS 250
/* Larger texts are not compared */
#define DIFF_GUTTER_MAX_BYTES (4 * 1024 * 1024)
/* Line number column: space before and after the digits, minimum digits */
#define NUMBERS_PAD_LEFT 8
#define NUMBERS_PAD_RIGHT 8
#define NUMBERS_MIN_DIGITS 2

typedef struct {
    GtkTextView* view;
    GtkWidget* area;
    GtkWidget* companion; /* weak pointer, redrawn with the gutter */
    GtkTextBuffer* buffer; /* ref held */
    gulong changed_id;
    gulong mark_set_id;
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
    gint numbers_from; /* buffer line numbered 1, -1 when numbers are hidden */
    gint width;        /* current gutter width */
    guint width_idle_id;
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

static void marks_changed(DiffGutter* g) {
    gtk_widget_queue_draw(g->area);
    if (g->companion != NULL) {
        gtk_widget_queue_draw(g->companion);
    }
}

static void clear_marks(DiffGutter* g) {
    gboolean had = (g->marks != NULL);
    g_clear_pointer(&g->marks, g_free);
    g->n_marks = 0;
    if (had) {
        marks_changed(g);
    }
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
    marks_changed(g);
}

/* Gutter width: change bar, plus the number column when shown */
static gint wanted_width(DiffGutter* g) {
    if (g->numbers_from < 0) {
        return DIFF_GUTTER_WIDTH;
    }
    gint last = gtk_text_buffer_get_line_count(g->buffer) - g->numbers_from;
    gint digits = 1;
    for (gint n = MAX(last, 1); n >= 10; n /= 10) {
        digits++;
    }
    digits = MAX(digits, NUMBERS_MIN_DIGITS);
    PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(g->view), "0");
    gint digit_w = 0;
    pango_layout_get_pixel_size(layout, &digit_w, NULL);
    g_object_unref(layout);
    return NUMBERS_PAD_LEFT + digits * digit_w + NUMBERS_PAD_RIGHT + DIFF_GUTTER_WIDTH;
}

static void update_width(DiffGutter* g) {
    gint w = wanted_width(g);
    if (w != g->width) {
        g->width = w;
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(g->area), w);
        gtk_widget_set_size_request(g->area, w, -1);
    }
    gtk_widget_queue_draw(g->area);
}

static gboolean on_width_idle(gpointer data) {
    DiffGutter* g = data;
    g->width_idle_id = 0;
    update_width(g);
    return G_SOURCE_REMOVE;
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
    if (g->numbers_from >= 0) {
        if (g->live) {
            update_width(g);
        } else {
            /* Preview replaced by other content: numbers no longer apply */
            diff_gutter_set_line_numbers(GTK_WIDGET(g->view), -1);
        }
    }
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
    /* Files changed elsewhere, HEAD did not: the marks are still right (the
     * line diff runs on the UI thread, not worth repeating) */
    if (g->base != NULL && hd->blob != NULL && g->base_len == hd->blob_len &&
        memcmp(g->base, hd->blob, hd->blob_len) == 0) {
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

void diff_gutter_set_line_numbers(GtkWidget* text_view, gint first_line) {
    DiffGutter* g = g_object_get_data(G_OBJECT(text_view), DIFF_GUTTER_KEY);
    if (g == NULL) {
        return;
    }
    gint from = (first_line < 0) ? -1 : first_line;
    if (from == g->numbers_from) {
        return;
    }
    g->numbers_from = from;
    update_width(g);
}

gboolean diff_gutter_get_line_numbers(GtkWidget* text_view) {
    DiffGutter* g = g_object_get_data(G_OBJECT(text_view), DIFF_GUTTER_KEY);
    return g != NULL && g->numbers_from >= 0;
}

const guint8* diff_gutter_get_marks(GtkWidget* text_view, guint* n_lines) {
    DiffGutter* g = g_object_get_data(G_OBJECT(text_view), DIFF_GUTTER_KEY);
    *n_lines = (g != NULL && g->marks != NULL) ? g->n_marks : 0;
    return (*n_lines > 0) ? g->marks : NULL;
}

void diff_gutter_add_companion(GtkWidget* text_view, GtkWidget* widget) {
    DiffGutter* g = g_object_get_data(G_OBJECT(text_view), DIFF_GUTTER_KEY);
    if (g == NULL || g->companion == widget) {
        return;
    }
    if (g->companion != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(g->companion), (gpointer*)&g->companion);
    }
    g->companion = widget;
    if (widget != NULL) {
        g_object_add_weak_pointer(G_OBJECT(widget), (gpointer*)&g->companion);
    }
}

void diff_gutter_refresh_all(void) {
    for (GList* l = gutters; l != NULL; l = l->next) {
        DiffGutter* g = l->data;
        if (g->path != NULL) {
            start_head_read(g);
        }
    }
}

static void set_source_role(cairo_t* cr, PaletteRole role) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
}

static void fill_bar(cairo_t* cr, double x, double y, double h, PaletteRole role) {
    set_source_role(cr, role);
    cairo_rectangle(cr, x, y, DIFF_GUTTER_WIDTH - 1, h);
    cairo_fill(cr);
}

/* Red triangle pointing into the text at y (between two lines) */
static void fill_deleted(cairo_t* cr, double x, double y) {
    set_source_role(cr, PAL_GUTTER_DELETED);
    cairo_move_to(cr, x, y - 4);
    cairo_line_to(cr, x + DIFF_GUTTER_WIDTH, y);
    cairo_line_to(cr, x, y + 4);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* Line number right-aligned before the change bar (the cursor line
 * is brighter in editors) */
static void draw_number(DiffGutter* g, cairo_t* cr, PangoLayout* layout, gint number,
                        gboolean current, double y, double bar_x) {
    gchar buf[16];
    g_snprintf(buf, sizeof buf, "%d", number);
    pango_layout_set_text(layout, buf, -1);
    gint w = 0;
    pango_layout_get_pixel_size(layout, &w, NULL);
    if (current) {
        set_source_role(cr, PAL_FG);
    } else {
        set_source_role(cr, PAL_FG_SUBTLE);
    }
    cairo_move_to(cr, bar_x - NUMBERS_PAD_RIGHT - w, y + gtk_text_view_get_pixels_above_lines(g->view));
    pango_cairo_show_layout(cr, layout);
}

/* Move to the next line; FALSE on the last one. gtk_text_iter_forward_line()
 * returns FALSE on reaching an empty last line, which must be drawn too. */
static gboolean next_line(GtkTextIter* it) {
    gint line = gtk_text_iter_get_line(it);
    gtk_text_iter_forward_line(it);
    return gtk_text_iter_get_line(it) != line;
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    DiffGutter* g = data;
    gboolean numbers = (g->numbers_from >= 0);
    if (numbers && g->width_idle_id == 0 && wanted_width(g) != g->width) {
        /* Font or line count changed: resize outside of drawing */
        g->width_idle_id = g_idle_add(on_width_idle, g);
    }
    if (!numbers && (g->marks == NULL || g->n_marks == 0)) {
        return;
    }
    double bar_x = numbers ? width - DIFF_GUTTER_WIDTH : 0;
    PangoLayout* layout = NULL;
    gint cursor_line = -1;
    if (numbers) {
        layout = gtk_widget_create_pango_layout(GTK_WIDGET(g->view), NULL);
        if (g->live) {
            GtkTextIter cur;
            gtk_text_buffer_get_iter_at_mark(g->buffer, &cur, gtk_text_buffer_get_insert(g->buffer));
            cursor_line = gtk_text_iter_get_line(&cur);
        }
    }
    GdkRectangle vis;
    gtk_text_view_get_visible_rect(g->view, &vis);
    GtkTextIter it;
    gtk_text_view_get_line_at_y(g->view, &it, vis.y, NULL);
    do {
        gint line = gtk_text_iter_get_line(&it);
        gint by = 0;
        gint bh = 0;
        gtk_text_view_get_line_yrange(g->view, &it, &by, &bh);
        gint wy = 0;
        gtk_text_view_buffer_to_window_coords(g->view, GTK_TEXT_WINDOW_LEFT, 0, by, NULL, &wy);
        if (wy > height) {
            break;
        }
        if (numbers && line >= g->numbers_from) {
            draw_number(g, cr, layout, line - g->numbers_from + 1, line == cursor_line, wy, bar_x);
        }
        if (g->marks == NULL || line < 0 || (guint)line >= g->n_marks) {
            continue;
        }
        guint8 m = g->marks[line];
        if (m & LINE_DIFF_ADDED) {
            fill_bar(cr, bar_x, wy, bh, PAL_GUTTER_ADDED);
        } else if (m & LINE_DIFF_MODIFIED) {
            fill_bar(cr, bar_x, wy, bh, PAL_GUTTER_MODIFIED);
        }
        if (m & LINE_DIFF_DELETED_ABOVE) {
            fill_deleted(cr, bar_x, wy);
        }
        if (m & LINE_DIFF_DELETED_BELOW) {
            fill_deleted(cr, bar_x, wy + bh);
        }
    } while (next_line(&it));
    if (layout != NULL) {
        g_object_unref(layout);
    }
}

/* The cursor line number is highlighted: follow the cursor */
static void on_mark_set(GtkTextBuffer* buffer, const GtkTextIter* iter, GtkTextMark* mark,
                        gpointer data) {
    (void)iter;
    DiffGutter* g = data;
    if (g->live && g->numbers_from >= 0 && mark == gtk_text_buffer_get_insert(buffer)) {
        gtk_widget_queue_draw(g->area);
    }
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
    if (g->width_idle_id != 0) {
        g_source_remove(g->width_idle_id);
    }
    g_signal_handler_disconnect(g->buffer, g->changed_id);
    g_signal_handler_disconnect(g->buffer, g->mark_set_id);
    g_object_unref(g->buffer);
    if (g->vadj != NULL) {
        g_signal_handler_disconnect(g->vadj, g->vadj_value_id);
        g_signal_handler_disconnect(g->vadj, g->vadj_changed_id);
        g_object_unref(g->vadj);
    }
    if (g->companion != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(g->companion), (gpointer*)&g->companion);
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
    g->mark_set_id = g_signal_connect(g->buffer, "mark-set", G_CALLBACK(on_mark_set), g);
    g->numbers_from = -1;
    g->width = DIFF_GUTTER_WIDTH;

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
