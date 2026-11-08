/* Unit tests for src/office/ole_reader.c: OLE2 compound files built in memory
 * (tests/ole_builder.h), the LibreOffice fixtures, and damaged files. */
#include "ole_builder.h"
#include "office/ole_reader.h"

#include <gio/gio.h>
#include <string.h>

static GBytes* sample_file(void) {
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "WordDocument", "short text", 10);
    gchar* big = g_malloc(10000);
    for (gint i = 0; i < 10000; i++) big[i] = (gchar)('a' + i % 26);
    ole_builder_add(ob, "1Table", big, 10000);
    ole_builder_add(ob, "ObjectPool/_1234/Contents", "nested", 6);
    ole_builder_add(ob, "\005SummaryInformation", "", 0);
    g_free(big);
    return ole_builder_finish(ob);
}

static void test_entries(void) {
    GBytes* file = sample_file();
    GError* err = NULL;
    OleReader* ole = ole_reader_new_from_bytes(file, &err);
    g_assert_no_error(err);
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < ole_reader_get_n_entries(ole); i++) {
        OleEntryInfo info;
        g_assert_true(ole_reader_get_info(ole, i, &info));
        g_string_append_printf(s, "%s:%d:%" G_GUINT64_FORMAT ";", info.path, info.type, info.size);
    }
    g_assert_cmpstr(s->str, ==,
                    "\005SummaryInformation:2:0;1Table:2:10000;ObjectPool:1:0;ObjectPool/_1234:1:0;"
                    "ObjectPool/_1234/Contents:2:6;WordDocument:2:10;");
    g_string_free(s, TRUE);
    g_assert_true(ole_reader_has(ole, "worddocument"));
    g_assert_false(ole_reader_has(ole, "Workbook"));
    g_assert_null(ole_reader_get_root_clsid(ole));
    ole_reader_free(ole);
    g_bytes_unref(file);
}

static void test_read(void) {
    GBytes* file = sample_file();
    OleReader* ole = ole_reader_new_from_bytes(file, NULL);
    GError* err = NULL;
    /* Mini stream */
    GBytes* b = ole_reader_read(ole, "WordDocument", 1000, &err);
    g_assert_no_error(err);
    g_assert_cmpuint(g_bytes_get_size(b), ==, 10);
    g_assert_cmpstr(g_bytes_get_data(b, NULL), ==, "short text"); /* NUL-terminated */
    g_bytes_unref(b);
    /* Regular sectors */
    b = ole_reader_read(ole, "1Table", 1 << 20, &err);
    g_assert_no_error(err);
    const gchar* d = g_bytes_get_data(b, NULL);
    g_assert_cmpuint(g_bytes_get_size(b), ==, 10000);
    g_assert_cmpint(d[0], ==, 'a');
    g_assert_cmpint(d[9999], ==, 'a' + 9999 % 26);
    g_bytes_unref(b);
    b = ole_reader_read(ole, "ObjectPool/_1234/Contents", 100, &err);
    g_assert_cmpstr(g_bytes_get_data(b, NULL), ==, "nested");
    g_bytes_unref(b);
    b = ole_reader_read(ole, "\005SummaryInformation", 100, &err);
    g_assert_cmpuint(g_bytes_get_size(b), ==, 0);
    g_bytes_unref(b);
    /* Errors: too large, missing, storage */
    g_assert_null(ole_reader_read(ole, "1Table", 100, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&err);
    g_assert_null(ole_reader_read(ole, "Nope", 100, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&err);
    g_assert_null(ole_reader_read(ole, "ObjectPool", 100, &err));
    g_clear_error(&err);
    ole_reader_free(ole);
    g_bytes_unref(file);
}

/* Patch a little-endian u32 of a copy of data */
static GBytes* patched(GBytes* data, gsize offset, guint32 value) {
    gsize len = 0;
    const guint8* src = g_bytes_get_data(data, &len);
    guint8* copy = g_memdup2(src, len);
    copy[offset] = (guint8)value;
    copy[offset + 1] = (guint8)(value >> 8);
    copy[offset + 2] = (guint8)(value >> 16);
    copy[offset + 3] = (guint8)(value >> 24);
    return g_bytes_new_take(copy, len);
}

static void test_damaged(void) {
    GBytes* file = sample_file();
    gsize len = 0;
    const guint8* raw = g_bytes_get_data(file, &len);
    GError* err = NULL;
    /* Not a compound file, too short */
    GBytes* junk = g_bytes_new_static("PK\003\004 not ole", 14);
    g_assert_null(ole_reader_new_from_bytes(junk, &err));
    g_clear_error(&err);
    g_bytes_unref(junk);
    GBytes* cut = g_bytes_new(raw, 600);
    g_assert_null(ole_reader_new_from_bytes(cut, &err));
    g_clear_error(&err);
    g_bytes_unref(cut);
    /* A loop in the FAT: the regular stream's chain points to itself */
    OleReader* ole = ole_reader_new_from_bytes(file, NULL);
    g_assert_nonnull(ole);
    ole_reader_free(ole);
    /* Sector of 1Table: the last of the file; its FAT entry is in sector 0
     * (offset 512 + 4 * sector) */
    guint32 total = (guint32)(len / 512 - 1);
    guint32 first = total - (10000 + 511) / 512;
    GBytes* loop = patched(file, 512 + 4 * (first + 1), first);
    ole = ole_reader_new_from_bytes(loop, &err);
    g_assert_no_error(err);
    g_assert_null(ole_reader_read(ole, "1Table", 1 << 20, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&err);
    /* Other streams still read */
    GBytes* b = ole_reader_read(ole, "WordDocument", 100, &err);
    g_assert_nonnull(b);
    g_bytes_unref(b);
    ole_reader_free(ole);
    g_bytes_unref(loop);
    /* Out-of-range sector */
    GBytes* far = patched(file, 512 + 4 * first, 999999);
    ole = ole_reader_new_from_bytes(far, NULL);
    g_assert_null(ole_reader_read(ole, "1Table", 1 << 20, &err));
    g_clear_error(&err);
    ole_reader_free(ole);
    g_bytes_unref(far);
    /* Truncated in the middle of a stream */
    GBytes* half = g_bytes_new(raw, len - 2048);
    ole = ole_reader_new_from_bytes(half, &err);
    if (ole != NULL) {
        g_assert_null(ole_reader_read(ole, "1Table", 1 << 20, &err));
        g_clear_error(&err);
        ole_reader_free(ole);
    }
    g_clear_error(&err);
    g_bytes_unref(half);
    /* A directory whose sibling links form a loop */
    GBytes* dirloop = patched(file, (gsize)(raw[0x30] + 1) * 512 + 128 + 0x48, 1);
    ole = ole_reader_new_from_bytes(dirloop, NULL);
    if (ole != NULL) ole_reader_free(ole);
    g_bytes_unref(dirloop);
    g_bytes_unref(file);
}

static void test_fixtures(void) {
    const gchar* const files[] = {"report.doc", "budget.xls", "roadmap.ppt"};
    const gchar* const streams[] = {"WordDocument", "Workbook", "PowerPoint Document"};
    for (gsize i = 0; i < G_N_ELEMENTS(files); i++) {
        gchar* path = g_build_filename("tests", "fixtures", "preview", files[i], NULL);
        GError* err = NULL;
        OleReader* ole = ole_reader_open(path, &err);
        g_assert_no_error(err);
        g_assert_true(ole_reader_has(ole, streams[i]));
        g_assert_true(ole_reader_has(ole, "\005SummaryInformation"));
        for (guint k = 0; k < ole_reader_get_n_entries(ole); k++) {
            OleEntryInfo info;
            ole_reader_get_info(ole, k, &info);
            if (info.type != OLE_ENTRY_STREAM) continue;
            GBytes* b = ole_reader_read_entry(ole, k, 1 << 24, &err);
            g_assert_no_error(err);
            g_assert_cmpuint(g_bytes_get_size(b), ==, info.size);
            g_bytes_unref(b);
        }
        ole_reader_free(ole);
        g_free(path);
    }
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ole_reader/entries", test_entries);
    g_test_add_func("/ole_reader/read", test_read);
    g_test_add_func("/ole_reader/damaged", test_damaged);
    g_test_add_func("/ole_reader/fixtures", test_fixtures);
    return g_test_run();
}
