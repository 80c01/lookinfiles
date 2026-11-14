/* JPEG decoding at the size shown (see jpeg_decode.h) */
#include "media/jpeg_decode.h"

#ifdef HAVE_LIBJPEG

#include <glib/gstdio.h>
#include <setjmp.h>
#include <stdio.h>

#include <jpeglib.h>

/* Rows decoded between two checks of the cancellable */
#define CANCEL_CHECK_ROWS 64

typedef struct {
    struct jpeg_error_mgr base;
    jmp_buf jump;
} JpegError;

static void on_jpeg_error(j_common_ptr cinfo) {
    longjmp(((JpegError*)cinfo->err)->jump, 1);
}

/* Warnings (corrupt data, premature end) are not reported: the image is
 * decoded as far as it goes */
static void on_jpeg_message(j_common_ptr cinfo) {
    (void)cinfo;
}

/* Smallest M/8 scale (M = 1..8) whose output still covers width x height */
static void choose_scale(struct jpeg_decompress_struct* cinfo, gint width, gint height) {
    cinfo->scale_denom = 8;
    for (guint num = 1; num <= 8; num++) {
        cinfo->scale_num = num;
        jpeg_calc_output_dimensions(cinfo);
        if ((gint)cinfo->output_width >= width && (gint)cinfo->output_height >= height) {
            return;
        }
    }
    cinfo->scale_num = 8;
}

GdkPixbuf* jpeg_decode_file(const gchar* path, gint width, gint height, GCancellable* cancellable) {
    if (path == NULL || width <= 0 || height <= 0) {
        return NULL;
    }
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    struct jpeg_decompress_struct cinfo;
    JpegError err;
    /* volatile: kept across the longjmp of an error */
    GdkPixbuf* volatile pix = NULL;
    cinfo.err = jpeg_std_error(&err.base);
    err.base.error_exit = on_jpeg_error;
    err.base.output_message = on_jpeg_message;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        if (pix != NULL) {
            g_object_unref(pix);
        }
        return NULL;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);

    /* Baseline: gdk-pixbuf is as fast. CMYK / YCCK cannot be turned into RGB
     * by libjpeg: gdk-pixbuf does it. */
    if (!cinfo.progressive_mode || cinfo.jpeg_color_space == JCS_CMYK ||
        cinfo.jpeg_color_space == JCS_YCCK) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    cinfo.out_color_space = JCS_RGB;
    cinfo.dct_method = JDCT_IFAST;
    cinfo.do_fancy_upsampling = FALSE;
    cinfo.do_block_smoothing = FALSE;
    choose_scale(&cinfo, width, height);

    jpeg_start_decompress(&cinfo);
    if (cinfo.output_components != 3) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, (int)cinfo.output_width,
                         (int)cinfo.output_height);
    if (pix == NULL) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    guchar* pixels = gdk_pixbuf_get_pixels(pix);
    gsize stride = (gsize)gdk_pixbuf_get_rowstride(pix);
    while (cinfo.output_scanline < cinfo.output_height) {
        if (cinfo.output_scanline % CANCEL_CHECK_ROWS == 0 && g_cancellable_is_cancelled(cancellable)) {
            jpeg_destroy_decompress(&cinfo);
            fclose(fp);
            g_object_unref(pix);
            return NULL;
        }
        JSAMPROW row = pixels + (gsize)cinfo.output_scanline * stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    GdkPixbuf* decoded = pix;
    if (gdk_pixbuf_get_width(decoded) == width && gdk_pixbuf_get_height(decoded) == height) {
        return decoded;
    }
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(decoded, width, height, GDK_INTERP_BILINEAR);
    g_object_unref(decoded);
    return scaled;
}

#else /* !HAVE_LIBJPEG */

GdkPixbuf* jpeg_decode_file(const gchar* path, gint width, gint height, GCancellable* cancellable) {
    (void)path;
    (void)width;
    (void)height;
    (void)cancellable;
    return NULL;
}

#endif /* HAVE_LIBJPEG */
