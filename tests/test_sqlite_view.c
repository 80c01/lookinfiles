/* Unit tests for src/formats/sqlite_view.c (SQLite database preview). */
#include "formats/sqlite_view.h"

#include <string.h>

static gchar* fixture(gsize* len) {
    gchar* data = NULL;
    g_assert_true(g_file_get_contents("tests/fixtures/preview/library.sqlite", &data, len, NULL));
    return data;
}

static void test_header(void) {
    gsize len;
    g_autofree gchar* data = fixture(&len);
    SqliteHeader h;
    g_assert_true(sqlite_view_read_header((const guchar*)data, len, &h));
    g_assert_cmpuint(h.page_size, ==, 4096);
    g_assert_cmpuint(h.user_version, ==, 7);
    g_assert_cmpstr(h.encoding, ==, "UTF-8");
    g_assert_cmpuint(h.page_count, ==, len / 4096);
    g_assert_false(sqlite_view_read_header((const guchar*)"SQLite format 2", 15, &h));
}

static void test_tables(void) {
    gsize len;
    g_autofree gchar* data = fixture(&len);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    g_assert_true(sqlite_view_append(doc, "tests/fixtures/preview/library.sqlite", (const guchar*)data, len, &error));
    g_assert_no_error(error);
    const gchar* out = doc->text->str;
    g_assert_nonnull(strstr(out, "User version   7"));
#ifdef HAVE_SQLITE
    g_assert_nonnull(strstr(out, "books  │ table │    4 │ id, title, author, year, price, cover"));
    g_assert_nonnull(strstr(out, "recent │ view  │"));
    g_assert_nonnull(strstr(out, "CREATE TABLE books"));
    g_assert_nonnull(strstr(out, "Neuromancer"));
    g_assert_nonnull(strstr(out, "<blob 7 bytes>"));
    g_assert_nonnull(strstr(out, "NULL"));
    /* loans has no rows */
    g_assert_nonnull(strstr(out, "loans\nCREATE TABLE loans"));
#endif
    g_autoptr(PreviewDoc) bad = preview_doc_new();
    g_assert_false(sqlite_view_append(bad, "x", (const guchar*)"nope", 4, NULL));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sqlite_view/header", test_header);
    g_test_add_func("/sqlite_view/tables", test_tables);
    return g_test_run();
}
