/* Unit tests for src/formats/hex_dump.c: hex dump of binary content. */
#include "formats/hex_dump.h"

#include <string.h>

static void test_empty(void) {
    gchar* s = hex_dump_format(NULL, 0, 0);
    g_assert_cmpstr(s, ==, "");
    g_free(s);
}

static void test_full_line(void) {
    const guchar data[16] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    gchar* s = hex_dump_format(data, sizeof data, 0);
    g_assert_cmpstr(s, ==,
        "00000000  7f 45 4c 46 02 01 01 00  00 00 00 00 00 00 00 00  |.ELF............|\n");
    g_assert_cmpuint(strlen(s), ==, HEX_DUMP_LINE_WIDTH + 1);
    g_assert_cmpint(s[HEX_DUMP_ASCII_COLUMN], ==, '|');
    g_free(s);
}

static void test_partial_last_line_is_padded(void) {
    const guchar data[19] = "0123456789abcdefXYZ";
    gchar* s = hex_dump_format(data, sizeof data, 0);
    g_assert_cmpstr(s, ==,
        "00000000  30 31 32 33 34 35 36 37  38 39 61 62 63 64 65 66  |0123456789abcdef|\n"
        "00000010  58 59 5a                                          |XYZ             |\n");
    gchar** lines = g_strsplit(s, "\n", -1);
    g_assert_cmpuint(strlen(lines[0]), ==, HEX_DUMP_LINE_WIDTH);
    g_assert_cmpuint(strlen(lines[1]), ==, HEX_DUMP_LINE_WIDTH);
    g_assert_cmpint(lines[1][HEX_DUMP_ASCII_COLUMN], ==, '|');
    g_strfreev(lines);
    g_free(s);
}

static void test_base_offset(void) {
    const guchar data[1] = {0xff};
    gchar* s = hex_dump_format(data, 1, 0x12345670);
    g_assert_true(g_str_has_prefix(s, "12345670  ff "));
    g_free(s);
}

static void test_non_ascii_is_valid_utf8(void) {
    guchar data[256];
    for (guint i = 0; i < 256; i++) {
        data[i] = (guchar)i;
    }
    gchar* s = hex_dump_format(data, sizeof data, 0);
    g_assert_true(g_utf8_validate(s, -1, NULL));
    /* Line 0x80..0x8f: only dots in the ASCII column */
    gchar** lines = g_strsplit(s, "\n", -1);
    g_assert_true(g_str_has_suffix(lines[8], "|................|"));
    g_assert_true(g_str_has_suffix(lines[4], "|@ABCDEFGHIJKLMNO|"));
    g_strfreev(lines);
    g_free(s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hex_dump/empty", test_empty);
    g_test_add_func("/hex_dump/full_line", test_full_line);
    g_test_add_func("/hex_dump/partial_last_line_is_padded", test_partial_last_line_is_padded);
    g_test_add_func("/hex_dump/base_offset", test_base_offset);
    g_test_add_func("/hex_dump/non_ascii_is_valid_utf8", test_non_ascii_is_valid_utf8);
    return g_test_run();
}
