/* Image helpers (see image_utils.h) */
#include "image_utils.h"

GdkTexture* texture_new_for_pixbuf(GdkPixbuf* pixbuf) {
    g_return_val_if_fail(GDK_IS_PIXBUF(pixbuf), NULL);
    /* GdkPixbuf stores 8-bit RGB or RGBA (not premultiplied) rows */
    GdkMemoryFormat format = gdk_pixbuf_get_has_alpha(pixbuf) ? GDK_MEMORY_R8G8B8A8
                                                              : GDK_MEMORY_R8G8B8;
    GBytes* bytes = gdk_pixbuf_read_pixel_bytes(pixbuf);
    GdkTexture* texture = gdk_memory_texture_new(gdk_pixbuf_get_width(pixbuf),
                                                 gdk_pixbuf_get_height(pixbuf), format, bytes,
                                                 (gsize)gdk_pixbuf_get_rowstride(pixbuf));
    g_bytes_unref(bytes);
    return texture;
}
