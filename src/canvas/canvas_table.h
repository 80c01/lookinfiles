#ifndef CANVAS_TABLE_H
#define CANVAS_TABLE_H

#include "canvas_doc.h"
#include <gtk/gtk.h>

typedef void (*CanvasTableChangeNotify)(CanvasBlock* b, gpointer user_data);

/* Creates the table container widget displaying editable cells */
GtkWidget* canvas_table_widget_new(CanvasBlock* b, CanvasTableChangeNotify on_change, gpointer user_data);

/* Rebuilds the cells grid after row/col additions or removals */
void canvas_table_widget_rebuild(GtkWidget* table_widget);

/* Row and column changes of the table bar, at the current cell (the one
 * typed in last; the last row and column before any) */
typedef enum {
    CANVAS_TABLE_ROW_ABOVE,
    CANVAS_TABLE_ROW_BELOW,
    CANVAS_TABLE_ROW_DELETE,
    CANVAS_TABLE_COL_LEFT,
    CANVAS_TABLE_COL_RIGHT,
    CANVAS_TABLE_COL_DELETE,
} CanvasTableAction;

/* FALSE when the action would leave no row or no column */
gboolean canvas_table_widget_can(GtkWidget* table_widget, CanvasTableAction action);
/* Apply it; a cell being typed in keeps the focus, on its new place */
void canvas_table_widget_apply(GtkWidget* table_widget, CanvasTableAction action);

#endif /* CANVAS_TABLE_H */
