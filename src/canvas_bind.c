/* Lines and arrows bound to blocks on the Canvas page (see canvas_bind.h) */
#include "canvas_bind.h"
#include "canvas_draw.h"
#include "canvas_geometry.h"

#include <math.h>

#define BIND_GAP 6     /* room between a bound end and the outline */
#define BIND_MARGIN 8  /* an end this close to a block binds to it */
#define MOVE_EPSILON 0.5

/* A block an end can be bound to: anything but lines, arrows and strokes */
static gboolean bindable(const CanvasItem* it) {
    return it != NULL && !(it->kind == CANVAS_BLOCK_SHAPE &&
                           (it->shape_type == CANVAS_SHAPE_LINE || it->shape_type == CANVAS_SHAPE_ARROW ||
                            it->shape_type == CANVAS_SHAPE_FREEHAND));
}

static void block_box(const CanvasItem* it, CanvasRect* r) {
    r->x = it->x;
    r->y = it->y;
    if (it->kind == CANVAS_BLOCK_SHAPE) {
        r->w = it->w;
        r->h = it->h;
    } else {
        r->w = MAX(it->w, gtk_widget_get_width(it->frame));
        r->h = MAX(it->h, gtk_widget_get_height(it->frame));
    }
}

static CanvasOutline block_outline(const CanvasItem* it) {
    if (it->kind != CANVAS_BLOCK_SHAPE) return CANVAS_OUTLINE_RECT;
    if (it->shape_type == CANVAS_SHAPE_ELLIPSE) return CANVAS_OUTLINE_ELLIPSE;
    if (it->shape_type == CANVAS_SHAPE_DIAMOND) return CANVAS_OUTLINE_DIAMOND;
    return CANVAS_OUTLINE_RECT;
}

static CanvasItem* block_by_id(GtkWidget* surface, gint id) {
    if (id <= 0) return NULL;
    for (GtkWidget* c = gtk_widget_get_first_child(surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it != NULL && it->id == id) return bindable(it) ? it : NULL;
    }
    return NULL;
}

/* The top block (other than line) with (x, y) on it or close to it */
static CanvasItem* block_at(GtkWidget* surface, const CanvasItem* line, gdouble x, gdouble y) {
    for (GtkWidget* c = gtk_widget_get_last_child(surface); c != NULL; c = gtk_widget_get_prev_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL || it == line || !bindable(it)) continue;
        CanvasRect r;
        block_box(it, &r);
        if (x >= r.x - BIND_MARGIN && x <= r.x + r.w + BIND_MARGIN &&
            y >= r.y - BIND_MARGIN && y <= r.y + r.h + BIND_MARGIN) {
            return it;
        }
    }
    return NULL;
}

static void centre(const CanvasItem* it, gdouble* x, gdouble* y) {
    CanvasRect r;
    block_box(it, &r);
    *x = r.x + r.w / 2.0;
    *y = r.y + r.h / 2.0;
}

/* The end on the outline of block, aimed at (tx, ty) */
static void on_outline(const CanvasItem* block, gdouble tx, gdouble ty, gdouble* x, gdouble* y) {
    CanvasRect r;
    block_box(block, &r);
    canvas_edge_point(block_outline(block), &r, tx, ty, BIND_GAP, x, y);
}

/* Each bound end goes on its block, aimed at the other block's centre (or
 * at the free end) */
static void follow_line(GtkWidget* surface, CanvasItem* line) {
    if (line->start_id <= 0 && line->end_id <= 0) return;
    gdouble x0, y0, x1, y1;
    if (!canvas_draw_get_ends(line, &x0, &y0, &x1, &y1)) return;
    CanvasItem* s = block_by_id(surface, line->start_id);
    CanvasItem* e = block_by_id(surface, line->end_id);
    if (s == NULL && e == NULL) return;
    if (s != NULL && s == e) return; /* both ends on one block: left as drawn */
    gdouble nx0 = x0, ny0 = y0, nx1 = x1, ny1 = y1;
    gdouble tx, ty;
    if (s != NULL) {
        if (e != NULL) {
            centre(e, &tx, &ty);
        } else {
            tx = x1;
            ty = y1;
        }
        on_outline(s, tx, ty, &nx0, &ny0);
    }
    if (e != NULL) {
        if (s != NULL) {
            centre(s, &tx, &ty);
        } else {
            tx = x0;
            ty = y0;
        }
        on_outline(e, tx, ty, &nx1, &ny1);
    }
    if (fabs(nx0 - x0) < MOVE_EPSILON && fabs(ny0 - y0) < MOVE_EPSILON &&
        fabs(nx1 - x1) < MOVE_EPSILON && fabs(ny1 - y1) < MOVE_EPSILON) {
        return;
    }
    canvas_draw_set_ends(line, nx0, ny0, nx1, ny1);
}

void canvas_bind_attach(GtkWidget* surface, CanvasItem* line) {
    gdouble x0, y0, x1, y1;
    if (surface == NULL || !canvas_draw_get_ends(line, &x0, &y0, &x1, &y1)) return;
    CanvasItem* s = block_at(surface, line, x0, y0);
    CanvasItem* e = block_at(surface, line, x1, y1);
    line->start_id = s != NULL ? s->id : 0;
    line->end_id = e != NULL && e != s ? e->id : 0;
    follow_line(surface, line);
}

void canvas_bind_follow(GtkWidget* surface) {
    if (surface == NULL) return;
    for (GtkWidget* c = gtk_widget_get_first_child(surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (canvas_draw_is_line(it)) follow_line(surface, it);
    }
}

static gboolean moved_has(GPtrArray* moved, gint id) {
    for (guint i = 0; i < moved->len; i++) {
        if (((CanvasItem*)g_ptr_array_index(moved, i))->id == id) return TRUE;
    }
    return FALSE;
}

void canvas_bind_release(GPtrArray* moved) {
    if (moved == NULL) return;
    for (guint i = 0; i < moved->len; i++) {
        CanvasItem* it = g_ptr_array_index(moved, i);
        if (!canvas_draw_is_line(it)) continue;
        if (it->start_id > 0 && !moved_has(moved, it->start_id)) it->start_id = 0;
        if (it->end_id > 0 && !moved_has(moved, it->end_id)) it->end_id = 0;
    }
}

void canvas_bind_forget(GtkWidget* surface, gint id) {
    if (surface == NULL || id <= 0) return;
    for (GtkWidget* c = gtk_widget_get_first_child(surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL) continue;
        if (it->start_id == id) it->start_id = 0;
        if (it->end_id == id) it->end_id = 0;
    }
}
