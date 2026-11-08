/* Unit tests for src/formats/font_info.c (OpenType name table, WOFF). Uses the fonts
 * installed on the system when present. */
#include "formats/font_info.h"

#include <gio/gio.h>
#include <string.h>

static const gchar* first_existing(const gchar* const* paths) {
    for (gint i = 0; paths[i] != NULL; i++) {
        if (g_file_test(paths[i], G_FILE_TEST_EXISTS)) {
            return paths[i];
        }
    }
    return NULL;
}

static void test_truetype(void) {
    const gchar* const paths[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", NULL };
    const gchar* path = first_existing(paths);
    if (path == NULL) {
        g_test_skip("DejaVu Sans Bold not installed");
        return;
    }
    GError* error = NULL;
    g_autoptr(FontInfo) info = font_info_read_file(path, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(info->format, ==, "TrueType");
    g_assert_cmpstr(info->family, ==, "DejaVu Sans");
    g_assert_cmpstr(info->style, ==, "Bold");
    g_assert_cmpuint(info->weight, ==, 700);
    g_assert_cmpstr(font_info_weight_name(info->weight), ==, "Bold");
    g_assert_cmpuint(info->n_glyphs, >, 1000);
    g_assert_nonnull(info->version);
    g_assert_false(g_str_has_prefix(info->version, "Version "));
}

static void test_opentype(void) {
    const gchar* const paths[] = { "/usr/share/fonts/opentype/urw-base35/URWGothic-BookOblique.otf", NULL };
    const gchar* path = first_existing(paths);
    if (path == NULL) {
        g_test_skip("URW Gothic not installed");
        return;
    }
    g_autoptr(FontInfo) info = font_info_read_file(path, NULL);
    g_assert_nonnull(info);
    g_assert_cmpstr(info->format, ==, "OpenType (CFF)");
    g_assert_nonnull(strstr(info->style, "Oblique"));
}

/* WOFF built from a TrueType font: every table zlib-compressed */
static void test_woff(void) {
    const gchar* const paths[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", NULL };
    const gchar* path = first_existing(paths);
    if (path == NULL) {
        g_test_skip("DejaVu Sans Bold not installed");
        return;
    }
    gchar* ttf = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents(path, &ttf, &len, NULL));
    const guchar* d = (const guchar*)ttf;
    guint n = (guint)((d[4] << 8) | d[5]);
    GByteArray* woff = g_byte_array_new();
    guint8 header[44] = { 'w', 'O', 'F', 'F', 0, 1, 0, 0 };
    header[12] = (guint8)(n >> 8);
    header[13] = (guint8)n;
    g_byte_array_append(woff, header, sizeof(header));
    g_byte_array_set_size(woff, 44 + n * 20);
    for (guint i = 0; i < n; i++) {
        const guchar* e = d + 12 + i * 16;
        guint32 off = ((guint32)e[8] << 24) | ((guint32)e[9] << 16) | ((guint32)e[10] << 8) | e[11];
        guint32 tlen = ((guint32)e[12] << 24) | ((guint32)e[13] << 16) | ((guint32)e[14] << 8) | e[15];
        GZlibCompressor* c = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, 6);
        guchar* out = g_malloc(tlen + 1024);
        gsize read = 0, written = 0;
        g_converter_convert(G_CONVERTER(c), d + off, tlen, out, tlen + 1024, G_CONVERTER_INPUT_AT_END,
                            &read, &written, NULL);
        g_object_unref(c);
        guint32 woff_off = woff->len;
        guint8* we = woff->data + 44 + i * 20;
        memcpy(we, e, 4);
        guint32 vals[] = { woff_off, (guint32)written, tlen, 0 };
        for (gint k = 0; k < 4; k++) {
            we[4 + k * 4] = (guint8)(vals[k] >> 24);
            we[5 + k * 4] = (guint8)(vals[k] >> 16);
            we[6 + k * 4] = (guint8)(vals[k] >> 8);
            we[7 + k * 4] = (guint8)vals[k];
        }
        g_byte_array_append(woff, out, (guint)written);
        g_free(out);
    }
    GError* error = NULL;
    g_autoptr(FontInfo) info = font_info_parse(woff->data, woff->len, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(info->format, ==, "WOFF");
    g_assert_cmpstr(info->family, ==, "DejaVu Sans");
    g_assert_cmpuint(info->weight, ==, 700);
    g_byte_array_unref(woff);
    g_free(ttf);
}

static void test_not_a_font(void) {
    GError* error = NULL;
    g_assert_null(font_info_parse((const guchar*)"hello world, not a font", 23, &error));
    g_assert_nonnull(error);
    g_error_free(error);
    /* WOFF2: only the format is known */
    g_autoptr(FontInfo) w2 = font_info_parse((const guchar*)"wOF2\0\1\0\0\0\0\0\0", 12, NULL);
    g_assert_cmpstr(w2->format, ==, "WOFF2");
    g_assert_null(w2->family);
    /* Truncated table directory */
    g_assert_null(font_info_parse((const guchar*)"\0\1\0\0\0\x10\0\0\0\0\0\0", 12, NULL));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/font_info/truetype", test_truetype);
    g_test_add_func("/font_info/opentype", test_opentype);
    g_test_add_func("/font_info/woff", test_woff);
    g_test_add_func("/font_info/not_a_font", test_not_a_font);
    return g_test_run();
}
