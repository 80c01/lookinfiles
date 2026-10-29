#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

/* Image helpers shared by the previews and rich renderers. No AppData. */
#include <gtk/gtk.h>

/* Texture with the pixels of a pixbuf (replaces gdk_texture_new_for_pixbuf(),
 * deprecated in GTK 4.20). Safe to call from a worker thread. */
GdkTexture* texture_new_for_pixbuf(GdkPixbuf* pixbuf);

/* Decode an encoded image (PNG, JPEG...) scaled down to max_width pixels
 * (0: full size); NULL if it cannot be decoded */
GdkPixbuf* pixbuf_from_bytes(const guchar* data, gsize len, gint max_width);

/* Image of a "data:image/...;base64," URI (pixbuf_from_bytes()); NULL if the
 * URI is not a base64 image */
GdkPixbuf* pixbuf_from_data_uri(const gchar* uri, gint max_width);

#endif /* IMAGE_UTILS_H */
