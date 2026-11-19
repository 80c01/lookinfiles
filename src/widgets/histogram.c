/* A bar chart of files in a few groups (see histogram.h) */
#include "widgets/histogram.h"
#include "core/i18n.h"
#include "theme/palette.h"

#define HISTOGRAM_KEY "lookin-histogram"
#define LEGEND_H 18.0  /* strip of the legend, at the top */
#define LABEL_H 16.0   /* strip of the labels, at the bottom */
#define BAR_MAX_W 16.0 /* widest bar */
#define SWATCH 9.0     /* square of a colour in the legend */
#define COUNT_ROLE PAL_ICON_BLUE
#define SIZE_ROLE PAL_ICON_ORANGE

typedef struct {
    gchar* label;
    gchar* tip;
    guint64 count;
    guint64 size;
} Bin;

typedef struct {
    GtkWidget* area;
    GArray* bins; /* Bin */
    gint hover;   /* group under the pointer, -1: none */
} Histogram;

static void clear_bin(gpointer data) {
    Bin* b = data;
    g_free(b->label);
    g_free(b->tip);
}

static void set_color(cairo_t* cr, PaletteRole role, gdouble alpha) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, alpha);
}

/* text in one line, centred on cx (cut to width) or from x when not centred */
static gdouble draw_text(cairo_t* cr, PangoLayout* layout, const gchar* text, gdouble x, gdouble y, gdouble width,
                         gboolean centred, PaletteRole role) {
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, width > 0.0 ? (int)(width * PANGO_SCALE) : -1);
    pango_layout_set_alignment(layout, centred ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    set_color(cr, role, 1.0);
    cairo_move_to(cr, centred ? x - width / 2.0 : x, y);
    pango_cairo_show_layout(cr, layout);
    return (gdouble)w;
}

static gdouble legend_item(cairo_t* cr, PangoLayout* layout, gdouble x, PaletteRole role, const gchar* name) {
    set_color(cr, role, 0.85);
    cairo_rectangle(cr, x, (LEGEND_H - SWATCH) / 2.0, SWATCH, SWATCH);
    cairo_fill(cr);
    x += SWATCH + 4.0;
    return x + draw_text(cr, layout, name, x, 1.0, -1.0, FALSE, PAL_FG_MUTED) + 14.0;
}

/* Height of a bar for value out of top over h (a pixel at least when not 0) */
static gdouble bar_height(guint64 value, guint64 top, gdouble h) {
    if (value == 0 || top == 0) return 0.0;
    return MAX(1.0, h * (gdouble)value / (gdouble)top);
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    Histogram* hg = data;
    guint n = hg->bins->len;
    PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    gdouble x = legend_item(cr, layout, 0.0, COUNT_ROLE, _("Files"));
    legend_item(cr, layout, x, SIZE_ROLE, _("Size"));
    gdouble plot_h = (gdouble)height - LEGEND_H - LABEL_H - 4.0;
    if (n == 0 || plot_h <= 4.0) {
        g_object_unref(layout);
        return;
    }
    guint64 top_count = 0, top_size = 0;
    for (guint i = 0; i < n; i++) {
        const Bin* b = &g_array_index(hg->bins, Bin, i);
        top_count = MAX(top_count, b->count);
        top_size = MAX(top_size, b->size);
    }
    gdouble slot = (gdouble)width / (gdouble)n;
    gdouble bar_w = MIN(BAR_MAX_W, MAX(1.0, (slot - 6.0) / 2.0));
    gdouble base = LEGEND_H + 4.0 + plot_h;
    for (guint i = 0; i < n; i++) {
        const Bin* b = &g_array_index(hg->bins, Bin, i);
        gdouble sx = slot * (gdouble)i;
        gdouble cx = sx + slot / 2.0;
        if ((gint)i == hg->hover) {
            set_color(cr, PAL_BG_HOVER, 1.0);
            cairo_rectangle(cr, sx, LEGEND_H, slot, (gdouble)height - LEGEND_H);
            cairo_fill(cr);
        }
        gdouble hc = bar_height(b->count, top_count, plot_h);
        gdouble hs = bar_height(b->size, top_size, plot_h);
        set_color(cr, COUNT_ROLE, 0.85);
        cairo_rectangle(cr, cx - bar_w - 1.0, base - hc, bar_w, hc);
        cairo_fill(cr);
        set_color(cr, SIZE_ROLE, 0.85);
        cairo_rectangle(cr, cx + 1.0, base - hs, bar_w, hs);
        cairo_fill(cr);
        draw_text(cr, layout, b->label, cx, base + 2.0, slot - 2.0, TRUE, PAL_FG_MUTED);
    }
    /* The baseline */
    set_color(cr, PAL_BORDER, 1.0);
    cairo_rectangle(cr, 0.0, base, (gdouble)width, 1.0);
    cairo_fill(cr);
    g_object_unref(layout);
}

/* The group at x, -1 if none */
static gint bin_at(Histogram* hg, gdouble x) {
    gint width = gtk_widget_get_width(hg->area);
    if (hg->bins->len == 0 || width <= 0 || x < 0.0 || x >= (gdouble)width) return -1;
    return (gint)(x * (gdouble)hg->bins->len / (gdouble)width);
}

static void set_hover(Histogram* hg, gint bin) {
    if (hg->hover == bin) return;
    hg->hover = bin;
    gtk_widget_queue_draw(hg->area);
}

static void on_motion(GtkEventControllerMotion* motion, gdouble x, gdouble y, gpointer data) {
    (void)motion;
    Histogram* hg = data;
    set_hover(hg, y >= LEGEND_H ? bin_at(hg, x) : -1);
}

static void on_leave(GtkEventControllerMotion* motion, gpointer data) {
    (void)motion;
    set_hover(data, -1);
}

static gboolean on_query_tooltip(GtkWidget* widget, gint x, gint y, gboolean keyboard, GtkTooltip* tooltip,
                                 gpointer data) {
    (void)widget;
    (void)keyboard;
    Histogram* hg = data;
    gint bin = (gdouble)y >= LEGEND_H ? bin_at(hg, x) : -1;
    if (bin < 0) return FALSE;
    const Bin* b = &g_array_index(hg->bins, Bin, bin);
    if (b->tip == NULL) return FALSE;
    gtk_tooltip_set_text(tooltip, b->tip);
    return TRUE;
}

static void histogram_free(gpointer data) {
    Histogram* hg = data;
    g_array_unref(hg->bins);
    g_free(hg);
}

GtkWidget* histogram_new(void) {
    Histogram* hg = g_new0(Histogram, 1);
    hg->bins = g_array_new(FALSE, TRUE, sizeof(Bin));
    g_array_set_clear_func(hg->bins, clear_bin);
    hg->hover = -1;
    hg->area = gtk_drawing_area_new();
    gtk_widget_add_css_class(hg->area, "histogram");
    gtk_widget_set_hexpand(hg->area, TRUE);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(hg->area), 120);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(hg->area), on_draw, hg, NULL);
    gtk_widget_set_has_tooltip(hg->area, TRUE);
    g_signal_connect(hg->area, "query-tooltip", G_CALLBACK(on_query_tooltip), hg);
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), hg);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), hg);
    gtk_widget_add_controller(hg->area, motion);
    g_object_set_data_full(G_OBJECT(hg->area), HISTOGRAM_KEY, hg, histogram_free);
    return hg->area;
}

void histogram_set(GtkWidget* chart, const HistogramBin* bins, guint n) {
    Histogram* hg = chart != NULL ? g_object_get_data(G_OBJECT(chart), HISTOGRAM_KEY) : NULL;
    if (hg == NULL) return;
    g_array_set_size(hg->bins, 0);
    for (guint i = 0; i < n; i++) {
        Bin b = { g_strdup(bins[i].label), g_strdup(bins[i].tip), bins[i].count, bins[i].size };
        g_array_append_val(hg->bins, b);
    }
    hg->hover = -1;
    gtk_widget_queue_draw(hg->area);
}
