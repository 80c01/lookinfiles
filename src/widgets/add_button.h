#ifndef ADD_BUTTON_H
#define ADD_BUTTON_H

#include <gtk/gtk.h>

/* The "+" add button drawn with the palette: a rounded box (window
 * background, strong border; on hover a selected tint and an accent border)
 * around a plus sign. The workspace switcher draws it after its cells
 * (add_button_draw()); the tab bars use it as a widget (add_button_new()). */

#define ADD_BUTTON_WIDTH 22
#define ADD_BUTTON_HEIGHT 20

/* Draw the button in the box x, y, w, h */
void add_button_draw(cairo_t* cr, gdouble x, gdouble y, gdouble w, gdouble h, gboolean hover);

/* A frameless GtkButton showing it (ADD_BUTTON_WIDTH x ADD_BUTTON_HEIGHT), lit while
 * the pointer is over it; CSS class tab-add-btn */
GtkWidget* add_button_new(const gchar* tooltip);

#endif /* ADD_BUTTON_H */
