#ifndef CANVAS_EXPORT_H
#define CANVAS_EXPORT_H

/* Export the Canvas page as a picture: the blocks on the page surface, as
 * drawn (theme colours, on the page background), cropped to them with a
 * margin. PNG at twice the page scale, or SVG. */
#include <gtk/gtk.h>

/* Write the picture of surface (a page shown in the window) to path.
 * FALSE with error set when the page is empty or cannot be drawn. */
gboolean canvas_export_picture(GtkWidget* surface, const gchar* path, gboolean svg, GError** error);

#endif /* CANVAS_EXPORT_H */
