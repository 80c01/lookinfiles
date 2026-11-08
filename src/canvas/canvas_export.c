/* Export the Canvas page as a picture (see canvas_export.h) */
#include "canvas/canvas_export.h"
#include "canvas/canvas_item.h"
#include "core/i18n.h"
#include "palette.h"

#include <cairo.h>
#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif

#define EXPORT_MARGIN 24
#define PNG_SCALE 2.0

/* Box around the blocks on the surface; FALSE when there is none */
static gboolean blocks_box(GtkWidget* surface, gint* x, gint* y, gint* w, gint* h) {
    gint x0 = G_MAXINT, y0 = G_MAXINT, x1 = G_MININT, y1 = G_MININT;
    for (GtkWidget* c = gtk_widget_get_first_child(surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL) continue;
        CanvasRect b;
        canvas_item_bounds(it, &b);
        x0 = MIN(x0, b.x);
        y0 = MIN(y0, b.y);
        x1 = MAX(x1, b.x + b.w);
        y1 = MAX(y1, b.y + b.h);
    }
    if (x0 == G_MAXINT) return FALSE;
    *x = MAX(x0 - EXPORT_MARGIN, 0);
    *y = MAX(y0 - EXPORT_MARGIN, 0);
    *w = x1 + EXPORT_MARGIN - *x;
    *h = y1 + EXPORT_MARGIN - *y;
    return TRUE;
}

gboolean canvas_export_picture(GtkWidget* surface, const gchar* path, gboolean svg, GError** error) {
    gint x, y, w, h;
    if (!blocks_box(surface, &x, &y, &w, &h)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("The canvas is empty."));
        return FALSE;
    }
#ifndef CAIRO_HAS_SVG_SURFACE
    if (svg) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("This build of cairo cannot write SVG."));
        return FALSE;
    }
#endif
    /* The page as drawn on screen, at its own scale (not the view's zoom) */
    GdkPaintable* paintable = gtk_widget_paintable_new(surface);
    GtkSnapshot* snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snapshot, gtk_widget_get_width(surface), gtk_widget_get_height(surface));
    GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
    g_object_unref(paintable);
    if (node == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("The canvas is not shown."));
        return FALSE;
    }

    gdouble scale = svg ? 1.0 : PNG_SCALE;
    cairo_surface_t* target;
#ifdef CAIRO_HAS_SVG_SURFACE
    if (svg) {
        target = cairo_svg_surface_create(path, w, h);
    } else
#endif
    {
        target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (gint)(w * scale), (gint)(h * scale));
    }
    cairo_t* cr = cairo_create(target);
    cairo_scale(cr, scale, scale);
    gdouble r, g, b;
    palette_rgb(PAL_BG_VIEW, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    cairo_translate(cr, -x, -y);
    gsk_render_node_draw(node, cr);
    cairo_destroy(cr);
    gsk_render_node_unref(node);

    cairo_status_t status = cairo_surface_status(target);
    if (status == CAIRO_STATUS_SUCCESS && !svg) status = cairo_surface_write_to_png(target, path);
    cairo_surface_finish(target);
    if (status == CAIRO_STATUS_SUCCESS) status = cairo_surface_status(target);
    cairo_surface_destroy(target);
    if (status != CAIRO_STATUS_SUCCESS) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", cairo_status_to_string(status));
        return FALSE;
    }
    return TRUE;
}
