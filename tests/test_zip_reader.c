/* Unit tests for src/formats/zip_reader.c: ZIP central directory and entry reading. */
#include "formats/zip_reader.h"
#include "zip_builder.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

static ZipReader* open_bytes(GBytes* bytes) {
    GError* err = NULL;
    ZipReader* zip = zip_reader_new_from_bytes(bytes, &err);
    g_assert_no_error(err);
    g_assert_nonnull(zip);
    return zip;
}

static void check_entry(ZipReader* zip, const gchar* name, const gchar* expected) {
    GError* err = NULL;
    GBytes* b = zip_reader_read(zip, name, 1 << 20, &err);
    g_assert_no_error(err);
    gsize len = 0;
    const gchar* data = g_bytes_get_data(b, &len);
    g_assert_cmpuint(len, ==, strlen(expected));
    g_assert_cmpstr(data, ==, expected); /* NUL-terminated */
    g_bytes_unref(b);
}

static gchar* repeated_text(guint n) {
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < n; i++) {
        g_string_append_printf(s, "<w:p>line %u</w:p>\n", i);
    }
    return g_string_free(s, FALSE);
}

static void test_stored_and_deflated(void) {
    gchar* big = repeated_text(5000);
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "mimetype", "application/test", FALSE);
    zip_builder_add(zb, "word/document.xml", big, TRUE);
    zip_builder_add(zb, "empty.txt", "", TRUE);
    GBytes* bytes = zip_builder_finish(zb);

    ZipReader* zip = open_bytes(bytes);
    g_assert_cmpuint(zip_reader_get_n_entries(zip), ==, 3);
    g_assert_cmpstr(zip_reader_get_name(zip, 1), ==, "word/document.xml");
    g_assert_true(zip_reader_has(zip, "mimetype"));
    g_assert_false(zip_reader_has(zip, "word"));
    g_assert_cmpint(zip_reader_get_size(zip, "word/document.xml"), ==, (gint64)strlen(big));
    g_assert_cmpint(zip_reader_get_size(zip, "nope"), ==, -1);
    check_entry(zip, "mimetype", "application/test");
    check_entry(zip, "word/document.xml", big);
    check_entry(zip, "empty.txt", "");

    zip_reader_free(zip);
    g_bytes_unref(bytes);
    g_free(big);
}

static void test_zip64(void) {
    ZipBuilder* zb = zip_builder_new(TRUE);
    zip_builder_add(zb, "a.txt", "alpha", FALSE);
    zip_builder_add(zb, "b.txt", "bravo bravo bravo bravo", TRUE);
    GBytes* bytes = zip_builder_finish(zb);

    ZipReader* zip = open_bytes(bytes);
    g_assert_cmpuint(zip_reader_get_n_entries(zip), ==, 2);
    check_entry(zip, "a.txt", "alpha");
    check_entry(zip, "b.txt", "bravo bravo bravo bravo");
    zip_reader_free(zip);
    g_bytes_unref(bytes);
}

static void test_errors(void) {
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "a.txt", "0123456789", TRUE);
    zip_builder_add_full(zb, "secret.txt", "xyz", 3, FALSE, 0x0001);
    GBytes* bytes = zip_builder_finish(zb);
    ZipReader* zip = open_bytes(bytes);
    GError* err = NULL;

    g_assert_null(zip_reader_read(zip, "missing", 100, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&err);

    g_assert_null(zip_reader_read(zip, "a.txt", 9, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE);
    g_clear_error(&err);

    g_assert_null(zip_reader_read(zip, "secret.txt", 100, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_clear_error(&err);

    zip_reader_free(zip);
    g_bytes_unref(bytes);
}

static void test_damaged_data(void) {
    gchar* big = repeated_text(200);
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "doc.xml", big, TRUE);
    GBytes* bytes = zip_builder_finish(zb);

    /* Corrupt the compressed data just after the local header and name */
    gsize len = 0;
    const guint8* orig = g_bytes_get_data(bytes, &len);
    guint8* copy = g_memdup2(orig, len);
    for (gsize i = 30 + 7; i < 30 + 7 + 40; i++) {
        copy[i] ^= 0x5A;
    }
    GBytes* damaged = g_bytes_new_take(copy, len);
    ZipReader* zip = open_bytes(damaged);
    GError* err = NULL;
    GBytes* b = zip_reader_read(zip, "doc.xml", 1 << 20, &err);
    if (b != NULL) {
        /* Garbage that still inflates must not match the original */
        g_assert_true(g_bytes_get_size(b) == strlen(big));
        g_assert_true(memcmp(g_bytes_get_data(b, NULL), big, strlen(big)) != 0);
        g_bytes_unref(b);
    } else {
        g_assert_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_clear_error(&err);
    }
    zip_reader_free(zip);
    g_bytes_unref(damaged);
    g_bytes_unref(bytes);
    g_free(big);
}

static void test_not_a_zip(void) {
    const gchar* inputs[] = { "", "PK", "hello world, this is plain text and not an archive at all" };
    for (gsize i = 0; i < G_N_ELEMENTS(inputs); i++) {
        GBytes* bytes = g_bytes_new_static(inputs[i], strlen(inputs[i]));
        GError* err = NULL;
        g_assert_null(zip_reader_new_from_bytes(bytes, &err));
        g_assert_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_clear_error(&err);
        g_bytes_unref(bytes);
    }
}

static void test_truncated_archive(void) {
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "a.txt", "some content here", FALSE);
    GBytes* bytes = zip_builder_finish(zb);
    gsize len = 0;
    const guint8* data = g_bytes_get_data(bytes, &len);
    /* Drop the start of the file: offsets now point past the data */
    GBytes* cut = g_bytes_new(data + 20, len - 20);
    GError* err = NULL;
    ZipReader* zip = zip_reader_new_from_bytes(cut, &err);
    if (zip != NULL) {
        g_assert_null(zip_reader_read(zip, "a.txt", 100, &err));
        g_assert_nonnull(err);
        zip_reader_free(zip);
    }
    g_assert_nonnull(err);
    g_clear_error(&err);
    g_bytes_unref(cut);
    g_bytes_unref(bytes);
}

static void test_open_file(void) {
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "hello.txt", "hello from disk", TRUE);
    GBytes* bytes = zip_builder_finish(zb);
    gchar* dir = g_dir_make_tmp("zip_reader_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "t.zip", NULL);
    g_assert_true(g_file_set_contents(path, g_bytes_get_data(bytes, NULL),
                                      (gssize)g_bytes_get_size(bytes), NULL));
    GError* err = NULL;
    ZipReader* zip = zip_reader_open(path, &err);
    g_assert_no_error(err);
    check_entry(zip, "hello.txt", "hello from disk");
    zip_reader_free(zip);

    g_assert_null(zip_reader_open("/nonexistent/file.zip", &err));
    g_assert_nonnull(err);
    g_clear_error(&err);

    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
    g_bytes_unref(bytes);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/zip_reader/stored_and_deflated", test_stored_and_deflated);
    g_test_add_func("/zip_reader/zip64", test_zip64);
    g_test_add_func("/zip_reader/errors", test_errors);
    g_test_add_func("/zip_reader/damaged_data", test_damaged_data);
    g_test_add_func("/zip_reader/not_a_zip", test_not_a_zip);
    g_test_add_func("/zip_reader/truncated_archive", test_truncated_archive);
    g_test_add_func("/zip_reader/open_file", test_open_file);
    return g_test_run();
}
