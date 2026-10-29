/* Unit tests for src/preview_formats.c (registry, ELF, documents, hex dump). */
#include "preview_formats.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_registry(void) {
    g_assert_true(preview_format_has_builder(FILE_KIND_EXECUTABLE));
    g_assert_true(preview_format_has_builder(FILE_KIND_DOCUMENT));
    g_assert_false(preview_format_has_builder(FILE_KIND_TEXT));
    g_assert_false(preview_format_has_builder(FILE_KIND_IMAGE));
    PreviewInput in = { .path = "/nonexistent", .name = "x", .kind = FILE_KIND_TEXT };
    GError* error = NULL;
    g_assert_null(preview_format_build(&in, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_error_free(error);
}

static void test_hex_dump(void) {
    guchar data[20];
    for (guint i = 0; i < sizeof(data); i++) {
        data[i] = (guchar)('A' + i);
    }
    g_autoptr(PreviewDoc) doc = preview_format_hex_dump("blob", FILE_KIND_BINARY, 100, data,
                                                        sizeof(data), NULL);
    g_assert_true(doc->monospace);
    g_assert_true(g_str_has_prefix(doc->text->str,
                                   "[ Binary file ] blob (100 bytes)\nShowing the first 20 bytes.\n\n"
                                   "00000000  41 42 43"));
    g_autoptr(PreviewDoc) doc2 = preview_format_hex_dump("a.zip", FILE_KIND_ARCHIVE, 20, data,
                                                         sizeof(data), "damaged");
    g_assert_true(g_str_has_prefix(doc2->text->str,
                                   "[ Archive file ] a.zip (20 bytes)\nCannot show the content: damaged.\n\n"));
    g_autoptr(PreviewDoc) empty = preview_format_hex_dump("e", FILE_KIND_BINARY, 0, NULL, 0, NULL);
    g_assert_cmpstr(empty->text->str, ==, "[ Binary file ] e (0 bytes)\n\n");
}

static void test_elf(void) {
    if (!g_file_test("/bin/true", G_FILE_TEST_EXISTS)) {
        g_test_skip("no /bin/true");
        return;
    }
    FileKind kind = FILE_KIND_TEXT;
    GError* error = NULL;
    g_autoptr(PreviewDoc) doc = preview_format_build_file("/bin/true", "true", 0, NULL, &kind, &error);
    g_assert_no_error(error);
    g_assert_cmpint(kind, ==, FILE_KIND_EXECUTABLE);
    g_assert_true(g_str_has_prefix(doc->text->str, "true\n"));
    g_assert_nonnull(strstr(doc->text->str, "Architecture"));
}

static void test_word_document(void) {
    const gchar* path = "tests/ui/fixtures/report.docx";
    FileKind kind = FILE_KIND_TEXT;
    GError* error = NULL;
    g_autoptr(PreviewDoc) doc = preview_format_build_file(path, "report.docx", 0, NULL, &kind, &error);
    g_assert_no_error(error);
    g_assert_cmpint(kind, ==, FILE_KIND_DOCUMENT);
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    g_assert_nonnull(strstr(doc->text->str, "<h1"));
}

static void test_damaged_document(void) {
    gchar* dir = g_dir_make_tmp("pf-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "broken.docx", NULL);
    g_assert_true(g_file_set_contents(path, "PK\x03\x04garbage", 11, NULL));
    GError* error = NULL;
    PreviewDoc* doc = preview_format_build_file(path, "broken.docx", 0, NULL, NULL, &error);
    g_assert_null(doc);
    g_assert_nonnull(error);
    g_error_free(error);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_formats/registry", test_registry);
    g_test_add_func("/preview_formats/hex_dump", test_hex_dump);
    g_test_add_func("/preview_formats/elf", test_elf);
    g_test_add_func("/preview_formats/word_document", test_word_document);
    g_test_add_func("/preview_formats/damaged_document", test_damaged_document);
    return g_test_run();
}
