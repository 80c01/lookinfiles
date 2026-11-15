#ifndef ODP_GEOMETRY_H
#define ODP_GEOMETRY_H

#include <glib.h>

#include "office/slide_render.h"

/* Outlines of OpenDocument drawing shapes as slide paths (SlidePathStep):
 * SVG paths (svg:d, arcs included), point lists (draw:points), and custom
 * shape geometries (draw:enhanced-path with its draw:equation formulas and
 * draw:modifiers: lines, curves, the arc and quadrant commands). Arcs
 * become Bézier curves. Pure GLib, unit-tested. */

/* "x y w h" of svg:viewBox; FALSE if it is not four numbers */
gboolean odp_geometry_viewbox(const gchar* value, gdouble box[4]);

/* Steps of an SVG path in its own units, (min_x, min_y) moved to the
 * origin; NULL if there are none */
GArray* odp_geometry_svg_path(const gchar* d, gdouble min_x, gdouble min_y);

/* Steps of a point list ("x,y x,y ..."), closed for a polygon; NULL if
 * there are fewer than two points */
GArray* odp_geometry_points(const gchar* points, gdouble min_x, gdouble min_y, gboolean close);

/* A custom shape geometry (draw:enhanced-geometry) */
typedef struct {
    const gchar* path;          /* draw:enhanced-path */
    const gchar* modifiers;     /* draw:modifiers, may be NULL */
    const gchar* const* eq_names;     /* draw:equation names, n_equations */
    const gchar* const* eq_formulas;
    guint n_equations;
    gdouble box[4];             /* svg:viewBox */
    gdouble width, height;      /* size of the shape, points */
} OdpEnhanced;

/* Steps of the geometry in viewBox units (its origin at 0, 0); NULL if the
 * path has no drawable command */
GArray* odp_geometry_enhanced(const OdpEnhanced* g);

/* Value of a formula with the geometry's equations and modifiers (tests) */
gdouble odp_geometry_formula(const OdpEnhanced* g, const gchar* formula);

#endif /* ODP_GEOMETRY_H */
