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

#endif /* CANVAS_GEOMETRY_H */
