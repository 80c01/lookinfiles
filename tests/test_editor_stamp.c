/* Unit tests for src/editor/editor_stamp.c: metadata stamps and external change
 * detection (size, mtime, atomic replace, removal). */
#include "editor/editor_stamp.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <utime.h>

static void test_missing_and_invalid(void) {
    EditorStamp st;
    g_assert_false(editor_stamp_read(NULL, &st));
    g_assert_false(st.valid);
    g_assert_false(st.exists);

    g_assert_false(editor_stamp_read("", &st));
    g_assert_false(st.valid);

    gchar* dir = g_dir_make_tmp("editor-stamp-XXXXXX", NULL);
    gchar* missing = g_build_filename(dir, "nope.txt", NULL);
    EditorStamp s1, s2;
    g_assert_false(editor_stamp_read(missing, &s1));
    g_assert_true(s1.valid);
    g_assert_false(s1.exists);
    g_assert_false(editor_stamp_changed(missing, &s1, &s2));
    g_assert_true(editor_stamp_equal(&s1, &s2));

    /* Creating the file flips exists from FALSE to TRUE */
    g_assert_true(g_file_set_contents(missing, "hi", -1, NULL));
    g_assert_true(editor_stamp_changed(missing, &s1, &s2));
    g_assert_true(s2.exists);

    g_remove(missing);
    g_rmdir(dir);
    g_free(missing);
    g_free(dir);
}

static void test_size_mtime_and_removal(void) {
    gchar* dir = g_dir_make_tmp("editor-stamp-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "doc.txt", NULL);
    g_assert_true(g_file_set_contents(path, "hello", -1, NULL));

    EditorStamp base, cur;
    g_assert_true(editor_stamp_read(path, &base));
    g_assert_true(base.valid);
    g_assert_true(base.exists);
    g_assert_cmpint(base.file.size, ==, 5);
    g_assert_false(editor_stamp_changed(path, &base, &cur));
    g_assert_true(editor_stamp_equal(&base, &cur));

    /* Size change */
    g_assert_true(g_file_set_contents(path, "hello world", -1, NULL));
    g_assert_true(editor_stamp_changed(path, &base, &cur));
    g_assert_cmpint(cur.file.size, ==, 11);
    base = cur;

    /* Same size, explicit mtime bump */
    struct utimbuf tb;
    time_t base_sec = (time_t)(base.file.mtime_ns / G_GINT64_CONSTANT(1000000000));
    tb.actime = base_sec + 5;
    tb.modtime = base_sec + 5;
    g_assert_cmpint(utime(path, &tb), ==, 0);
    g_assert_true(editor_stamp_changed(path, &base, &cur));
    base = cur;
    g_assert_false(editor_stamp_changed(path, &base, NULL));

    /* Removal */
    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_true(editor_stamp_changed(path, &base, &cur));
    g_assert_true(cur.valid);
    g_assert_false(cur.exists);

    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

#ifndef G_OS_WIN32
static void test_atomic_replace_inode(void) {
    gchar* dir = g_dir_make_tmp("editor-stamp-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "a.txt", NULL);
    gchar* tmp = g_build_filename(dir, "a.tmp", NULL);
    g_assert_true(g_file_set_contents(path, "same!", -1, NULL));

    EditorStamp base, cur;
    g_assert_true(editor_stamp_read(path, &base));

    /* Write a brand new file of the exact same size and rename over target */
    g_assert_true(g_file_set_contents(tmp, "diff!", -1, NULL));
    g_assert_cmpint(g_rename(tmp, path), ==, 0);
    g_assert_true(editor_stamp_changed(path, &base, &cur));
    g_assert_cmpuint(cur.file.inode, !=, base.file.inode);

    g_remove(path);
    g_rmdir(dir);
    g_free(tmp);
    g_free(path);
    g_free(dir);
}
#endif

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/editor_stamp/missing_and_invalid", test_missing_and_invalid);
    g_test_add_func("/editor_stamp/size_mtime_and_removal", test_size_mtime_and_removal);
#ifndef G_OS_WIN32
    g_test_add_func("/editor_stamp/atomic_replace_inode", test_atomic_replace_inode);
#endif
    return g_test_run();
}
