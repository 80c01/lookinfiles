#ifndef CANVAS_TABLE_BAR_H
#define CANVAS_TABLE_BAR_H

/* Table bar of the Canvas page: one row floating just above the table
 * being edited or selected (below it when there is no room), over the page
 * overlay. Insert a row above / below, delete the row, insert a column
 * left / right, delete the column, at the current cell. */
#include "canvas_table.h"
#include <gtk/gtk.h>

typedef void (*CanvasTableBarAction)(CanvasTableAction action, gpointer udata);

typedef struct CanvasTableBar CanvasTableBar;

CanvasTableBar* canvas_table_bar_new(CanvasTableBarAction on_action, gpointer user_data);
void canvas_table_bar_free(CanvasTableBar* bar);

/* The bar, to add as an overlay of the GtkOverlay over the page */
GtkWidget* canvas_table_bar_get_widget(CanvasTableBar* bar);

/* Show the bar next to target (a table block frame), its delete buttons
 * enabled when table (the table widget) has more than one row / column;
 * it follows the block. NULL hides it. */
void canvas_table_bar_show_for(CanvasTableBar* bar, GtkWidget* target, GtkWidget* table);

#endif /* CANVAS_TABLE_BAR_H */
