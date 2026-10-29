#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

/* Image helpers shared by the previews and rich renderers. No AppData. */
#include <gtk/gtk.h>

/* Texture with the pixels of a pixbuf (replaces gdk_texture_new_for_pixbuf(),
 * deprecated in GTK 4.20). Safe to call from a worker thread. */
GdkTexture* texture_new_for_pixbuf(GdkPixbuf* pixbuf);

#endif /* IMAGE_UTILS_H */
