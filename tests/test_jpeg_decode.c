/* Unit tests for src/media/jpeg_decode.c: progressive JPEGs decoded at the
 * size shown. */
#include "media/jpeg_decode.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_LIBJPEG
#include <jpeglib.h>
#endif

/* Four quadrants: red, green / blue, white */
static const guint8 QUADRANTS[4][3] = {
    { 230, 20, 20 }, { 20, 200, 20 },
    { 20, 20, 230 }, { 245, 245, 245 },
};

static gchar* tmp_dir;

/* The baseline JPEG at path rewritten as a progressive one (same DCT
 * coefficients, lossless) */
static void make_progressive(const gchar* path) {
#ifdef HAVE_LIBJPEG
    gchar* tmp = g_strconcat(path, ".prog", NULL);
    FILE* in = g_fopen(path, "rb");
    FILE* out = g_fopen(tmp, "wb");
    g_assert_nonnull(in);
    g_assert_nonnull(out);
    struct jpeg_decompress_struct src;
    struct jpeg_compress_struct dst;
    struct jpeg_error_mgr src_err, dst_err;
    src.err = jpeg_std_error(&src_err);
    jpeg_create_decompress(&src);
    jpeg_stdio_src(&src, in);
    jpeg_read_header(&src, TRUE);
    jvirt_barray_ptr* coefs = jpeg_read_coefficients(&src);
    dst.err = jpeg_std_error(&dst_err);
    jpeg_create_compress(&dst);
    jpeg_copy_critical_parameters(&src, &dst);
    jpeg_simple_progression(&dst);
    jpeg_stdio_dest(&dst, out);
    jpeg_write_coefficients(&dst, coefs);
    jpeg_finish_compress(&dst);
    jpeg_destroy_compress(&dst);
    jpeg_finish_decompress(&src);
    jpeg_destroy_decompress(&src);
    fclose(in);
    fclose(out);
    g_assert_cmpint(g_rename(tmp, path), ==, 0);
    g_free(tmp);
#else
    (void)path;
#endif
}

/* A JPEG of the four quadrants, progressive unless baseline */
static gchar* write_jpeg_as(const gchar* name, gint w, gint h, gboolean baseline) {
    GdkPixbuf* pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
    guchar* px = gdk_pixbuf_get_pixels(pix);
    gint stride = gdk_pixbuf_get_rowstride(pix);
    for (gint y = 0; y < h; y++) {
        for (gint x = 0; x < w; x++) {
            const guint8* c = QUADRANTS[(y >= h / 2) * 2 + (x >= w / 2)];
            guchar* p = px + (gsize)y * stride + (gsize)x * 3;
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
        }
    }
    gchar* path = g_build_filename(tmp_dir, name, NULL);
    g_assert_true(gdk_pixbuf_save(pix, path, "jpeg", NULL, "quality", "90", NULL));
    g_object_unref(pix);
    if (!baseline) {
        make_progressive(path);
    }
    return path;
}

static gchar* write_jpeg(const gchar* name, gint w, gint h) {
    return write_jpeg_as(name, w, h, FALSE);
}

/* The colour at the middle of quadrant q is close to the one written */
static void assert_quadrant(GdkPixbuf* pix, gint q) {
    gint w = gdk_pixbuf_get_width(pix), h = gdk_pixbuf_get_height(pix);
    gint x = (q % 2) ? w * 3 / 4 : w / 4;
    gint y = (q / 2) ? h * 3 / 4 : h / 4;
    const guchar* p = gdk_pixbuf_get_pixels(pix) + (gsize)y * gdk_pixbuf_get_rowstride(pix) + (gsize)x * 3;
    for (gint i = 0; i < 3; i++) {
        g_assert_cmpint(abs((gint)p[i] - (gint)QUADRANTS[q][i]), <, 24);
    }
}

static gboolean have_decoder(void) {
#ifdef HAVE_LIBJPEG
    return TRUE;
#else
    g_test_skip("built without libjpeg");
    return FALSE;
#endif
}

static void test_exact_size(void) {
    if (!have_decoder()) {
        return;
    }
    gchar* path = write_jpeg("quad.jpg", 800, 600);
    /* Scaled while decoding (1/8 .. 8/8), then to the exact size; 1:1 too */
    static const gint sizes[][2] = { { 100, 75 }, { 333, 250 }, { 799, 599 }, { 800, 600 }, { 37, 600 } };
    for (guint i = 0; i < G_N_ELEMENTS(sizes); i++) {
        GdkPixbuf* pix = jpeg_decode_file(path, sizes[i][0], sizes[i][1], NULL);
        g_assert_nonnull(pix);
        g_assert_cmpint(gdk_pixbuf_get_width(pix), ==, sizes[i][0]);
        g_assert_cmpint(gdk_pixbuf_get_height(pix), ==, sizes[i][1]);
        g_assert_false(gdk_pixbuf_get_has_alpha(pix));
        for (gint q = 0; q < 4; q++) {
            assert_quadrant(pix, q);
        }
        g_object_unref(pix);
    }
    g_remove(path);
    g_free(path);
}

static void test_not_jpeg(void) {
    if (!have_decoder()) {
        return;
    }
    gchar* missing = g_build_filename(tmp_dir, "missing.jpg", NULL);
    g_assert_null(jpeg_decode_file(missing, 10, 10, NULL));
    g_free(missing);

    gchar* text = g_build_filename(tmp_dir, "text.jpg", NULL);
    g_assert_true(g_file_set_contents(text, "not a JPEG at all\n", -1, NULL));
    g_assert_null(jpeg_decode_file(text, 10, 10, NULL));
    g_remove(text);
    g_free(text);

    gchar* path = write_jpeg("size.jpg", 64, 48);
    g_assert_null(jpeg_decode_file(path, 0, 10, NULL));
    g_remove(path);
    g_free(path);
}

/* Baseline JPEGs are left to gdk-pixbuf (as fast) */
static void test_baseline(void) {
    if (!have_decoder()) {
        return;
    }
    gchar* path = write_jpeg_as("baseline.jpg", 320, 240, TRUE);
    g_assert_null(jpeg_decode_file(path, 160, 120, NULL));
    g_remove(path);
    g_free(path);
}

static void test_truncated(void) {
    if (!have_decoder()) {
        return;
    }
    gchar* path = write_jpeg("cut.jpg", 640, 480);
    gchar* data = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents(path, &data, &len, NULL));
    g_assert_true(g_file_set_contents(path, data, (gssize)(len / 2), NULL));
    g_free(data);
    /* Decoded as far as it goes: the top half is there */
    GdkPixbuf* pix = jpeg_decode_file(path, 320, 240, NULL);
    g_assert_nonnull(pix);
    g_assert_cmpint(gdk_pixbuf_get_width(pix), ==, 320);
    assert_quadrant(pix, 0);
    assert_quadrant(pix, 1);
    g_object_unref(pix);
    g_remove(path);
    g_free(path);
}

static void test_cancelled(void) {
    if (!have_decoder()) {
        return;
    }
    gchar* path = write_jpeg("cancel.jpg", 320, 240);
    GCancellable* cancellable = g_cancellable_new();
    g_cancellable_cancel(cancellable);
    g_assert_null(jpeg_decode_file(path, 320, 240, cancellable));
    g_object_unref(cancellable);
    g_remove(path);
    g_free(path);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    tmp_dir = g_dir_make_tmp("test_jpeg_decode-XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);
    g_test_add_func("/jpeg_decode/exact_size", test_exact_size);
    g_test_add_func("/jpeg_decode/not_jpeg", test_not_jpeg);
    g_test_add_func("/jpeg_decode/baseline", test_baseline);
    g_test_add_func("/jpeg_decode/truncated", test_truncated);
    g_test_add_func("/jpeg_decode/cancelled", test_cancelled);
    int rc = g_test_run();
    g_rmdir(tmp_dir);
    g_free(tmp_dir);
    return rc;
}
