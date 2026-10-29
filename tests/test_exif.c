/* Unit tests for src/exif.c (EXIF in JPEG / TIFF, summary lines). */
#include "exif.h"

#include <math.h>
#include <string.h>

static void test_jpeg(void) {
    g_autoptr(ExifInfo) info = exif_read_file("tests/fixtures/preview/photo.jpg");
    g_assert_nonnull(info);
    g_assert_cmpstr(info->make, ==, "Canon");
    g_assert_cmpstr(info->model, ==, "Canon EOS R5");
    g_assert_cmpstr(info->lens, ==, "RF24-105mm F4 L IS USM");
    g_assert_cmpint(info->orientation, ==, 6);
    g_assert_cmpfloat_with_epsilon(info->exposure_time, 1.0 / 250, 1e-9);
    g_assert_cmpfloat_with_epsilon(info->f_number, 4.0, 1e-9);
    g_assert_cmpint(info->iso, ==, 200);
    g_assert_cmpint(info->focal_length_35mm, ==, 75);
    g_assert_true(info->has_gps);
    g_assert_cmpfloat_with_epsilon(info->latitude, 48 + 51 / 60.0 + 30.24 / 3600, 1e-6);
    g_assert_cmpfloat_with_epsilon(info->longitude, 2 + 17 / 60.0 + 40.2 / 3600, 1e-6);
    g_assert_true(info->has_altitude);
    g_assert_cmpfloat_with_epsilon(info->altitude, 35, 1e-9);

    gchar** lines = exif_summary_lines(info);
    g_assert_cmpstr(lines[0], ==, "Canon EOS R5 · RF24-105mm F4 L IS USM");
    g_assert_cmpstr(lines[1], ==, "1/250 s · f/4 · ISO 200 · 50 mm (75 mm equiv.) · flash");
    g_assert_cmpstr(lines[2], ==, "Taken 2026-10-29 10:00");
    g_assert_cmpstr(lines[3], ==, "48.85840° N, 2.29450° E · 35 m");
    g_assert_cmpstr(lines[4], ==, "Edited with GIMP 2.10");
    g_assert_null(lines[5]);
    g_strfreev(lines);
}

static void test_big_endian_tiff(void) {
    /* MM header, IFD0 with Orientation = 3 and a southern / western GPS IFD */
    static const guchar tiff[] = {
        'M', 'M', 0, 42, 0, 0, 0, 8,
        0, 2,
        0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, 3, 0, 0,
        0x88, 0x25, 0, 4, 0, 0, 0, 1, 0, 0, 0, 38,
        0, 0, 0, 0,
        /* GPS IFD at 38: 4 entries */
        0, 4,
        0, 1, 0, 2, 0, 0, 0, 2, 'S', 0, 0, 0,
        0, 2, 0, 5, 0, 0, 0, 3, 0, 0, 0, 92,
        0, 3, 0, 2, 0, 0, 0, 2, 'W', 0, 0, 0,
        0, 4, 0, 5, 0, 0, 0, 3, 0, 0, 0, 116,
        0, 0, 0, 0,
        /* rationals at 92: 10/1 30/1 0/1, at 116: 20/1 0/1 0/1 */
        0, 0, 0, 10, 0, 0, 0, 1, 0, 0, 0, 30, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
        0, 0, 0, 20, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    };
    g_autoptr(ExifInfo) info = exif_parse(tiff, sizeof(tiff));
    g_assert_nonnull(info);
    g_assert_cmpint(info->orientation, ==, 3);
    g_assert_true(info->has_gps);
    g_assert_cmpfloat_with_epsilon(info->latitude, -10.5, 1e-9);
    g_assert_cmpfloat_with_epsilon(info->longitude, -20.0, 1e-9);
    gchar** lines = exif_summary_lines(info);
    g_assert_cmpstr(lines[0], ==, "10.50000° S, 20.00000° W");
    g_strfreev(lines);
}

static void test_no_exif(void) {
    static const guchar png[] = "\x89PNG\r\n\x1a\n\0\0\0\rIHDR";
    g_assert_null(exif_parse(png, sizeof(png) - 1));
    g_assert_null(exif_parse((const guchar*)"hello", 5));
    static const guchar jpeg[] = { 0xFF, 0xD8, 0xFF, 0xDA, 0, 4, 0, 0 };
    g_assert_null(exif_parse(jpeg, sizeof(jpeg)));
    /* Damaged offsets are ignored, not read past the end */
    static const guchar bad[] = { 'I', 'I', 42, 0, 0xF0, 0xFF, 0, 0 };
    g_autoptr(ExifInfo) info = exif_parse(bad, sizeof(bad));
    g_assert_nonnull(info);
    g_assert_cmpint(info->orientation, ==, 0);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/exif/jpeg", test_jpeg);
    g_test_add_func("/exif/big_endian_tiff", test_big_endian_tiff);
    g_test_add_func("/exif/no_exif", test_no_exif);
    return g_test_run();
}
