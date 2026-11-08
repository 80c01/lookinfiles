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

static void test_parse(void) {
    gchar* path = NULL;
    gchar* date = NULL;
    g_assert_true(trash_info_parse("[Trash Info]\nPath=/home/u/a%20b.txt\n"
                                   "DeletionDate=2026-10-31T10:00:00\n",
                                   NULL, &path, &date));
    g_assert_cmpstr(path, ==, "/home/u/a b.txt");
    g_assert_cmpstr(date, ==, "2026-10-31T10:00:00");
    g_free(path);
    g_free(date);

    /* Relative paths belong to a mount's trash only */
    g_assert_false(trash_info_parse("[Trash Info]\nPath=docs/x\n", NULL, &path, &date));
    g_assert_true(trash_info_parse("[Trash Info]\nPath=docs/x\n", "/media/usb", &path, &date));
    g_assert_cmpstr(path, ==, "/media/usb/docs/x");
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
    g_test_add_func("/trash_restore/trash_and_restore", test_trash_and_restore);
    return g_test_run();
}
