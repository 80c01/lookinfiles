/* Unit tests for src/editor/file_save.c: in-place saves keep links, modes and
 * read-only files. */
#include "editor/file_save.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

static gchar* read_all(const gchar* path) {
    gchar* text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    return text;
}

static void test_new_and_existing(void) {
    gchar* dir = g_dir_make_tmp("file-save-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "a.txt", NULL);
    g_assert_true(file_save_text(path, "one", -1, NULL));
    gchar* text = read_all(path);
    g_assert_cmpstr(text, ==, "one");
    g_free(text);
    g_assert_true(file_save_text(path, "two\r\nlines", 5, NULL));
    text = read_all(path);
    g_assert_cmpstr(text, ==, "two\r\n");
    g_free(text);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

#ifndef G_OS_WIN32
static void test_mode_kept(void) {
    gchar* dir = g_dir_make_tmp("file-save-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "run.sh", NULL);
    g_assert_true(g_file_set_contents(path, "#!/bin/sh\n", -1, NULL));
    g_assert_cmpint(g_chmod(path, 0750), ==, 0);
    g_assert_true(file_save_text(path, "#!/bin/sh\necho hi\n", -1, NULL));
    GStatBuf st;
    g_assert_cmpint(g_stat(path, &st), ==, 0);
    g_assert_cmpint(st.st_mode & 0777, ==, 0750);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_symlink_kept(void) {
    gchar* dir = g_dir_make_tmp("file-save-XXXXXX", NULL);
    gchar* real = g_build_filename(dir, "real.txt", NULL);
    gchar* link = g_build_filename(dir, "link.txt", NULL);
    g_assert_true(g_file_set_contents(real, "old", -1, NULL));
    g_assert_cmpint(symlink("real.txt", link), ==, 0); /* relative target */
    g_assert_true(file_save_text(link, "new", -1, NULL));
    gchar* target = g_file_read_link(link, NULL);
    g_assert_cmpstr(target, ==, "real.txt"); /* still a link */
    g_free(target);
    gchar* text = read_all(real);
    g_assert_cmpstr(text, ==, "new");
    g_free(text);
    g_remove(link);
    g_remove(real);
    g_rmdir(dir);
    g_free(link);
    g_free(real);
    g_free(dir);
}

static void test_read_only_refused(void) {
    if (geteuid() == 0) {
        g_test_skip("root writes read-only files");
        return;
    }
    gchar* dir = g_dir_make_tmp("file-save-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "ro.txt", NULL);
    g_assert_true(g_file_set_contents(path, "keep", -1, NULL));
    g_assert_cmpint(g_chmod(path, 0444), ==, 0);
    GError* err = NULL;
    g_assert_false(file_save_text(path, "lost", -1, &err));
    g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_ACCES);
    g_clear_error(&err);
    gchar* text = read_all(path);
    g_assert_cmpstr(text, ==, "keep");
    g_free(text);
    g_chmod(path, 0644);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}
#endif

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/file_save/new_and_existing", test_new_and_existing);
#ifndef G_OS_WIN32
    g_test_add_func("/file_save/mode_kept", test_mode_kept);
    g_test_add_func("/file_save/symlink_kept", test_symlink_kept);
    g_test_add_func("/file_save/read_only_refused", test_read_only_refused);
#endif
    return g_test_run();
}
