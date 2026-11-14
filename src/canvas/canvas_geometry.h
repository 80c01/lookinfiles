#ifndef CANVAS_GEOMETRY_H
#define CANVAS_GEOMETRY_H

#include <glib.h>

#define CANVAS_DEFAULT_GRID 16
#define CANVAS_SNAP_THRESHOLD 6

typedef struct {
    gint x, y, w, h;
} CanvasRect;

typedef struct {
    gboolean active_x;
    gint guide_x;
    gboolean active_y;
    gint guide_y;
} CanvasGuides;

/* Snap coordinate to nearest grid point */
gint canvas_snap_coord(gint val, gint grid);

/* Snap moving rectangle against grid and existing block rectangles.
 * Returns adjusted x and y, and sets active guide lines in guides. */
void canvas_snap_box(gint* x, gint* y, gint w, gint h,
                     const CanvasRect* others, guint count,
                     gboolean snap_grid, gint grid,
                     CanvasGuides* guides);

typedef enum {
    CANVAS_ALIGN_LEFT,
    CANVAS_ALIGN_CENTER_H,
    CANVAS_ALIGN_RIGHT,
    CANVAS_ALIGN_TOP,
    CANVAS_ALIGN_MIDDLE_V,
    CANVAS_ALIGN_BOTTOM,
    CANVAS_DISTRIBUTE_H,
    CANVAS_DISTRIBUTE_V
} CanvasAlignOp;

/* Apply alignment or distribution to an array of rects */
void canvas_align_boxes(CanvasRect* rects, guint count, CanvasAlignOp op);

/* The same, the rects with the same group (groups[i] > 0) aligned as one
 * box around them, keeping their places relative to each other; groups[i]
 * 0: a rect on its own. Fewer than 2 such boxes: nothing moves. */
void canvas_align_grouped(CanvasRect* rects, const gint* groups, guint count, CanvasAlignOp op);

/* Room kept around the points of a line, arrow or pencil stroke inside its
 * block (stroke width, arrowhead) */
#define CANVAS_LINE_PAD 16

/* Distance from a point or segment middle of a selected line that grabs it */
#define CANVAS_LINE_HANDLE 8
/* Segments shorter than this have no middle handle */
#define CANVAS_LINE_MID_MIN (4 * CANVAS_LINE_HANDLE)

/* Box dragged from (x0, y0) to (x1, y1), whatever the direction; square:
 * equal sides, the shorter one grown (Shift) */
void canvas_drag_box(gdouble x0, gdouble y0, gdouble x1, gdouble y1, gboolean square, CanvasRect* out);

/* End point (*x1, *y1) of a line from (x0, y0) turned to the nearest
 * multiple of 45 degrees, its length kept (Shift) */
void canvas_snap_angle(gdouble x0, gdouble y0, gdouble* x1, gdouble* y1);

/* Coordinate v of the range [from, from + from_len] placed in
 * [to, to + to_len]; an empty range goes to the middle */
gdouble canvas_map_coord(gdouble v, gdouble from, gdouble from_len, gdouble to, gdouble to_len);

/* Turned shapes: angles in degrees, clockwise on the page (y down) */

/* Angle a in [0, 360) */
gdouble canvas_angle_normalize(gdouble a);

/* (*x, *y) turned by angle around (cx, cy) */
void canvas_rotate_point(gdouble cx, gdouble cy, gdouble angle, gdouble* x, gdouble* y);

/* Box holding box turned by angle around its centre */
void canvas_rotated_bounds(const CanvasRect* box, gdouble angle, CanvasRect* out);

/* Angle of a block turned around (cx, cy) by its handle (above it at angle
 * 0) dragged to (x, y), in [0, 360); snap: the nearest multiple of 15
 * degrees (Shift) */
gdouble canvas_handle_angle(gdouble cx, gdouble cy, gdouble x, gdouble y, gboolean snap);

/* Box turned by angle around its centre, resized by its handle on side
 * (sx, sy) (-1: left or top, 0: middle, 1: right or bottom) dragged by
 * (dx, dy) on the page: the opposite handle stays put, neither side under
 * min; keep: the proportions of box (Shift) */
void canvas_resize_box(const CanvasRect* box, gdouble angle, gint sx, gint sy, gdouble dx, gdouble dy, gint min,
                       gboolean keep, CanvasRect* out);

/* Size (*w, *h) brought back to the proportions of w0 x h0 (Shift while
 * resizing a shape): the larger stretch wins, neither side under min */
void canvas_keep_ratio(gint w0, gint h0, gint min, gint* w, gint* h);

/* Outline of a block an arrow end is bound to */
typedef enum {
    CANVAS_OUTLINE_RECT,
    CANVAS_OUTLINE_ELLIPSE,
    CANVAS_OUTLINE_DIAMOND
} CanvasOutline;

/* Where a line from the centre of box towards (tx, ty) leaves its outline,
 * gap further out: the end of an arrow bound to the block. FALSE (point
 * unchanged) when (tx, ty) is the centre. */
gboolean canvas_edge_point(CanvasOutline outline, const CanvasRect* box, gdouble tx, gdouble ty,
                           gdouble gap, gdouble* x, gdouble* y);

/* Smooth curve through the n points of xy (x0, y0, x1, y1...): the points
 * of a line or an arrow, joined by Catmull-Rom segments (two points: a
 * straight segment). Segment i goes from point i to point i + 1. */

/* Bezier controls of segment i: c = {x1, y1, x2, y2} */
void canvas_curve_controls(const gdouble* xy, guint n, guint i, gdouble c[4]);

/* Point at t (0..1) of segment i */
void canvas_curve_point(const gdouble* xy, guint n, guint i, gdouble t, gdouble* x, gdouble* y);

/* Bounding box of the curve (a single point: that point) */
void canvas_curve_bounds(const gdouble* xy, guint n, gdouble* x0, gdouble* y0, gdouble* x1, gdouble* y1);

/* Distance from (px, py) to the curve (G_MAXDOUBLE without points) */
gdouble canvas_curve_distance(const gdouble* xy, guint n, gdouble px, gdouble py);

#endif /* CANVAS_GEOMETRY_H */
