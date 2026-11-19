/* The treemap of the Disk Usage tab (see disk_usage_map.h) */
#include "diskuse/disk_usage_map.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "core/treemap_layout.h"
#include "theme/palette.h"

#include <string.h>

#define MAP_KEY "lookin-disk-usage-map"
#define MAX_DEPTH 3      /* folder levels drawn inside the folder shown */
#define MAX_ENTRIES 300  /* rectangles per folder; the smallest others are summed up */
#define HEADER 16.0      /* height of a folder's name strip */
#define MIN_LABEL_W 44.0 /* smallest rectangle that gets a name */
#define MIN_LABEL_H 15.0

typedef enum {
    TILE_DIR,
    TILE_FILE,
    TILE_OTHER, /* files not listed, or the smallest entries, summed up */
} TileKind;

/* A rectangle drawn, and what it stands for */
typedef struct {
    TreemapRect r;
    TileKind kind;
    guint depth;          /* 0: an entry of the folder shown */
    DiskNode* dir;        /* TILE_DIR */
    DiskNode* parent;     /* the folder holding it */
    const gchar* name;    /* entry name (borrowed from the tree), NULL for TILE_OTHER */
    const gchar* top;     /* name of the entry of the folder shown holding it, or NULL */
    guint64 size;
    guint64 count;        /* TILE_OTHER: entries summed up */
    DiskKind file_kind;   /* TILE_FILE */
    gboolean nested;      /* TILE_DIR: its content is drawn inside */
} Tile;

typedef struct {
    GtkWidget* area;
    DiskUsageMapHandlers handlers;
    DiskNode* node;   /* folder drawn (borrowed), NULL: none */
    gchar* selected;  /* entry outlined */
    GArray* tiles;    /* Tile, parents before their content */
    gint laid_w;      /* size the tiles were laid out for, -1: to do */
    gint laid_h;
} Map;

static DiskKind kind_of_name(const gchar* name) {
    const gchar* dot = strrchr(name, '.');
    gchar ext[16] = "";
    if (dot != NULL && dot != name && strlen(dot + 1) < sizeof ext) {
        for (gsize i = 0; dot[1 + i] != '\0'; i++) ext[i] = g_ascii_tolower(dot[1 + i]);
        ext[strlen(dot + 1)] = '\0';
    }
    return disk_kind_of_extension(ext);
}

static void lay_out(Map* m, DiskNode* node, TreemapRect area, guint depth, const gchar* top) {
    GArray* entries = disk_node_entries(node, MAX_ENTRIES);
    guint n = entries->len;
    if (n == 0) {
        g_array_unref(entries);
        return;
    }
    gdouble* weights = g_new(gdouble, n);
    TreemapRect* rects = g_new(TreemapRect, n);
    for (guint i = 0; i < n; i++) weights[i] = (gdouble)g_array_index(entries, DiskEntry, i).size;
    treemap_squarify(weights, n, area, rects);
    for (guint i = 0; i < n; i++) {
        const DiskEntry* e = &g_array_index(entries, DiskEntry, i);
        const TreemapRect* r = &rects[i];
        if (r->w < 1.0 || r->h < 1.0) continue; /* too small to be seen */
        Tile t = { 0 };
        t.r = *r;
        t.depth = depth;
        t.parent = node;
        t.size = e->size;
        if (e->dir != NULL) {
            t.kind = TILE_DIR;
            t.dir = e->dir;
            t.name = e->dir->name;
        } else if (e->file != NULL) {
            t.kind = TILE_FILE;
            t.name = e->file;
            t.file_kind = kind_of_name(e->file);
        } else {
            t.kind = TILE_OTHER;
            t.count = e->count;
        }
        t.top = depth == 0 ? t.name : top;
        TreemapRect inner = { r->x + 2.0, r->y + HEADER, r->w - 4.0, r->h - HEADER - 2.0 };
        t.nested = t.kind == TILE_DIR && depth + 1 < MAX_DEPTH && inner.w >= 12.0 && inner.h >= 12.0;
        g_array_append_val(m->tiles, t);
        if (t.nested) lay_out(m, e->dir, inner, depth + 1, t.top);
    }
    g_free(weights);
    g_free(rects);
    g_array_unref(entries);
}

static void ensure_layout(Map* m, gint width, gint height) {
    if (m->laid_w == width && m->laid_h == height) return;
    g_array_set_size(m->tiles, 0);
    m->laid_w = width;
    m->laid_h = height;
    if (m->node == NULL || m->node->size == 0) return;
    lay_out(m, m->node, (TreemapRect){ 0.0, 0.0, (gdouble)width, (gdouble)height }, 0, NULL);
}

static PaletteRole kind_role(DiskKind kind) {
    switch (kind) {
    case DISK_KIND_DOCUMENTS: return PAL_ICON_BLUE;
    case DISK_KIND_IMAGES: return PAL_ICON_PURPLE;
    case DISK_KIND_VIDEOS: return PAL_ICON_PINK;
    case DISK_KIND_AUDIO: return PAL_ICON_ORANGE;
    case DISK_KIND_ARCHIVES: return PAL_ICON_YELLOW;
    case DISK_KIND_CODE: return PAL_ICON_GREEN;
    case DISK_KIND_PROGRAMS: return PAL_ICON_RED;
    case DISK_KIND_OTHER:
    case DISK_KIND_COUNT: break;
    }
    return PAL_ICON_GREY;
}

static void set_color(cairo_t* cr, PaletteRole role, gdouble alpha) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, alpha);
}

/* "12 other files", or "34 more" for the smallest entries summed up */
static gchar* other_text(const Tile* t) {
    gchar* num = g_strdup_printf("%" G_GUINT64_FORMAT, t->count);
    gulong n = (gulong)MIN(t->count, G_MAXULONG);
    gchar* text = t->parent != NULL && t->count == disk_node_other_files_count(t->parent)
                      /* TRANSLATORS: %s is a number of files */
                      ? g_strdup_printf(ngettext("%s other file", "%s other files", n), num)
                      /* TRANSLATORS: %s is a number of files and folders too small to be drawn */
                      : g_strdup_printf(ngettext("%s more", "%s more", n), num);
    g_free(num);
    return text;
}

/* text in one line from (x, y), cut to width */
static void draw_text(cairo_t* cr, PangoLayout* layout, const gchar* text, gdouble x, gdouble y, gdouble width,
                      PaletteRole role) {
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    set_color(cr, role, 1.0);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}

/* The name of a rectangle (and its size when there is room) */
static void draw_label(cairo_t* cr, PangoLayout* layout, const Tile* t, PaletteRole role) {
    if (t->r.w < MIN_LABEL_W || t->r.h < MIN_LABEL_H) return;
    gchar* name = t->kind == TILE_OTHER ? other_text(t) : g_strdup(t->name);
    gchar* size = g_format_size(t->size);
    gchar* text = t->r.w >= 120.0 ? g_strdup_printf("%s · %s", name, size) : g_strdup(name);
    draw_text(cr, layout, text, t->r.x + 4.0, t->r.y + 1.0, t->r.w - 8.0, role);
    g_free(text);
    g_free(size);
    g_free(name);
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    Map* m = data;
    set_color(cr, PAL_BG_VIEW, 1.0);
    cairo_paint(cr);
    PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    if (m->node == NULL || m->node->size == 0) {
        draw_text(cr, layout, m->node == NULL ? _("The map comes once the folder is read") : _("(empty)"), 12.0,
                  10.0, (gdouble)width - 24.0, PAL_FG_SUBTLE);
        g_object_unref(layout);
        return;
    }
    ensure_layout(m, width, height);
    cairo_set_line_width(cr, 1.0);
    for (guint i = 0; i < m->tiles->len; i++) {
        const Tile* t = &g_array_index(m->tiles, Tile, i);
        /* A pixel between neighbours */
        gdouble x = t->r.x + 0.5, y = t->r.y + 0.5, w = t->r.w - 1.0, h = t->r.h - 1.0;
        if (w <= 0.0 || h <= 0.0) continue;
        cairo_rectangle(cr, x, y, w, h);
        switch (t->kind) {
        case TILE_DIR:
            if (t->nested) {
                set_color(cr, PAL_BG_BLOCK, 1.0);
            } else {
                set_color(cr, PAL_ICON_GREY_LIGHT, 0.35);
            }
            cairo_fill_preserve(cr);
            set_color(cr, PAL_BORDER, 1.0);
            cairo_stroke(cr);
            break;
        case TILE_FILE:
            set_color(cr, kind_role(t->file_kind), 0.8);
            cairo_fill(cr);
            break;
        case TILE_OTHER:
            set_color(cr, PAL_ICON_GREY, 0.45);
            cairo_fill(cr);
            break;
        }
        cairo_save(cr);
        cairo_rectangle(cr, t->r.x, t->r.y, t->r.w, t->r.h);
        cairo_clip(cr);
        draw_label(cr, layout, t, t->kind == TILE_FILE ? PAL_BADGE_FG : PAL_FG_TEXT);
        cairo_restore(cr);
    }
    /* The entry selected in the breakdown */
    for (guint i = 0; m->selected != NULL && i < m->tiles->len; i++) {
        const Tile* t = &g_array_index(m->tiles, Tile, i);
        if (t->depth != 0 || g_strcmp0(t->name, m->selected) != 0) continue;
        set_color(cr, PAL_ACCENT, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, t->r.x + 1.0, t->r.y + 1.0, MAX(t->r.w - 2.0, 0.0), MAX(t->r.h - 2.0, 0.0));
        cairo_stroke(cr);
        break;
    }
    g_object_unref(layout);
}

/* The deepest rectangle under (x, y), NULL if none */
static const Tile* tile_at(Map* m, gdouble x, gdouble y) {
    for (guint i = m->tiles->len; i > 0; i--) {
        const Tile* t = &g_array_index(m->tiles, Tile, i - 1);
        if (treemap_rect_contains(&t->r, x, y)) return t;
    }
    return NULL;
}

/* Full path of a rectangle, NULL for TILE_OTHER */
static gchar* tile_path(const Tile* t) {
    if (t->kind == TILE_DIR) return disk_node_path(t->dir);
    if (t->kind != TILE_FILE) return NULL;
    gchar* dir = disk_node_path(t->parent);
    gchar* path = g_build_filename(dir, t->name, NULL);
    g_free(dir);
    return path;
}

static gboolean on_query_tooltip(GtkWidget* widget, gint x, gint y, gboolean keyboard, GtkTooltip* tooltip,
                                 gpointer data) {
    (void)widget;
    (void)keyboard;
    Map* m = data;
    const Tile* t = tile_at(m, x, y);
    if (t == NULL || m->node == NULL) return FALSE;
    gchar* path = tile_path(t);
    gchar* what;
    if (path != NULL) {
        what = g_strdup(path);
    } else {
        gchar* parent = disk_node_path(t->parent);
        gchar* other = other_text(t);
        what = g_strdup_printf("%s (%s)", other, parent);
        g_free(other);
        g_free(parent);
    }
    gchar* size = g_format_size(t->size);
    gchar* pct = disk_format_percent(t->size, m->node->size);
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
    Map* m = data;
    const Tile* t = tile_at(m, x, y);
    if (t == NULL) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (n_press == 1) {
        if (t->top != NULL && m->handlers.selected != NULL) m->handlers.selected(m->handlers.data, t->top);
        return;
    }
    if (n_press != 2) return;
    if (t->kind == TILE_DIR && m->handlers.open != NULL) {
        m->handlers.open(m->handlers.data, t->dir);
    } else if (t->kind == TILE_FILE && m->handlers.reveal != NULL) {
        gchar* path = tile_path(t);
        m->handlers.reveal(m->handlers.data, path);
        g_free(path);
    }
}

static void on_resize(GtkDrawingArea* area, int width, int height, gpointer data) {
    (void)area;
    (void)width;
    (void)height;
    ((Map*)data)->laid_w = -1;
}

static void map_free(gpointer data) {
    Map* m = data;
    g_array_unref(m->tiles);
    g_free(m->selected);
    g_free(m);
}

GtkWidget* disk_usage_map_new(const DiskUsageMapHandlers* handlers) {
    Map* m = g_new0(Map, 1);
    if (handlers != NULL) m->handlers = *handlers;
    m->tiles = g_array_new(FALSE, FALSE, sizeof(Tile));
    m->laid_w = -1;
    m->area = gtk_drawing_area_new();
    gtk_widget_add_css_class(m->area, "du-map");
    gtk_widget_set_hexpand(m->area, TRUE);
    gtk_widget_set_vexpand(m->area, TRUE);
    gtk_widget_set_size_request(m->area, -1, 80);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(m->area), on_draw, m, NULL);
    g_signal_connect(m->area, "resize", G_CALLBACK(on_resize), m);
    gtk_widget_set_has_tooltip(m->area, TRUE);
    g_signal_connect(m->area, "query-tooltip", G_CALLBACK(on_query_tooltip), m);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), m);
    gtk_widget_add_controller(m->area, GTK_EVENT_CONTROLLER(click));
    g_object_set_data_full(G_OBJECT(m->area), MAP_KEY, m, map_free);
    return m->area;
}

static Map* map_state(GtkWidget* map) {
    return map != NULL ? g_object_get_data(G_OBJECT(map), MAP_KEY) : NULL;
}

void disk_usage_map_show(GtkWidget* map, DiskNode* node) {
    Map* m = map_state(map);
    if (m == NULL) return;
    m->node = node;
    m->laid_w = -1;
    g_array_set_size(m->tiles, 0);
    gtk_widget_queue_draw(m->area);
}

void disk_usage_map_select(GtkWidget* map, const gchar* name) {
    Map* m = map_state(map);
    if (m == NULL || g_strcmp0(m->selected, name) == 0) return;
    g_free(m->selected);
    m->selected = g_strdup(name);
    gtk_widget_queue_draw(m->area);
}
