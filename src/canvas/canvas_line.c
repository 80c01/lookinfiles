/* Lines and arrows edited on the Canvas page (see canvas_line.h) */
#include "canvas/canvas_line.h"
#include "canvas/canvas_draw.h"
#include "canvas/canvas_geometry.h"

#include <math.h>

#define HIT_SLACK 6.0  /* room around the stroke that still picks it */
#define PICK_MAX 64    /* lines skipped at most by one pick */

gboolean canvas_line_hits(const CanvasItem* it, gdouble x, gdouble y) {
    GArray* pts = canvas_draw_get_points(it);
    if (pts == NULL) return FALSE;
    gdouble d = canvas_curve_distance((const gdouble*)pts->data, pts->len, x, y);
    g_array_unref(pts);
    return d <= MAX(it->stroke_width / 2.0 + HIT_SLACK, CANVAS_LINE_HANDLE);
}

GtkWidget* canvas_line_pick(GtkWidget* surface, gdouble x, gdouble y) {
    GPtrArray* skipped = g_ptr_array_new();
    GtkWidget* hit = NULL;
    for (gint n = 0; n < PICK_MAX; n++) {
        hit = gtk_widget_pick(surface, x, y, GTK_PICK_DEFAULT);
        CanvasItem* it = canvas_item_of(hit);
        if (!canvas_draw_is_line(it) || canvas_line_hits(it, x, y)) break;
        gtk_widget_set_can_target(it->frame, FALSE);
        g_ptr_array_add(skipped, it->frame);
        hit = NULL;
    }
    for (guint i = 0; i < skipped->len; i++) gtk_widget_set_can_target(g_ptr_array_index(skipped, i), TRUE);
    g_ptr_array_unref(skipped);
    return hit;
}

gint canvas_line_handle_at(const CanvasItem* it, gdouble x, gdouble y, gboolean* middle) {
    if (middle != NULL) *middle = FALSE;
    GArray* pts = canvas_draw_get_points(it);
    if (pts == NULL) return -1;
    const gdouble* xy = (const gdouble*)pts->data;
    guint n = pts->len;
    gint best = -1;
    gboolean best_mid = FALSE;
    gdouble best_d = CANVAS_LINE_HANDLE;
    /* The points first: an end wins over a middle close to it */
    for (guint i = 0; i < n; i++) {
        gdouble d = hypot(x - xy[2 * i], y - xy[2 * i + 1]);
        if (d <= best_d) {
            best = (gint)i;
            best_d = d;
        }
    }
    if (best < 0) {
        for (guint i = 0; i + 1 < n; i++) {
            if (hypot(xy[2 * i + 2] - xy[2 * i], xy[2 * i + 3] - xy[2 * i + 1]) < CANVAS_LINE_MID_MIN) continue;
            gdouble mx, my;
            canvas_curve_point(xy, n, i, 0.5, &mx, &my);
            gdouble d = hypot(x - mx, y - my);
            if (d <= best_d) {
                best = (gint)i;
                best_mid = TRUE;
                best_d = d;
            }
        }
    }
    g_array_unref(pts);
    if (middle != NULL) *middle = best_mid;
    return best;
}

gint canvas_line_insert_point(CanvasItem* it, guint segment) {
    GArray* pts = canvas_draw_get_points(it);
    if (pts == NULL) return -1;
    if (segment + 1 >= pts->len) {
        g_array_unref(pts);
        return -1;
    }
    CanvasPoint mid;
    canvas_curve_point((const gdouble*)pts->data, pts->len, segment, 0.5, &mid.x, &mid.y);
    g_array_insert_val(pts, segment + 1, mid);
    canvas_draw_set_points(it, pts);
    g_array_unref(pts);
    return (gint)segment + 1;
}

gboolean canvas_line_remove_point(CanvasItem* it, guint i) {
    GArray* pts = canvas_draw_get_points(it);
    if (pts == NULL) return FALSE;
    gboolean inner = i > 0 && i + 1 < pts->len;
    if (inner) {
        g_array_remove_index(pts, i);
        canvas_draw_set_points(it, pts);
    }
    g_array_unref(pts);
    return inner;
}

void canvas_line_update_cursor(CanvasItem* it, gdouble x, gdouble y) {
    if (!canvas_draw_is_line(it) || it->content == NULL) return;
    const gchar* name = NULL;
    if (!it->locked) {
        gboolean selected = gtk_widget_has_focus(it->frame) || gtk_widget_has_css_class(it->frame, CANVAS_SELECTED_CLASS);
        if (selected && canvas_line_handle_at(it, x, y, NULL) >= 0) {
            name = "pointer";
        } else if (canvas_line_hits(it, x, y)) {
            name = "move";
        }
    }
    gtk_widget_set_cursor_from_name(it->content, name);
}
