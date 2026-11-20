#ifndef FILLER_LINES_H
#define FILLER_LINES_H

/* Vim's filler lines: a "~" at the start of each screen line below the end
 * of the buffer, drawn over a text view (a non-targetable drawing area in
 * an overlay), in the view's font, in the view's first column (over the
 * gutter), scrolling with the text. Colour:
 * @accent_fg (Vim's NonText). */
#include <gtk/gtk.h>

/* The overlay to put in place of child (the widget showing view): child is
 * its child, the lines are drawn over it. Hidden until filler_lines_set(). */
GtkWidget* filler_lines_wrap(GtkWidget* child, GtkWidget* view);

/* Show or hide the lines. Ignored for a view not wrapped by
 * filler_lines_wrap(). */
void filler_lines_set(GtkWidget* view, gboolean shown);

#endif /* FILLER_LINES_H */
