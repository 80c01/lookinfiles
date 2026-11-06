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

/* Room kept around the points of a line, arrow or pencil stroke inside its
 * block (stroke width, arrowhead) */
#define CANVAS_LINE_PAD 16

/* Box dragged from (x0, y0) to (x1, y1), whatever the direction; square:
 * equal sides, the shorter one grown (Shift) */
void canvas_drag_box(gdouble x0, gdouble y0, gdouble x1, gdouble y1, gboolean square, CanvasRect* out);

/* End point (*x1, *y1) of a line from (x0, y0) turned to the nearest
 * multiple of 45 degrees, its length kept (Shift) */
void canvas_snap_angle(gdouble x0, gdouble y0, gdouble* x1, gdouble* y1);

/* Coordinate v of the range [from, from + from_len] placed in
 * [to, to + to_len]; an empty range goes to the middle */
gdouble canvas_map_coord(gdouble v, gdouble from, gdouble from_len, gdouble to, gdouble to_len);

#endif /* CANVAS_GEOMETRY_H */
