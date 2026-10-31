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

/* Image file scaled down to max_width pixels (0: full size), never scaled
 * up; NULL if it cannot be read */
GdkPixbuf* pixbuf_from_file(const gchar* path, gint max_width);

/* Paintable for a text view: the texture at its natural size, scaled down
 * to fit the width of view (NULL view: natural size), following resizes
 * like CSS "max-width: 100%". Main thread. */
/* Images shown with fit_image_new() are decoded at most this wide (memory
 * bound), then drawn at the width of the view */
#define FIT_IMAGE_MAX_WIDTH 2560

#define LOOKIN_TYPE_FIT_IMAGE (lookin_fit_image_get_type())
G_DECLARE_FINAL_TYPE(LookinFitImage, lookin_fit_image, LOOKIN, FIT_IMAGE, GObject)
GdkPaintable* fit_image_new(GdkTexture* texture, GtkTextView* view);

#endif /* IMAGE_UTILS_H */
