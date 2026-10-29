#ifndef MINIMAP_GEOMETRY_H
#define MINIMAP_GEOMETRY_H

#include <glib.h>

/* Geometry of the minimap, as in VS Code: the document is drawn at a fixed
 * vertical scale; when it is taller than the minimap, the minimap scrolls in
 * proportion to the view, so the slider reaches the bottom of the minimap
 * exactly when the view reaches the end of the document.
 *
 * Inputs are the view's vertical adjustment (lower is 0): `upper` content
 * height, `page` visible height, `value` scroll position, and the minimap
 * height, all in pixels. */

/* Largest scale (short documents are not blown up) */
#define MINIMAP_MAX_SCALE 0.25
/* Smallest scale: below it the minimap scrolls instead of shrinking */
#define MINIMAP_MIN_SCALE 0.125

typedef struct {
    gdouble scale;         /* minimap pixels per document pixel */
    gdouble content;       /* document height in minimap pixels */
    gdouble offset;        /* minimap scroll: content y shown at the top */
    gdouble slider_top;    /* slider position in the minimap */
    gdouble slider_height; /* slider height (the visible page) */
} MinimapGeometry;

/* FALSE (geometry zeroed) when there is nothing to show */
gboolean minimap_geometry_compute(gdouble upper, gdouble page, gdouble value,
                                  gdouble height, MinimapGeometry* g);

/* Scroll value that puts the slider top at `slider_top` (slider drag) */
gdouble minimap_geometry_value_for_slider(const MinimapGeometry* g, gdouble upper,
                                          gdouble page, gdouble height, gdouble slider_top);

/* Scroll value that centers the view on the document line drawn at minimap
 * y `y` (click outside the slider) */
gdouble minimap_geometry_value_centered(const MinimapGeometry* g, gdouble upper,
                                        gdouble page, gdouble y);

#endif /* MINIMAP_GEOMETRY_H */
