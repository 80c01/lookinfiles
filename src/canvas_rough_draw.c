#include "canvas_rough_draw.h"
#include "canvas_geometry.h"
#include "palette.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Resolve color string (e.g. "accent", "danger", "muted", "fg") to RGB */
static void resolve_color(const gchar* name, double* r, double* g, double* b) {
    PaletteRole role = PAL_FG;
    if (name != NULL && name[0] != '\0') {
        const gchar* key = (name[0] == '@') ? name + 1 : name;
        if (!palette_lookup(key, &role)) {
            if (strcmp(key, "accent") == 0) role = PAL_ACCENT;
            else if (strcmp(key, "danger") == 0 || strcmp(key, "error") == 0) role = PAL_ERROR;
            else if (strcmp(key, "warning") == 0) role = PAL_WARNING;
            else if (strcmp(key, "success") == 0) role = PAL_SUCCESS;
            else if (strcmp(key, "muted") == 0) role = PAL_FG_SUBTLE;
            else role = PAL_FG;
        }
    }
    palette_rgb(role, r, g, b);
}

/* Pseudo-random jitter based on coordinates */
static double jitter(double seed, double amount) {
    double val = sin(seed * 12.9898 + 78.233) * 43758.5453;
    val = val - floor(val);
    return (val - 0.5) * 2.0 * amount;
}

static void draw_rough_line(cairo_t* cr, double x1, double y1, double x2, double y2, double amount) {
    double mx = (x1 + x2) / 2.0 + jitter(x1 + y1, amount);
    double my = (y1 + y2) / 2.0 + jitter(x2 + y2, amount);

    cairo_move_to(cr, x1, y1);
    cairo_curve_to(cr, (x1 + mx) / 2.0, (y1 + my) / 2.0, (mx + x2) / 2.0, (my + y2) / 2.0, x2, y2);

    /* Second subtle overlapping pass */
    double ox1 = x1 + jitter(y1, amount * 0.7);
    double oy1 = y1 + jitter(x1, amount * 0.7);
    double ox2 = x2 + jitter(y2, amount * 0.7);
    double oy2 = y2 + jitter(x2, amount * 0.7);
    cairo_move_to(cr, ox1, oy1);
    cairo_curve_to(cr, mx + jitter(my, amount * 0.5), my + jitter(mx, amount * 0.5),
                   (ox1 + ox2) / 2.0, (oy1 + oy2) / 2.0, ox2, oy2);
}

static void build_shape_path(cairo_t* cr, CanvasShapeType type, double x, double y, double w, double h) {
    switch (type) {
    case CANVAS_SHAPE_RECT:
    case CANVAS_SHAPE_ROUND_RECT:
        cairo_rectangle(cr, x, y, w, h);
        break;
    case CANVAS_SHAPE_ELLIPSE:
        cairo_save(cr);
        cairo_translate(cr, x + w / 2.0, y + h / 2.0);
        cairo_scale(cr, w / 2.0, h / 2.0);
        cairo_arc(cr, 0, 0, 1.0, 0, 2.0 * M_PI);
        cairo_restore(cr);
        break;
    case CANVAS_SHAPE_DIAMOND:
        cairo_move_to(cr, x + w / 2.0, y);
        cairo_line_to(cr, x + w, y + h / 2.0);
        cairo_line_to(cr, x + w / 2.0, y + h);
        cairo_line_to(cr, x, y + h / 2.0);
        cairo_close_path(cr);
        break;
    default:
        break;
    }
}

static void draw_sketch_fill(cairo_t* cr, double x, double y, double w, double h) {
    cairo_save(cr);
    cairo_clip(cr);
    double step = 8.0;
    double max_dim = (w + h) * 1.5;
    for (double d = -max_dim; d <= max_dim; d += step) {
        cairo_move_to(cr, x + d, y);
        cairo_line_to(cr, x + d + h, y + h);
    }
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* Points of a line, arrow or pencil stroke, placed in the block: their
 * bounding box fills it, CANVAS_LINE_PAD from its sides */
typedef struct {
    const GArray* pts;
    double bx, by, bw, bh;   /* bounding box of the points */
    double width, height;    /* block */
} PointMap;

static gboolean point_map_init(PointMap* m, const GArray* pts, gint width, gint height) {
    if (pts == NULL || pts->len < 2) return FALSE;
    const CanvasPoint* p0 = &g_array_index(pts, CanvasPoint, 0);
    double x0 = p0->x, y0 = p0->y, x1 = p0->x, y1 = p0->y;
    for (guint i = 1; i < pts->len; i++) {
        const CanvasPoint* p = &g_array_index(pts, CanvasPoint, i);
        x0 = MIN(x0, p->x);
        y0 = MIN(y0, p->y);
        x1 = MAX(x1, p->x);
        y1 = MAX(y1, p->y);
    }
    m->pts = pts;
    m->bx = x0;
    m->by = y0;
    m->bw = x1 - x0;
    m->bh = y1 - y0;
    m->width = width;
    m->height = height;
    return TRUE;
}

static void point_map_get(const PointMap* m, guint i, double* x, double* y) {
    const CanvasPoint* p = &g_array_index(m->pts, CanvasPoint, i);
    double pad = CANVAS_LINE_PAD;
    *x = canvas_map_coord(p->x, m->bx, m->bw, pad, MAX(m->width - 2 * pad, 0));
    *y = canvas_map_coord(p->y, m->by, m->bh, pad, MAX(m->height - 2 * pad, 0));
}

static void draw_arrow_head(cairo_t* cr, double fx, double fy, double tx, double ty,
                            double stroke_w, gboolean rough, double j_amt) {
    double angle = atan2(ty - fy, tx - fx);
    double head_len = CLAMP(stroke_w * 6.0, 10.0, 24.0);
    double a1 = angle + M_PI * 0.85;
    double a2 = angle - M_PI * 0.85;
    if (rough) {
        draw_rough_line(cr, tx, ty, tx + head_len * cos(a1), ty + head_len * sin(a1), j_amt);
        draw_rough_line(cr, tx, ty, tx + head_len * cos(a2), ty + head_len * sin(a2), j_amt);
    } else {
        cairo_move_to(cr, tx, ty);
        cairo_line_to(cr, tx + head_len * cos(a1), ty + head_len * sin(a1));
        cairo_move_to(cr, tx, ty);
        cairo_line_to(cr, tx + head_len * cos(a2), ty + head_len * sin(a2));
    }
}

/* Line or arrow between its first and last points */
static void draw_point_line(cairo_t* cr, const PointMap* m, gboolean arrow, double stroke_w,
                            gboolean rough, double j_amt) {
    double x0, y0, x1, y1;
    point_map_get(m, 0, &x0, &y0);
    point_map_get(m, m->pts->len - 1, &x1, &y1);
    if (rough) {
        draw_rough_line(cr, x0, y0, x1, y1, j_amt);
    } else {
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x1, y1);
    }
    if (arrow) draw_arrow_head(cr, x0, y0, x1, y1, stroke_w, rough, j_amt);
    cairo_stroke(cr);
}

/* Pencil stroke, smoothed: quadratic curves through the middles of its
 * segments */
static void draw_point_stroke(cairo_t* cr, const PointMap* m) {
    double px, py;
    point_map_get(m, 0, &px, &py);
    cairo_move_to(cr, px, py);
    double cx = px, cy = py;
    for (guint i = 1; i < m->pts->len; i++) {
        double x, y;
        point_map_get(m, i, &x, &y);
        double mx = (px + x) / 2.0;
        double my = (py + y) / 2.0;
        cairo_curve_to(cr, cx + 2.0 / 3.0 * (px - cx), cy + 2.0 / 3.0 * (py - cy),
                       mx + 2.0 / 3.0 * (px - mx), my + 2.0 / 3.0 * (py - my), mx, my);
        cx = mx;
        cy = my;
        px = x;
        py = y;
    }
    cairo_line_to(cr, px, py);
    cairo_stroke(cr);
}

void canvas_rough_draw_shape(cairo_t* cr, const CanvasBlock* b, gint width, gint height) {
    if (cr == NULL || b == NULL || b->kind != CANVAS_BLOCK_SHAPE) return;

    double pad = 6.0;
    double x = pad;
    double y = pad;
    double w = (double)width - 2.0 * pad;
    double h = (double)height - 2.0 * pad;
    if (w <= 2.0 || h <= 2.0) return;

    double r, g, bl;
    resolve_color(b->stroke_color, &r, &g, &bl);

    double stroke_w = (b->stroke_width > 0) ? (double)b->stroke_width : 2.0;
    double j_amt = b->rough ? 1.8 : 0.0;

    /* Lines, arrows and pencil strokes drawn on the page follow their points */
    PointMap map;
    gboolean by_points = (b->shape_type == CANVAS_SHAPE_LINE || b->shape_type == CANVAS_SHAPE_ARROW ||
                          b->shape_type == CANVAS_SHAPE_FREEHAND) &&
                         point_map_init(&map, b->points, width, height);
    if (by_points) {
        cairo_set_source_rgba(cr, r, g, bl, 0.95);
        cairo_set_line_width(cr, stroke_w);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        if (b->shape_type == CANVAS_SHAPE_FREEHAND) {
            draw_point_stroke(cr, &map);
        } else {
            draw_point_line(cr, &map, b->shape_type == CANVAS_SHAPE_ARROW, stroke_w, b->rough, j_amt);
        }
        return;
    }

    /* Fill */
    if (b->fill_style == CANVAS_FILL_SOLID) {
        build_shape_path(cr, b->shape_type, x, y, w, h);
        cairo_set_source_rgba(cr, r, g, bl, 0.20);
        cairo_fill(cr);
    } else if (b->fill_style == CANVAS_FILL_SKETCH) {
        build_shape_path(cr, b->shape_type, x, y, w, h);
        cairo_set_source_rgba(cr, r, g, bl, 0.45);
        draw_sketch_fill(cr, x, y, w, h);
    }

    /* Stroke */
    cairo_set_source_rgba(cr, r, g, bl, 0.95);
    cairo_set_line_width(cr, stroke_w);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    switch (b->shape_type) {
    case CANVAS_SHAPE_RECT:
    case CANVAS_SHAPE_ROUND_RECT:
        if (b->rough) {
            draw_rough_line(cr, x, y, x + w, y, j_amt);
            draw_rough_line(cr, x + w, y, x + w, y + h, j_amt);
            draw_rough_line(cr, x + w, y + h, x, y + h, j_amt);
            draw_rough_line(cr, x, y + h, x, y, j_amt);
            cairo_stroke(cr);
        } else {
            cairo_rectangle(cr, x, y, w, h);
            cairo_stroke(cr);
        }
        break;

    case CANVAS_SHAPE_ELLIPSE:
        if (b->rough) {
            cairo_save(cr);
            cairo_translate(cr, x + w / 2.0, y + h / 2.0);
            cairo_scale(cr, (w / 2.0) + jitter(x, j_amt), (h / 2.0) + jitter(y, j_amt));
            cairo_arc(cr, 0, 0, 1.0, 0, 2.0 * M_PI);
            cairo_restore(cr);
            cairo_save(cr);
            cairo_translate(cr, x + w / 2.0 + jitter(y, j_amt * 0.5), y + h / 2.0 + jitter(x, j_amt * 0.5));
            cairo_scale(cr, (w / 2.0) - jitter(y, j_amt * 0.6), (h / 2.0) - jitter(x, j_amt * 0.6));
            cairo_arc(cr, 0, 0, 1.0, 0.1, 2.0 * M_PI + 0.2);
            cairo_restore(cr);
            cairo_stroke(cr);
        } else {
            cairo_save(cr);
            cairo_translate(cr, x + w / 2.0, y + h / 2.0);
            cairo_scale(cr, w / 2.0, h / 2.0);
            cairo_arc(cr, 0, 0, 1.0, 0, 2.0 * M_PI);
            cairo_restore(cr);
            cairo_stroke(cr);
        }
        break;

    case CANVAS_SHAPE_DIAMOND:
        if (b->rough) {
            draw_rough_line(cr, x + w / 2.0, y, x + w, y + h / 2.0, j_amt);
            draw_rough_line(cr, x + w, y + h / 2.0, x + w / 2.0, y + h, j_amt);
            draw_rough_line(cr, x + w / 2.0, y + h, x, y + h / 2.0, j_amt);
            draw_rough_line(cr, x, y + h / 2.0, x + w / 2.0, y, j_amt);
            cairo_stroke(cr);
        } else {
            cairo_move_to(cr, x + w / 2.0, y);
            cairo_line_to(cr, x + w, y + h / 2.0);
            cairo_line_to(cr, x + w / 2.0, y + h);
            cairo_line_to(cr, x, y + h / 2.0);
            cairo_close_path(cr);
            cairo_stroke(cr);
        }
        break;

    case CANVAS_SHAPE_LINE:
        if (b->rough) {
            draw_rough_line(cr, x, y, x + w, y + h, j_amt);
            cairo_stroke(cr);
        } else {
            cairo_move_to(cr, x, y);
            cairo_line_to(cr, x + w, y + h);
            cairo_stroke(cr);
        }
        break;

    case CANVAS_SHAPE_ARROW: {
        double ax2 = x + w;
        double ay2 = y + h;
        if (b->rough) {
            draw_rough_line(cr, x, y, ax2, ay2, j_amt);
        } else {
            cairo_move_to(cr, x, y);
            cairo_line_to(cr, ax2, ay2);
        }
        draw_arrow_head(cr, x, y, ax2, ay2, stroke_w, b->rough, j_amt);
        cairo_stroke(cr);
        break;
    }

    case CANVAS_SHAPE_FREEHAND:
        break;
    }
}
