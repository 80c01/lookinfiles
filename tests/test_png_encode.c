/* Unit tests of the PNG encoder (src/formats/png_encode.c) */
#include "formats/png_encode.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

/* A picture in cairo's RGB24 layout, with a padded stride */
static guint32* picture(gint w, gint h, gint stride_words) {
    guint32* px = g_new0(guint32, (gsize)stride_words * h);
    for (gint y = 0; y < h; y++) {
        for (gint x = 0; x < w; x++) {
            px[y * stride_words + x] = ((guint32)(x * 37 & 0xFF) << 16) | ((guint32)(y * 53 & 0xFF) << 8) |
                                       (guint32)((x * y) & 0xFF);
        }
        px[y * stride_words + w] = 0xDEADBEEF; /* padding: never read */
    }
    return px;
}

static GdkPixbuf* decode(GBytes* png) {
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("png", NULL);
    g_assert_nonnull(loader);
    g_assert_true(gdk_pixbuf_loader_write_bytes(loader, png, NULL));
    g_assert_true(gdk_pixbuf_loader_close(loader, NULL));
    GdkPixbuf* pix = g_object_ref(gdk_pixbuf_loader_get_pixbuf(loader));
    g_object_unref(loader);
    return pix;
}

static void check_round_trip(gint w, gint h, gint level) {
    gint stride_words = w + 1;
    guint32* px = picture(w, h, stride_words);
    GBytes* png = png_encode_xrgb32((const guint8*)px, w, h, stride_words * 4, level);
    g_assert_nonnull(png);
    const guint8* d = g_bytes_get_data(png, NULL);
    g_assert_cmpint(memcmp(d, "\x89PNG\r\n\x1a\n", 8), ==, 0);
    GdkPixbuf* pix = decode(png);
    g_assert_cmpint(gdk_pixbuf_get_width(pix), ==, w);
    g_assert_cmpint(gdk_pixbuf_get_height(pix), ==, h);
    g_assert_false(gdk_pixbuf_get_has_alpha(pix));
    const guchar* p = gdk_pixbuf_read_pixels(pix);
    gint rs = gdk_pixbuf_get_rowstride(pix);
    for (gint y = 0; y < h; y++) {
        for (gint x = 0; x < w; x++) {
            guint32 v = px[y * stride_words + x];
            const guchar* q = p + (gsize)y * rs + x * 3;
            g_assert_cmpuint(q[0], ==, (v >> 16) & 0xFF);
            g_assert_cmpuint(q[1], ==, (v >> 8) & 0xFF);
            g_assert_cmpuint(q[2], ==, v & 0xFF);
        }
    }
    g_object_unref(pix);
    g_bytes_unref(png);
    g_free(px);
}

static void test_round_trip(void) {
    check_round_trip(1, 1, PNG_ENCODE_FAST);
    check_round_trip(7, 5, PNG_ENCODE_FAST);
    check_round_trip(7, 5, 0);
    check_round_trip(7, 5, 9);
    /* Larger than the compressor's 64 KB output buffer, several times */
    check_round_trip(960, 540, PNG_ENCODE_FAST);
}

static void test_invalid(void) {
    guint32 px[4] = {0};
    g_assert_null(png_encode_xrgb32(NULL, 1, 1, 4, 1));
    g_assert_null(png_encode_xrgb32((const guint8*)px, 0, 1, 4, 1));
    g_assert_null(png_encode_xrgb32((const guint8*)px, 2, 1, 4, 1)); /* stride too small */
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/png_encode/round_trip", test_round_trip);
    g_test_add_func("/png_encode/invalid", test_invalid);
    return g_test_run();
}
