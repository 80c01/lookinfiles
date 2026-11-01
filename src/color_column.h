#ifndef COLOR_COLUMN_H
#define COLOR_COLUMN_H

/* Vertical line at a column of a text view, like Vim's colorcolumn: drawn
 * over the view (a non-targetable drawing area in an overlay), at the left
 * edge of that column in the view's font, scrolling with the text. Works
 * with GtkSourceView or a plain GtkTextView. Colour: @editor_ruler. */
#include <gtk/gtk.h>

/* The overlay to put in place of scroll (the GtkScrolledWindow holding
 * view): scroll is its child, the line is drawn over it */
GtkWidget* color_column_wrap(GtkWidget* scroll, GtkWidget* view);

/* Column of the line (1-based: 81 draws it after 80 characters), 0 hides
 * it. Ignored for a view not wrapped by color_column_wrap(). */
void color_column_set(GtkWidget* view, gint column);
gint color_column_get(GtkWidget* view);

#endif /* COLOR_COLUMN_H */
