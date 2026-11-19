/* The wheel of the Disk Usage tab (see disk_usage_wheel.h) */
#include "diskuse/disk_usage_wheel.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "core/sunburst_layout.h"
#include "theme/palette.h"

#include <math.h>

#define WHEEL_KEY "lookin-disk-usage-wheel"
#define RINGS 4           /* levels drawn around the folder shown */
#define MAX_ENTRIES 200   /* arcs per folder; the smallest others are summed up */
#define MIN_ANGLE 0.004   /* slimmest arc drawn (radians) */
#define MIN_LABEL_ARC 46.0 /* shortest arc (pixels, at mid ring) that gets a name */
#define MARGIN 6.0

typedef enum {
    ARC_DIR,
    ARC_FILE,
    ARC_OTHER, /* files not listed, or the smallest entries, summed up */
} ArcKind;

typedef struct {
    gdouble a0, a1;       /* angles, from the top, clockwise */
    guint ring;           /* 0: an entry of the folder shown */
    ArcKind kind;
    DiskNode* dir;        /* ARC_DIR */
    DiskNode* parent;     /* the folder holding it */
    const gchar* name;    /* entry name (borrowed from the tree), NULL for ARC_OTHER */
    const gchar* top;     /* name of the entry of the folder shown holding it, or NULL */
    guint colour;         /* index of the entry of the folder shown holding it */
    guint64 size;
    guint64 count;        /* ARC_OTHER: entries summed up */
} Arc;

typedef struct {
    GtkWidget* area;
    DiskUsageWheelHandlers handlers;
    DiskNode* node;   /* folder drawn (borrowed), NULL: none */
    gchar* selected;  /* entry outlined */
    GArray* arcs;     /* Arc, parents before their content */
    gboolean laid;    /* arcs laid out for node */
} Wheel;

/* The colours of the entries of the folder shown, in turn */
static const PaletteRole COLOURS[] = { PAL_ICON_BLUE,   PAL_ICON_GREEN, PAL_ICON_ORANGE, PAL_ICON_PURPLE,
                                       PAL_ICON_PINK,   PAL_ICON_YELLOW, PAL_ICON_RED };

static void lay_out(Wheel* w, DiskNode* node, gdouble a0, gdouble a1, guint ring, const gchar* top, guint colour) {
    if (ring >= RINGS || a1 - a0 < MIN_ANGLE) return;
    GArray* entries = disk_node_entries(node, MAX_ENTRIES);
    guint n = entries->len;
    gdouble* weights = g_new(gdouble, MAX(n, 1));
    gdouble* starts = g_new(gdouble, n + 1);
    for (guint i = 0; i < n; i++) weights[i] = (gdouble)g_array_index(entries, DiskEntry, i).size;
    /* A folder whose content is not all in its entries keeps the rest empty */
    gdouble whole = 0.0;
    for (guint i = 0; i < n; i++) whole += weights[i];
    gdouble end = node->size > 0 && whole < (gdouble)node->size ? a0 + (a1 - a0) * whole / (gdouble)node->size : a1;
    sunburst_split(weights, n, a0, end, starts);
    for (guint i = 0; i < n; i++) {
        if (starts[i + 1] - starts[i] < MIN_ANGLE) continue;
        const DiskEntry* e = &g_array_index(entries, DiskEntry, i);
        Arc arc = { 0 };
        arc.a0 = starts[i];
        arc.a1 = starts[i + 1];
        arc.ring = ring;
        arc.parent = node;
        arc.size = e->size;
        if (e->dir != NULL) {
            arc.kind = ARC_DIR;
            arc.dir = e->dir;
            arc.name = e->dir->name;
        } else if (e->file != NULL) {
            arc.kind = ARC_FILE;
            arc.name = e->file;
        } else {
            arc.kind = ARC_OTHER;
            arc.count = e->count;
        }
        arc.top = ring == 0 ? arc.name : top;
        arc.colour = ring == 0 ? i % G_N_ELEMENTS(COLOURS) : colour;
        g_array_append_val(w->arcs, arc);
        if (arc.kind == ARC_DIR) lay_out(w, e->dir, arc.a0, arc.a1, ring + 1, arc.top, arc.colour);
    }
    g_free(weights);
    g_free(starts);
    g_array_unref(entries);
}

static void ensure_layout(Wheel* w) {
    if (w->laid) return;
    w->laid = TRUE;
    g_array_set_size(w->arcs, 0);
    if (w->node != NULL && w->node->size > 0) lay_out(w, w->node, 0.0, 2.0 * G_PI, 0, NULL, 0);
}

static void set_color(cairo_t* cr, PaletteRole role, gdouble alpha) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, alpha);
}

/* The path of an arc (cairo angles start at 3 o'clock) */
static void arc_path(cairo_t* cr, const SunburstFrame* f, const Arc* a) {
    gdouble inner, outer;
    sunburst_ring_radii(f, a->ring, &inner, &outer);
    gdouble s = a->a0 - G_PI / 2.0, e = a->a1 - G_PI / 2.0;
    cairo_new_path(cr);
    cairo_arc(cr, f->cx, f->cy, outer - 0.5, s, e);
    cairo_arc_negative(cr, f->cx, f->cy, inner + 0.5, e, s);
    cairo_close_path(cr);
}

/* "12 other files", or "34 more" for the smallest entries summed up */
static gchar* other_text(const Arc* a) {
    gchar* num = g_strdup_printf("%" G_GUINT64_FORMAT, a->count);
    gulong n = (gulong)MIN(a->count, G_MAXULONG);
    gchar* text = a->parent != NULL && a->count == disk_node_other_files_count(a->parent)
                      /* TRANSLATORS: %s is a number of files */
                      ? g_strdup_printf(ngettext("%s other file", "%s other files", n), num)
                      /* TRANSLATORS: %s is a number of files and folders too small to be drawn */
                      : g_strdup_printf(ngettext("%s more", "%s more", n), num);
    g_free(num);
    return text;
}

/* text centred on (x, y), cut to width */
static void draw_centred(cairo_t* cr, PangoLayout* layout, const gchar* text, gdouble x, gdouble y, gdouble width,
                         PaletteRole role) {
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, (int)(MAX(width, 1.0) * PANGO_SCALE));
    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    set_color(cr, role, 1.0);
    cairo_move_to(cr, x - width / 2.0, y - (gdouble)th / 2.0);
    pango_cairo_show_layout(cr, layout);
}

static void draw_hole(cairo_t* cr, PangoLayout* layout, const SunburstFrame* f, const DiskNode* node) {
    set_color(cr, PAL_BG_BLOCK, 1.0);
    cairo_new_path(cr);
    cairo_arc(cr, f->cx, f->cy, f->hole - 1.0, 0, 2.0 * G_PI);
    cairo_fill(cr);
    gdouble width = f->hole * 1.6;
    draw_centred(cr, layout, node->name, f->cx, f->cy - 8.0, width, PAL_FG_STRONG);
    gchar* size = g_format_size(node->size);
    draw_centred(cr, layout, size, f->cx, f->cy + 8.0, width, PAL_FG_MUTED);
    g_free(size);
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    Wheel* w = data;
    set_color(cr, PAL_BG_VIEW, 1.0);
    cairo_paint(cr);
    PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    if (w->node == NULL || w->node->size == 0) {
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
        pango_layout_set_text(layout, w->node == NULL ? _("The wheel comes once the folder is read") : _("(empty)"),
                              -1);
        pango_layout_set_width(layout, (int)(MAX(width - 24, 1) * PANGO_SCALE));
        set_color(cr, PAL_FG_SUBTLE, 1.0);
        cairo_move_to(cr, 12.0, 10.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        return;
    }
    ensure_layout(w);
    SunburstFrame f = sunburst_frame(width, height, RINGS, MARGIN);
    for (guint i = 0; i < w->arcs->len; i++) {
        const Arc* a = &g_array_index(w->arcs, Arc, i);
        arc_path(cr, &f, a);
        switch (a->kind) {
        case ARC_DIR: set_color(cr, COLOURS[a->colour], 0.9 - 0.17 * (gdouble)a->ring); break;
        case ARC_FILE: set_color(cr, PAL_ICON_GREY, 0.55); break;
        case ARC_OTHER: set_color(cr, PAL_ICON_GREY_LIGHT, 0.45); break;
        }
        cairo_fill(cr);
    }
    /* The names of the entries of the folder shown, where they fit */
    for (guint i = 0; i < w->arcs->len; i++) {
        const Arc* a = &g_array_index(w->arcs, Arc, i);
        if (a->ring != 0 || a->name == NULL) continue;
        gdouble inner, outer;
        sunburst_ring_radii(&f, 0, &inner, &outer);
        gdouble mid_r = (inner + outer) / 2.0, mid_a = (a->a0 + a->a1) / 2.0;
        gdouble arc_len = mid_r * (a->a1 - a->a0);
        if (arc_len < MIN_LABEL_ARC) continue;
        gdouble x = f.cx + mid_r * sin(mid_a), y = f.cy - mid_r * cos(mid_a);
        draw_centred(cr, layout, a->name, x, y, MIN(arc_len, f.ring * 2.2), PAL_BADGE_FG);
    }
    /* The entry selected in the breakdown */
    for (guint i = 0; w->selected != NULL && i < w->arcs->len; i++) {
        const Arc* a = &g_array_index(w->arcs, Arc, i);
        if (a->ring != 0 || g_strcmp0(a->name, w->selected) != 0) continue;
        arc_path(cr, &f, a);
        set_color(cr, PAL_ACCENT, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
        break;
    }
    draw_hole(cr, layout, &f, w->node);
    g_object_unref(layout);
}

/* The arc under (x, y), NULL if none; *hole TRUE in the hole */
static const Arc* arc_at(Wheel* w, gdouble x, gdouble y, gboolean* hole) {
    *hole = FALSE;
    if (w->node == NULL || w->node->size == 0) return NULL;
    SunburstFrame f =
        sunburst_frame(gtk_widget_get_width(w->area), gtk_widget_get_height(w->area), RINGS, MARGIN);
    gint ring;
    gdouble angle;
    if (!sunburst_locate(&f, x, y, &ring, &angle)) return NULL;
    if (ring < 0) {
        *hole = TRUE;
        return NULL;
    }
    for (guint i = 0; i < w->arcs->len; i++) {
        const Arc* a = &g_array_index(w->arcs, Arc, i);
        if ((gint)a->ring == ring && angle >= a->a0 && angle < a->a1) return a;
    }
    return NULL;
}

/* Full path of an arc, NULL for ARC_OTHER */
static gchar* arc_path_name(const Arc* a) {
    if (a->kind == ARC_DIR) return disk_node_path(a->dir);
    if (a->kind != ARC_FILE) return NULL;
    gchar* dir = disk_node_path(a->parent);
    gchar* path = g_build_filename(dir, a->name, NULL);
    g_free(dir);
    return path;
}

static gboolean on_query_tooltip(GtkWidget* widget, gint x, gint y, gboolean keyboard, GtkTooltip* tooltip,
                                 gpointer data) {
    (void)widget;
    (void)keyboard;
    Wheel* w = data;
    gboolean hole;
    const Arc* a = arc_at(w, x, y, &hole);
    if (hole && w->node->parent != NULL) {
        gtk_tooltip_set_text(tooltip, _("Parent Folder"));
        return TRUE;
    }
    if (a == NULL) return FALSE;
    gchar* path = arc_path_name(a);
    gchar* what;
    if (path != NULL) {
        what = g_strdup(path);
    } else {
        gchar* parent = disk_node_path(a->parent);
        gchar* other = other_text(a);
        what = g_strdup_printf("%s (%s)", other, parent);
        g_free(other);
        g_free(parent);
    }
    gchar* size = g_format_size(a->size);
    gchar* pct = disk_format_percent(a->size, w->node->size);
    gchar* text = g_strdup_printf("%s\n%s · %s", what, size, pct);
    gtk_tooltip_set_text(tooltip, text);
    g_free(text);
    g_free(pct);
    g_free(size);
    g_free(what);
    g_free(path);
    return TRUE;
}

static void on_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    Wheel* w = data;
    gboolean hole;
    const Arc* a = arc_at(w, x, y, &hole);
    if (hole) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        if (n_press == 1 && w->node->parent != NULL && w->handlers.up != NULL) w->handlers.up(w->handlers.data);
        return;
    }
    if (a == NULL) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (n_press == 1) {
        if (a->top != NULL && w->handlers.selected != NULL) w->handlers.selected(w->handlers.data, a->top);
        return;
    }
    if (n_press != 2) return;
    if (a->kind == ARC_DIR && w->handlers.open != NULL) {
        w->handlers.open(w->handlers.data, a->dir);
    } else if (a->kind == ARC_FILE && w->handlers.reveal != NULL) {
        gchar* path = arc_path_name(a);
        w->handlers.reveal(w->handlers.data, path);
        g_free(path);
    }
}

static void wheel_free(gpointer data) {
    Wheel* w = data;
    g_array_unref(w->arcs);
    g_free(w->selected);
    g_free(w);
}

GtkWidget* disk_usage_wheel_new(const DiskUsageWheelHandlers* handlers) {
    Wheel* w = g_new0(Wheel, 1);
    if (handlers != NULL) w->handlers = *handlers;
    w->arcs = g_array_new(FALSE, FALSE, sizeof(Arc));
    w->area = gtk_drawing_area_new();
    gtk_widget_add_css_class(w->area, "du-map");
    gtk_widget_set_hexpand(w->area, TRUE);
    gtk_widget_set_vexpand(w->area, TRUE);
    gtk_widget_set_size_request(w->area, -1, 80);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(w->area), on_draw, w, NULL);
    gtk_widget_set_has_tooltip(w->area, TRUE);
    g_signal_connect(w->area, "query-tooltip", G_CALLBACK(on_query_tooltip), w);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), w);
    gtk_widget_add_controller(w->area, GTK_EVENT_CONTROLLER(click));
    g_object_set_data_full(G_OBJECT(w->area), WHEEL_KEY, w, wheel_free);
    return w->area;
}

static Wheel* wheel_state(GtkWidget* wheel) {
    return wheel != NULL ? g_object_get_data(G_OBJECT(wheel), WHEEL_KEY) : NULL;
}

void disk_usage_wheel_show(GtkWidget* wheel, DiskNode* node) {
    Wheel* w = wheel_state(wheel);
    if (w == NULL) return;
    w->node = node;
    w->laid = FALSE;
    g_array_set_size(w->arcs, 0);
    gtk_widget_queue_draw(w->area);
}

void disk_usage_wheel_select(GtkWidget* wheel, const gchar* name) {
    Wheel* w = wheel_state(wheel);
    if (w == NULL || g_strcmp0(w->selected, name) == 0) return;
    g_free(w->selected);
    w->selected = g_strdup(name);
    gtk_widget_queue_draw(w->area);
}
