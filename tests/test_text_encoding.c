/* Unit tests for src/text_encoding.c. */
#include "text_encoding.h"

#include <string.h>

static gchar* decode(const gchar* data, gsize len, const gchar** enc) {
    return text_encoding_to_utf8(data, len, enc);
}

static void test_utf8(void) {
    const gchar* enc = NULL;
    gchar* s = decode("caf\xC3\xA9", 5, &enc);
    g_assert_cmpstr(enc, ==, "UTF-8");
    g_assert_cmpstr(s, ==, "café");
    g_free(s);
    s = decode("\xEF\xBB\xBFhi", 5, &enc);
    g_assert_cmpstr(enc, ==, "UTF-8 BOM");
    g_assert_cmpstr(s, ==, "hi");
    g_free(s);
    /* A character cut by the end of the data is still UTF-8 */
    g_assert_cmpstr(text_encoding_detect((const guchar*)"abc\xC3", 4), ==, "UTF-8");
}

static void test_utf16(void) {
    const gchar* enc = NULL;
    gchar* s = decode("\xFF\xFEh\0\xE9\0", 6, &enc);
    g_assert_cmpstr(enc, ==, "UTF-16LE");
    g_assert_cmpstr(s, ==, "hé");
    g_free(s);
    s = decode("\xFE\xFF\0h\0i", 6, &enc);
    g_assert_cmpstr(enc, ==, "UTF-16BE");
    g_assert_cmpstr(s, ==, "hi");
    g_free(s);
    /* Without a mark */
    const gchar le[] = "h\0e\0l\0l\0o\0 \0w\0o\0r\0l\0d\0";
    s = decode(le, sizeof(le) - 1, &enc);
    g_assert_cmpstr(enc, ==, "UTF-16LE");
    g_assert_cmpstr(s, ==, "hello world");
    g_free(s);
    /* Odd length: the cut unit is dropped */
    s = decode("\xFF\xFEh\0i\0x", 7, &enc);
    g_assert_cmpstr(s, ==, "hi");
    g_free(s);
}

static void test_legacy(void) {
    const gchar* enc = NULL;
    gchar* s = decode("caf\xE9 \x80 \x93q\x94", 10, &enc);
    g_assert_cmpstr(enc, ==, "Windows-1252");
    g_assert_cmpstr(s, ==, "café € “q”");
    g_free(s);
    /* Undefined in Windows-1252: Latin-1 */
    s = decode("a\x81\xE9", 3, &enc);
    g_assert_true(g_utf8_validate(s, -1, NULL));
    g_free(s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/text_encoding/utf8", test_utf8);
    g_test_add_func("/text_encoding/utf16", test_utf16);
    g_test_add_func("/text_encoding/legacy", test_legacy);
    return g_test_run();
}
