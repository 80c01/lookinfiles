/* Unit tests for src/explorer/trash_restore.c: put back files moved to the trash. */
#include "explorer/trash_restore.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

static void write_file(const gchar* dir, const gchar* name, const gchar* text) {
    g_mkdir_with_parents(dir, 0700);
    gchar* path = g_build_filename(dir, name, NULL);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(path);
}

/* path with native separators (the parser returns local file names) */
static gchar* native(const gchar* path) {
    gchar* out = g_strdup(path);
    g_strdelimit(out, "/", G_DIR_SEPARATOR);
    return out;
}

static void test_parse(void) {
    gchar* path = NULL;
    gchar* date = NULL;
    g_autofree gchar* want = native("/home/u/a b.txt");
    g_autofree gchar* want_mount = native("/media/usb/docs/x");
    g_assert_true(trash_info_parse("[Trash Info]\nPath=/home/u/a%20b.txt\n"
                                   "DeletionDate=2026-10-31T10:00:00\n",
                                   NULL, &path, &date));
    g_assert_cmpstr(path, ==, want);
    g_assert_cmpstr(date, ==, "2026-10-31T10:00:00");
    g_free(path);
    g_free(date);

    /* Relative paths belong to a mount's trash only */
    g_assert_false(trash_info_parse("[Trash Info]\nPath=docs/x\n", NULL, &path, &date));
    g_assert_true(trash_info_parse("[Trash Info]\nPath=docs/x\n", "/media/usb", &path, &date));
    g_assert_cmpstr(path, ==, want_mount);
    g_assert_cmpstr(date, ==, "");
    g_free(path);
    g_free(date);

    g_assert_false(trash_info_parse("garbage", NULL, &path, &date));
    g_assert_null(path);
}

static void test_find_newest(void) {
    gchar* trash = g_build_filename(g_get_tmp_dir(), "trash-find", NULL);
    gchar* info_dir = g_build_filename(trash, "info", NULL);
    gchar* files_dir = g_build_filename(trash, "files", NULL);
    write_file(info_dir, "a.txt.trashinfo",
               "[Trash Info]\nPath=/w/a.txt\nDeletionDate=2026-10-30T09:00:00\n");
    write_file(info_dir, "a.2.txt.trashinfo",
               "[Trash Info]\nPath=/w/a.txt\nDeletionDate=2026-10-31T09:00:00\n");
    write_file(info_dir, "b.txt.trashinfo",
               "[Trash Info]\nPath=/w/b.txt\nDeletionDate=2026-10-31T11:00:00\n");
    write_file(files_dir, "a.txt", "old");
    write_file(files_dir, "a.2.txt", "new");

    gchar* info = NULL;
    gchar* item = trash_find_item(trash, NULL, "/w/a.txt", &info);
    gchar* want = g_build_filename(files_dir, "a.2.txt", NULL);
    g_assert_cmpstr(item, ==, want);
    g_assert_true(g_str_has_suffix(info, "a.2.txt.trashinfo"));
    g_free(want);
    g_free(item);
    g_free(info);

    g_assert_null(trash_find_item(trash, NULL, "/w/c.txt", &info));
    g_assert_null(info);
    g_free(files_dir);
    g_free(info_dir);
    g_free(trash);
}

/* Bytes of a Recycle Bin $I file (version 1: path in 260 units; 2: counted) */
static GByteArray* win_info(guint64 version, guint64 time, const gchar* path) {
    GByteArray* b = g_byte_array_new();
    guint64 head[3] = {version, 1234, time};
    for (gsize i = 0; i < 3; i++) {
        for (int k = 0; k < 8; k++) {
            guint8 byte = (guint8)(head[i] >> (8 * k));
            g_byte_array_append(b, &byte, 1);
        }
    }
    glong n = 0;
    gunichar2* wide = g_utf8_to_utf16(path, -1, NULL, &n, NULL);
    glong units = version == 1 ? 260 : n + 1;
    if (version == 2) {
        for (int k = 0; k < 4; k++) {
            guint8 byte = (guint8)((guint32)units >> (8 * k));
            g_byte_array_append(b, &byte, 1);
        }
    }
    for (glong i = 0; i < units; i++) {
        gunichar2 c = i < n ? wide[i] : 0;
        guint8 two[2] = {(guint8)(c & 0xff), (guint8)(c >> 8)};
        g_byte_array_append(b, two, 2);
    }
    g_free(wide);
    return b;
}

static void write_bytes(const gchar* dir, const gchar* name, GByteArray* b) {
    g_mkdir_with_parents(dir, 0700);
    gchar* path = g_build_filename(dir, name, NULL);
    g_assert_true(g_file_set_contents(path, (const gchar*)b->data, (gssize)b->len, NULL));
    g_free(path);
    g_byte_array_unref(b);
}

static void test_win_parse(void) {
    gchar* path = NULL;
    guint64 time = 0;
    GByteArray* b = win_info(2, 133000000000000000ULL, "C:\\Users\\\xc3\xa9t\xc3\xa9\\a.txt");
    g_assert_true(trash_win_info_parse(b->data, b->len, &path, &time));
    g_assert_cmpstr(path, ==, "C:\\Users\\\xc3\xa9t\xc3\xa9\\a.txt");
    g_assert_cmpuint(time, ==, 133000000000000000ULL);
    g_free(path);
    /* A count past the end of the data */
    g_assert_false(trash_win_info_parse(b->data, b->len - 4, &path, &time));
    g_assert_null(path);
    g_byte_array_unref(b);

    b = win_info(1, 42, "D:\\old.doc");
    g_assert_cmpuint(b->len, ==, 24 + 520);
    g_assert_true(trash_win_info_parse(b->data, b->len, &path, &time));
    g_assert_cmpstr(path, ==, "D:\\old.doc");
    g_assert_cmpuint(time, ==, 42);
    g_free(path);
    g_byte_array_unref(b);

    b = win_info(3, 42, "D:\\x");
    g_assert_false(trash_win_info_parse(b->data, b->len, &path, &time));
    g_byte_array_unref(b);
    g_assert_false(trash_win_info_parse((const guint8*)"short", 5, &path, &time));
}

static void test_win_find_newest(void) {
    gchar* bin = g_build_filename(g_get_tmp_dir(), "recycle-find", NULL);
    write_bytes(bin, "$IAAAAAA.txt", win_info(2, 100, "C:\\Work\\a.txt"));
    write_file(bin, "$RAAAAAA.txt", "old");
    /* Same file, case and slashes differing, deleted later */
    write_bytes(bin, "$IBBBBBB.txt", win_info(1, 200, "c:/work/A.TXT"));
    write_file(bin, "$RBBBBBB.txt", "new");
    /* Newer still, but its $R file is gone */
    write_bytes(bin, "$ICCCCCC.txt", win_info(2, 300, "C:\\Work\\a.txt"));
    write_bytes(bin, "$IDDDDDD.txt", win_info(2, 400, "C:\\Work\\b.txt"));
    write_file(bin, "$RDDDDDD.txt", "b");

    gchar* info = NULL;
    gchar* item = trash_win_find_item(bin, "C:\\Work\\a.txt", &info);
    gchar* want = g_build_filename(bin, "$RBBBBBB.txt", NULL);
    g_assert_cmpstr(item, ==, want);
    g_assert_true(g_str_has_suffix(info, "$IBBBBBB.txt"));
    g_free(want);
    g_free(item);
    g_free(info);

    g_assert_null(trash_win_find_item(bin, "C:\\Work\\c.txt", &info));
    g_assert_null(info);
    g_free(bin);
}

/* g_file_trash() then trash_restore() (dirs isolated: the home trash is
 * under the test's XDG_DATA_HOME, on the same disk as the file) */
static void test_trash_and_restore(void) {
    gchar* dir = g_build_filename(g_get_user_data_dir(), "work", NULL);
    write_file(dir, "note.txt", "hello");
    gchar* path = g_build_filename(dir, "note.txt", NULL);
    GFile* file = g_file_new_for_path(path);
    GError* err = NULL;
    if (!g_file_trash(file, NULL, &err)) {
        g_test_skip(err->message);
        g_clear_error(&err);
    } else {
        g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
        g_assert_true(trash_restore(path, &err));
        g_assert_no_error(err);
        gchar* text = NULL;
        g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
        g_assert_cmpstr(text, ==, "hello");
        g_free(text);
        /* Nothing left to restore, and an existing file is never overwritten */
        g_assert_false(trash_restore(path, &err));
        g_assert_error(err, G_IO_ERROR, G_IO_ERROR_EXISTS);
        g_clear_error(&err);
    }
    g_object_unref(file);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/trash_restore/parse", test_parse);
    g_test_add_func("/trash_restore/find_newest", test_find_newest);
    g_test_add_func("/trash_restore/win_parse", test_win_parse);
    g_test_add_func("/trash_restore/win_find_newest", test_win_find_newest);
    g_test_add_func("/trash_restore/trash_and_restore", test_trash_and_restore);
    return g_test_run();
}
