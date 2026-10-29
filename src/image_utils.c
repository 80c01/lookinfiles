/* Image helpers (see image_utils.h) */
#include "image_utils.h"

#include <string.h>

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

GdkPixbuf* pixbuf_from_bytes(const guchar* data, gsize len, gint max_width) {
    if (data == NULL || len == 0) {
        return NULL;
    }
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    GdkPixbuf* pix = NULL;
    if (gdk_pixbuf_loader_write(loader, data, len, NULL) && gdk_pixbuf_loader_close(loader, NULL)) {
        GdkPixbuf* orig = gdk_pixbuf_loader_get_pixbuf(loader);
        if (orig != NULL) {
            gint w = gdk_pixbuf_get_width(orig);
            gint h = gdk_pixbuf_get_height(orig);
            if (max_width > 0 && w > max_width) {
                gint new_h = MAX(1, (gint)((gdouble)h * max_width / w));
                pix = gdk_pixbuf_scale_simple(orig, max_width, new_h, GDK_INTERP_BILINEAR);
            } else {
                pix = g_object_ref(orig);
            }
        }
    } else {
        gdk_pixbuf_loader_close(loader, NULL);
    }
    g_object_unref(loader);
    return pix;
}

GdkPixbuf* pixbuf_from_data_uri(const gchar* uri, gint max_width) {
    if (uri == NULL || !g_str_has_prefix(uri, "data:image/")) {
        return NULL;
    }
    const gchar* comma = strchr(uri, ',');
    if (comma == NULL || g_strstr_len(uri, comma - uri, ";base64") == NULL) {
        return NULL;
    }
    gsize len = 0;
    guchar* decoded = g_base64_decode(comma + 1, &len);
    GdkPixbuf* pix = pixbuf_from_bytes(decoded, len, max_width);
    g_free(decoded);
    return pix;
}
