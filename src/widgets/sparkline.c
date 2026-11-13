/* A small line chart of the last values of some series (see sparkline.h) */
#include "widgets/sparkline.h"

#define SPARKLINE_KEY "lookin-sparkline"
#define LINE_WIDTH 1.5
#define FILL_ALPHA 0.22

typedef struct {
    PaletteRole role;
    gdouble* values; /* ring of capacity values */
    guint count;     /* values pushed, up to capacity */
    guint next;      /* where the next value goes */
} Series;

typedef struct {
    guint capacity;
    GArray* series; /* Series */
    gdouble max;    /* fixed scale, 0: automatic */
    gboolean grid;
} Sparkline;

static Sparkline* of_chart(GtkWidget* chart) {
    return chart != NULL ? g_object_get_data(G_OBJECT(chart), SPARKLINE_KEY) : NULL;
}

static void sparkline_free(gpointer data) {
    Sparkline* s = data;
    for (guint i = 0; i < s->series->len; i++) g_free(g_array_index(s->series, Series, i).values);
    g_array_unref(s->series);
    g_free(s);
}

/* The i-th value from the oldest one kept */
static gdouble value_at(const Sparkline* s, const Series* se, guint i) {
    guint first = se->count < s->capacity ? 0 : se->next;
    return se->values[(first + i) % s->capacity];
}

static gdouble scale_max(const Sparkline* s) {
    if (s->max > 0) return s->max;
    gdouble top = 0;
    for (guint k = 0; k < s->series->len; k++) {
        const Series* se = &g_array_index(s->series, Series, k);
        for (guint i = 0; i < se->count; i++) top = MAX(top, value_at(s, se, i));
    }
    return top > 0 ? top * 1.1 : 1.0;
}

static void trace(cairo_t* cr, const Sparkline* s, const Series* se, double w, double h, gdouble top) {
    double step = w / (double)(s->capacity - 1);
    double x0 = w - step * (double)(se->count - 1);
    for (guint i = 0; i < se->count; i++) {
        gdouble v = CLAMP(value_at(s, se, i), 0.0, top);
        double x = x0 + step * (double)i;
        double y = h - LINE_WIDTH / 2.0 - (h - LINE_WIDTH) * v / top;
        if (i == 0) {
            cairo_move_to(cr, x, y);
        } else {
            cairo_line_to(cr, x, y);
        }
    }
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    Sparkline* s = data;
    double w = width, h = height;
    double r, g, b;
    if (s->grid) {
        palette_rgb(PAL_BORDER, &r, &g, &b);
        cairo_set_source_rgb(cr, r, g, b);
        cairo_set_line_width(cr, 1.0);
        for (gint q = 1; q < 4; q++) {
            double y = (double)(gint)(h * q / 4.0) + 0.5;
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, w, y);
        }
        cairo_stroke(cr);
    }
    gdouble top = scale_max(s);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (guint k = 0; k < s->series->len; k++) {
        const Series* se = &g_array_index(s->series, Series, k);
        if (se->count < 2) continue;
        palette_rgb(se->role, &r, &g, &b);
        double step = w / (double)(s->capacity - 1);
        trace(cr, s, se, w, h, top);
        cairo_line_to(cr, w, h);
        cairo_line_to(cr, w - step * (double)(se->count - 1), h);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, r, g, b, FILL_ALPHA);
        cairo_fill(cr);
        trace(cr, s, se, w, h, top);
        cairo_set_source_rgb(cr, r, g, b);
        cairo_set_line_width(cr, LINE_WIDTH);
        cairo_stroke(cr);
    }
}

GtkWidget* sparkline_new(guint capacity) {
    Sparkline* s = g_new0(Sparkline, 1);
    s->capacity = MAX(capacity, 2);
    s->series = g_array_new(FALSE, TRUE, sizeof(Series));
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_add_css_class(area, "sparkline");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, s, NULL);
    g_object_set_data_full(G_OBJECT(area), SPARKLINE_KEY, s, sparkline_free);
    return area;
}

guint sparkline_add_series(GtkWidget* chart, PaletteRole role) {
    Sparkline* s = of_chart(chart);
    if (s == NULL) return 0;
    Series se = { role, g_new0(gdouble, s->capacity), 0, 0 };
    g_array_append_val(s->series, se);
    return s->series->len - 1;
}

void sparkline_push(GtkWidget* chart, guint series, gdouble value) {
    Sparkline* s = of_chart(chart);
    if (s == NULL || series >= s->series->len) return;
    Series* se = &g_array_index(s->series, Series, series);
    se->values[se->next] = value;
    se->next = (se->next + 1) % s->capacity;
    if (se->count < s->capacity) se->count++;
    gtk_widget_queue_draw(chart);
}

void sparkline_set_max(GtkWidget* chart, gdouble max) {
    Sparkline* s = of_chart(chart);
    if (s == NULL) return;
    s->max = max > 0 ? max : 0;
    gtk_widget_queue_draw(chart);
}

gdouble sparkline_get_max(GtkWidget* chart) {
    Sparkline* s = of_chart(chart);
    return s != NULL ? scale_max(s) : 1.0;
}

void sparkline_set_grid(GtkWidget* chart, gboolean grid) {
    Sparkline* s = of_chart(chart);
    if (s == NULL) return;
    s->grid = grid;
    gtk_widget_queue_draw(chart);
}

void sparkline_clear(GtkWidget* chart) {
    Sparkline* s = of_chart(chart);
    if (s == NULL) return;
    for (guint k = 0; k < s->series->len; k++) {
        Series* se = &g_array_index(s->series, Series, k);
        se->count = se->next = 0;
    }
    gtk_widget_queue_draw(chart);
}
