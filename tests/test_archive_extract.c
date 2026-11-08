/* Unit tests for src/formats/archive_extract.c (entry paths, ZIP extraction). */
#include "formats/archive_extract.h"

#include <glib/gstdio.h>
#include <string.h>

#include "zip_builder.h"

static void test_clean_path(void) {
    gchar* p = archive_extract_clean_path("./dir//file.txt");
    g_assert_cmpstr(p, ==, "dir/file.txt");
    g_free(p);
    p = archive_extract_clean_path("/abs/dir/");
    g_assert_cmpstr(p, ==, "abs/dir");
    g_free(p);
    g_assert_null(archive_extract_clean_path("../evil"));
    g_assert_null(archive_extract_clean_path("a/../../evil"));
    g_assert_null(archive_extract_clean_path("./"));
    g_assert_null(archive_extract_clean_path(NULL));
}

static void test_can_browse(void) {
    g_assert_true(archive_extract_can_browse("a.zip"));
    g_assert_true(archive_extract_can_browse("LIB.JAR"));
    g_assert_false(archive_extract_can_browse("a.txt"));
    g_assert_false(archive_extract_can_browse("zip"));
    g_assert_false(archive_extract_can_browse("report.docx"));
}

static void test_extract_zip(void) {
    gchar* dir = g_dir_make_tmp("lookinfiles-extract-XXXXXX", NULL);
    g_assert_nonnull(dir);
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "docs/readme.txt", "hello archive\n", TRUE);
    zip_builder_add(zb, "big.bin", "0123456789", FALSE);
    GBytes* zip = zip_builder_finish(zb);
    gchar* archive = g_build_filename(dir, "sample.zip", NULL);
    gsize len = 0;
    const gchar* bytes = g_bytes_get_data(zip, &len);
    g_assert_true(g_file_set_contents(archive, bytes, (gssize)len, NULL));
    g_bytes_unref(zip);

    gchar* dest = g_build_filename(dir, "out", "docs", "readme.txt", NULL);
    GError* error = NULL;
    g_assert_true(archive_extract_entry(archive, "docs/readme.txt", dest, 1024, NULL, &error));
    g_assert_no_error(error);
    gchar* text = NULL;
    g_assert_true(g_file_get_contents(dest, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "hello archive\n");
    g_free(text);

    /* Too large and missing entries fail without creating the file */
    gchar* dest2 = g_build_filename(dir, "out", "big.bin", NULL);
    g_assert_false(archive_extract_entry(archive, "big.bin", dest2, 4, NULL, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_false(archive_extract_entry(archive, "nope", dest2, 1024, NULL, &error));
    g_clear_error(&error);
    g_assert_false(g_file_test(dest2, G_FILE_TEST_EXISTS));

    gchar* cache = archive_extract_cache_path(archive, "docs/readme.txt");
    g_assert_nonnull(cache);
    g_assert_true(g_str_has_suffix(cache, "/docs/readme.txt"));
    g_free(cache);
    g_assert_null(archive_extract_cache_path(archive, "../x"));

    g_remove(dest);
    g_free(dest);
    g_free(dest2);
    g_free(archive);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/archive_extract/clean_path", test_clean_path);
    g_test_add_func("/archive_extract/can_browse", test_can_browse);
    g_test_add_func("/archive_extract/zip", test_extract_zip);
    return g_test_run();
}
