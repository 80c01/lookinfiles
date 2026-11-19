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
    GArray* bins;   /* Bin */
    gint hover;     /* group under the pointer, -1: none */
    gint selected;  /* group chosen, -1: none */
    gint cursor;    /* group with the keyboard focus */
    void (*activate)(gpointer data, guint bin);
    gpointer data;
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

/* Every how many groups a label is drawn, so that they do not overlap */
static guint label_step(Histogram* hg, PangoLayout* layout, gdouble slot) {
    gint widest = 0;
    pango_layout_set_width(layout, -1);
    for (guint i = 0; i < hg->bins->len; i++) {
        int w, h;
        pango_layout_set_text(layout, g_array_index(hg->bins, Bin, i).label, -1);
        pango_layout_get_pixel_size(layout, &w, &h);
        widest = MAX(widest, w);
    }
    if (slot <= 0.0) return 1;
    guint step = (guint)(((gdouble)widest + 6.0) / slot) + 1;
    return MAX(step, 1);
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
    guint step = label_step(hg, layout, slot);
    gboolean focused = hg->activate != NULL && gtk_widget_has_focus(hg->area);
    for (guint i = 0; i < n; i++) {
        const Bin* b = &g_array_index(hg->bins, Bin, i);
        gdouble sx = slot * (gdouble)i;
        gdouble cx = sx + slot / 2.0;
        gboolean chosen = (gint)i == hg->selected;
        if (chosen || (gint)i == hg->hover) {
            set_color(cr, chosen ? PAL_BG_SELECTED : PAL_BG_HOVER, 1.0);
            cairo_rectangle(cr, sx, LEGEND_H, slot, (gdouble)height - LEGEND_H);
            cairo_fill(cr);
        }
        gdouble alpha = hg->selected < 0 || chosen ? 0.85 : 0.35;
        gdouble hc = bar_height(b->count, top_count, plot_h);
        gdouble hs = bar_height(b->size, top_size, plot_h);
        set_color(cr, COUNT_ROLE, alpha);
        cairo_rectangle(cr, cx - bar_w - 1.0, base - hc, bar_w, hc);
        cairo_fill(cr);
        set_color(cr, SIZE_ROLE, alpha);
        cairo_rectangle(cr, cx + 1.0, base - hs, bar_w, hs);
        cairo_fill(cr);
        if (i % step == 0 || chosen) {
            draw_text(cr, layout, b->label, cx, base + 2.0, MAX(slot * (gdouble)step, slot) - 2.0, TRUE,
                      chosen ? PAL_FG_STRONG : PAL_FG_MUTED);
        }
        if (focused && (gint)i == hg->cursor) {
            set_color(cr, PAL_FOCUS_RING, 1.0);
            cairo_set_line_width(cr, 1.5);
            cairo_rectangle(cr, sx + 1.0, LEGEND_H + 1.0, MAX(slot - 2.0, 1.0), (gdouble)height - LEGEND_H - 2.0);
            cairo_stroke(cr);
        }
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
    Histogram* hg = data;
    gint bin = keyboard ? hg->cursor : (gdouble)y >= LEGEND_H ? bin_at(hg, x) : -1;
    if (bin < 0 || (guint)bin >= hg->bins->len) return FALSE;
    const Bin* b = &g_array_index(hg->bins, Bin, bin);
    if (b->tip == NULL) return FALSE;
    gtk_tooltip_set_text(tooltip, b->tip);
    return TRUE;
}

static void activate_bin(Histogram* hg, gint bin) {
    if (hg->activate == NULL || bin < 0 || (guint)bin >= hg->bins->len) return;
    hg->cursor = bin;
    hg->activate(hg->data, (guint)bin);
}

static void on_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    Histogram* hg = data;
    gint bin = y >= LEGEND_H ? bin_at(hg, x) : -1;
    if (hg->activate == NULL || bin < 0) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_widget_grab_focus(hg->area);
    activate_bin(hg, bin);
}

static gboolean on_key(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state,
                       gpointer data) {
    (void)controller;
    (void)keycode;
    Histogram* hg = data;
    gint n = (gint)hg->bins->len;
    if (hg->activate == NULL || n == 0 || (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0) return FALSE;
    gint cursor = CLAMP(hg->cursor, 0, n - 1);
    switch (keyval) {
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left: cursor = MAX(cursor - 1, 0); break;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right: cursor = MIN(cursor + 1, n - 1); break;
    case GDK_KEY_Home: cursor = 0; break;
    case GDK_KEY_End: cursor = n - 1; break;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_space:
        activate_bin(hg, cursor);
        return TRUE;
    default: return FALSE;
    }
    hg->cursor = cursor;
    gtk_widget_trigger_tooltip_query(hg->area);
    gtk_widget_queue_draw(hg->area);
    return TRUE;
}

static void on_focus_changed(GtkEventControllerFocus* focus, gpointer data) {
    (void)focus;
    gtk_widget_queue_draw(((Histogram*)data)->area);
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
    hg->selected = -1;
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
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), hg);
    gtk_widget_add_controller(hg->area, GTK_EVENT_CONTROLLER(click));
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), hg);
    gtk_widget_add_controller(hg->area, keys);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_focus_changed), hg);
    g_signal_connect(focus, "leave", G_CALLBACK(on_focus_changed), hg);
    gtk_widget_add_controller(hg->area, focus);
    g_object_set_data_full(G_OBJECT(hg->area), HISTOGRAM_KEY, hg, histogram_free);
    return hg->area;
}

static Histogram* of_chart(GtkWidget* chart) {
    return chart != NULL ? g_object_get_data(G_OBJECT(chart), HISTOGRAM_KEY) : NULL;
}

void histogram_set(GtkWidget* chart, const HistogramBin* bins, guint n) {
    Histogram* hg = of_chart(chart);
    if (hg == NULL) return;
    g_array_set_size(hg->bins, 0);
    for (guint i = 0; i < n; i++) {
        Bin b = { g_strdup(bins[i].label), g_strdup(bins[i].tip), bins[i].count, bins[i].size };
        g_array_append_val(hg->bins, b);
    }
    hg->hover = -1;
    if (hg->selected >= (gint)n) hg->selected = -1;
    if (hg->cursor >= (gint)n) hg->cursor = n > 0 ? (gint)n - 1 : 0;
    gtk_widget_queue_draw(hg->area);
}

void histogram_set_activate(GtkWidget* chart, void (*activate)(gpointer data, guint bin), gpointer data) {
    Histogram* hg = of_chart(chart);
    if (hg == NULL) return;
    hg->activate = activate;
    hg->data = data;
    gtk_widget_set_focusable(hg->area, activate != NULL);
    gtk_widget_set_cursor_from_name(hg->area, activate != NULL ? "pointer" : NULL);
}

void histogram_set_selected(GtkWidget* chart, gint bin) {
    Histogram* hg = of_chart(chart);
    if (hg == NULL) return;
    hg->selected = bin >= 0 && (guint)bin < hg->bins->len ? bin : -1;
    if (hg->selected >= 0) hg->cursor = hg->selected;
    gtk_widget_queue_draw(hg->area);
}

/* The pair */

#define PAIR_SPACING 16

static gboolean pair_side_by_side(int width) {
    return width < 0 || width >= 2 * HISTOGRAM_PAIR_MIN_WIDTH + PAIR_SPACING;
}

static GtkSizeRequestMode pair_request_mode(GtkWidget* widget) {
    (void)widget;
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void pair_measure(GtkWidget* widget, GtkOrientation orientation, int for_size, int* minimum, int* natural,
                         int* minimum_baseline, int* natural_baseline) {
    GtkWidget* kids[2] = { gtk_widget_get_first_child(widget), gtk_widget_get_last_child(widget) };
    int mins[2] = { 0, 0 }, nats[2] = { 0, 0 };
    gboolean side = orientation == GTK_ORIENTATION_VERTICAL && pair_side_by_side(for_size);
    for (int i = 0; i < 2; i++) {
        if (kids[i] == NULL || !gtk_widget_should_layout(kids[i])) continue;
        int each = orientation == GTK_ORIENTATION_VERTICAL && for_size >= 0 && side
                       ? MAX((for_size - PAIR_SPACING) / 2, 0)
                       : for_size;
        gtk_widget_measure(kids[i], orientation, each, &mins[i], &nats[i], NULL, NULL);
    }
    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        /* Narrow: one over the other; natural: side by side (the width
         * taken from the panel, never asked for) */
        *minimum = MAX(mins[0], mins[1]);
        *natural = nats[0] + nats[1] + PAIR_SPACING;
    } else if (side) {
        *minimum = MAX(mins[0], mins[1]);
        *natural = MAX(nats[0], nats[1]);
    } else {
        *minimum = mins[0] + mins[1] + PAIR_SPACING;
        *natural = nats[0] + nats[1] + PAIR_SPACING;
    }
    *minimum_baseline = -1;
    *natural_baseline = -1;
}

static void pair_allocate(GtkWidget* widget, int width, int height, int baseline) {
    (void)baseline;
    GtkWidget* kids[2] = { gtk_widget_get_first_child(widget), gtk_widget_get_last_child(widget) };
    gboolean side = pair_side_by_side(width);
    int w = side ? MAX((width - PAIR_SPACING) / 2, 0) : width;
    int h = side ? height : MAX((height - PAIR_SPACING) / 2, 0);
    for (int i = 0; i < 2; i++) {
        if (kids[i] == NULL || !gtk_widget_should_layout(kids[i])) continue;
        int x = side ? i * (w + PAIR_SPACING) : 0;
        int y = side ? 0 : i * (h + PAIR_SPACING);
        if (i == 1) {
            /* The second takes what the halves leave over */
            if (side) w = width - x;
            else h = height - y;
        }
        gtk_widget_size_allocate(kids[i], &(GtkAllocation){ x, y, MAX(w, 0), MAX(h, 0) }, -1);
    }
}

GtkWidget* histogram_pair_new(GtkWidget* first, GtkWidget* second) {
    /* A box for its children, laid out by the pair (no box setters used) */
    GtkWidget* pair = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_layout_manager(pair, gtk_custom_layout_new(pair_request_mode, pair_measure, pair_allocate));
    gtk_widget_add_css_class(pair, "histogram-pair");
    gtk_widget_set_hexpand(pair, TRUE);
    gtk_box_append(GTK_BOX(pair), first);
    gtk_box_append(GTK_BOX(pair), second);
    return pair;
}
