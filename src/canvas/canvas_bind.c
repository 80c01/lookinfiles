/* Lines and arrows bound to blocks on the Canvas page (see canvas_bind.h) */
#include "canvas/canvas_bind.h"
#include "canvas/canvas_draw.h"
#include "canvas/canvas_geometry.h"

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

/* Angle of a turned shape, 0 for the other blocks */
static gdouble block_angle(const CanvasItem* it) {
    return canvas_item_rotatable(it) ? it->angle : 0;
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
        /* A turned shape: the point turned back with it */
        gdouble px = x, py = y;
        canvas_rotate_point(r.x + r.w / 2.0, r.y + r.h / 2.0, -block_angle(it), &px, &py);
        if (px >= r.x - BIND_MARGIN && px <= r.x + r.w + BIND_MARGIN &&
            py >= r.y - BIND_MARGIN && py <= r.y + r.h + BIND_MARGIN) {
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
    /* A turned shape: aimed in its own frame, then turned back */
    gdouble cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0, angle = block_angle(block);
    canvas_rotate_point(cx, cy, -angle, &tx, &ty);
    if (canvas_edge_point(block_outline(block), &r, tx, ty, BIND_GAP, x, y)) {
        canvas_rotate_point(cx, cy, angle, x, y);
    }
}

/* Each bound end goes on its block, aimed at the next point of the line:
 * with no point between the ends, the other block's centre (or the free
 * end) */
static void follow_line(GtkWidget* surface, CanvasItem* line) {
    if (line->start_id <= 0 && line->end_id <= 0) return;
    CanvasItem* s = block_by_id(surface, line->start_id);
    CanvasItem* e = block_by_id(surface, line->end_id);
    if (s == NULL && e == NULL) return;
    if (s != NULL && s == e) return; /* both ends on one block: left as drawn */
    GArray* pts = canvas_draw_get_points(line);
    if (pts == NULL) return;
    guint n = pts->len;
    CanvasPoint* first = &g_array_index(pts, CanvasPoint, 0);
    CanvasPoint* last = &g_array_index(pts, CanvasPoint, n - 1);
    CanvasPoint old_first = *first, old_last = *last;
    gdouble tx, ty;
    if (s != NULL) {
        if (n > 2) {
            tx = g_array_index(pts, CanvasPoint, 1).x;
            ty = g_array_index(pts, CanvasPoint, 1).y;
        } else if (e != NULL) {
            centre(e, &tx, &ty);
        } else {
            tx = old_last.x;
            ty = old_last.y;
        }
        on_outline(s, tx, ty, &first->x, &first->y);
    }
    if (e != NULL) {
        if (n > 2) {
            tx = g_array_index(pts, CanvasPoint, n - 2).x;
            ty = g_array_index(pts, CanvasPoint, n - 2).y;
        } else if (s != NULL) {
            centre(s, &tx, &ty);
        } else {
            tx = old_first.x;
            ty = old_first.y;
        }
        on_outline(e, tx, ty, &last->x, &last->y);
    }
    if (fabs(first->x - old_first.x) >= MOVE_EPSILON || fabs(first->y - old_first.y) >= MOVE_EPSILON ||
        fabs(last->x - old_last.x) >= MOVE_EPSILON || fabs(last->y - old_last.y) >= MOVE_EPSILON) {
        canvas_draw_set_points(line, pts);
    }
    g_array_unref(pts);
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

void canvas_bind_attach_end(GtkWidget* surface, CanvasItem* line, gboolean end) {
    gdouble x0, y0, x1, y1;
    if (surface == NULL || !canvas_draw_get_ends(line, &x0, &y0, &x1, &y1)) return;
    CanvasItem* b = end ? block_at(surface, line, x1, y1) : block_at(surface, line, x0, y0);
    gint other = end ? line->start_id : line->end_id;
    gint id = b != NULL && b->id != other ? b->id : 0;
    if (end) {
        line->end_id = id;
    } else {
        line->start_id = id;
    }
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
