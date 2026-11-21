#ifndef GIT_GRAPH_CELL_H
#define GIT_GRAPH_CELL_H

/* The cell of the Graph column of the History view: the lanes and the dot
 * of one commit's row (git_graph), drawn in the palette colours. */
#include <gtk/gtk.h>
#include "git/git_graph.h"

/* Columns drawn at most: the lanes past them are cut */
#define GIT_GRAPH_CELL_MAX_LANES 12

/* A new, empty cell */
GtkWidget* git_graph_cell_new(void);

/* Draw row in cell (copied; NULL: nothing) */
void git_graph_cell_set_row(GtkWidget* cell, const GitGraphRow* row);

/* Width of the column for rows of lanes columns (with its padding) */
gint git_graph_cell_column_width(guint lanes);

#endif /* GIT_GRAPH_CELL_H */
