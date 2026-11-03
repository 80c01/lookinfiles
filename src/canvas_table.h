#ifndef CANVAS_TABLE_H
#define CANVAS_TABLE_H

#include "canvas_doc.h"
#include <gtk/gtk.h>

typedef void (*CanvasTableChangeNotify)(CanvasBlock* b, gpointer user_data);

/* Creates the table container widget displaying editable cells */
GtkWidget* canvas_table_widget_new(CanvasBlock* b, CanvasTableChangeNotify on_change, gpointer user_data);

/* Rebuilds the cells grid after row/col additions or removals */
void canvas_table_widget_rebuild(GtkWidget* table_widget);

#endif /* CANVAS_TABLE_H */
