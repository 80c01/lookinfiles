#ifndef CANVAS_LINE_H
#define CANVAS_LINE_H

/* Lines and arrows edited on the Canvas page, as in Excalidraw: picked on
 * their stroke only (not anywhere in their box), and, once selected, a
 * handle on each point to drag it and one in the middle of each segment to
 * add a point there (the line bends into a curve through its points). */
#include "canvas_item.h"

/* (x, y) (page coordinates) is on the stroke of the line or arrow it */
gboolean canvas_line_hits(const CanvasItem* it, gdouble x, gdouble y);

/* The widget under (x, y) on the page surface, as gtk_widget_pick(), but a
 * line or an arrow only where its stroke is: elsewhere in its box, what is
 * under it */
GtkWidget* canvas_line_pick(GtkWidget* surface, gdouble x, gdouble y);

/* Handle of the line or arrow it at (x, y): the index of a point, or with
 * *middle set, of the segment (from that point to the next) whose middle it
 * is; -1 when none */
gint canvas_line_handle_at(const CanvasItem* it, gdouble x, gdouble y, gboolean* middle);

/* A new point in the middle of segment i; returns its index (-1: none) */
gint canvas_line_insert_point(CanvasItem* it, guint segment);

/* Remove point i, not an end; FALSE if there is none to remove */
gboolean canvas_line_remove_point(CanvasItem* it, guint i);

/* Pointer cursor over the line or arrow it at (x, y): on a handle, on the
 * stroke, or none away from it */
void canvas_line_update_cursor(CanvasItem* it, gdouble x, gdouble y);

#endif /* CANVAS_LINE_H */
