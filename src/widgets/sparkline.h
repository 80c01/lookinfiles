#ifndef SPARKLINE_H
#define SPARKLINE_H

/* A small line chart of the last values of one or more series (a
 * GtkDrawingArea): the newest value at the right edge, each series a line
 * over a translucent fill in its palette colour, drawn when a value comes.
 * The scale is fixed (sparkline_set_max()) or follows the largest value
 * shown. Used by the system monitor (top bar and its tab). */
#include <gtk/gtk.h>
#include "theme/palette.h"

/* A chart keeping the last capacity values (at least 2) of each series */
GtkWidget* sparkline_new(guint capacity);
/* Add a series drawn in role's colour; returns its index */
guint sparkline_add_series(GtkWidget* chart, PaletteRole role);
/* Append a value to a series (the oldest one goes) and redraw */
void sparkline_push(GtkWidget* chart, guint series, gdouble value);
/* Fixed top of the scale (100 for a percentage); 0: the largest value shown */
void sparkline_set_max(GtkWidget* chart, gdouble max);
/* Top of the scale in use */
gdouble sparkline_get_max(GtkWidget* chart);
/* Horizontal lines at the quarters of the scale */
void sparkline_set_grid(GtkWidget* chart, gboolean grid);
/* Forget the values of every series */
void sparkline_clear(GtkWidget* chart);

#endif /* SPARKLINE_H */
