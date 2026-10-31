/* Unit tests for src/path_kind.c: path kinds read in a worker thread and kept. */
#include "path_kind.h"

#include <glib/gstdio.h>

typedef struct {
    gint calls;
    PathKind old_kind;
    PathKind new_kind;
} Result;

static void on_done(const gchar* path, PathKind old_kind, PathKind new_kind, gpointer data) {
    (void)path;
    Result* r = data;
    r->calls++;
    r->old_kind = old_kind;
    r->new_kind = new_kind;
}

static void wait_for(Result* r, gint calls) {
    while (r->calls < calls) {
        g_main_context_iteration(NULL, TRUE);
    }
}

static void test_kinds(void) {
    path_kind_clear();
    gchar* dir = g_dir_make_tmp("path_kind_XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar* file = g_build_filename(dir, "a.txt", NULL);
    g_assert_true(g_file_set_contents(file, "x", -1, NULL));
    gchar* missing = g_build_filename(dir, "none", NULL);

    g_assert_cmpint(path_kind_lookup(dir), ==, PATH_KIND_UNKNOWN);
    Result r = { 0, 0, 0 };
    g_assert_true(path_kind_check(dir, on_done, &r));
    wait_for(&r, 1);
    g_assert_cmpint(r.old_kind, ==, PATH_KIND_UNKNOWN);
    g_assert_cmpint(r.new_kind, ==, PATH_KIND_DIR);
    g_assert_cmpint(path_kind_lookup(dir), ==, PATH_KIND_DIR);

    g_assert_true(path_kind_check(file, on_done, &r));
    wait_for(&r, 2);
    g_assert_cmpint(path_kind_lookup(file), ==, PATH_KIND_FILE);

    g_assert_true(path_kind_check(missing, on_done, &r));
    wait_for(&r, 3);
    g_assert_cmpint(path_kind_lookup(missing), ==, PATH_KIND_MISSING);

    /* A kind that changes is reported with the previous one */
    g_remove(file);
    g_assert_true(path_kind_check(file, on_done, &r));
    wait_for(&r, 4);
    g_assert_cmpint(r.old_kind, ==, PATH_KIND_FILE);
    g_assert_cmpint(r.new_kind, ==, PATH_KIND_MISSING);

    g_rmdir(dir);
    g_free(missing);
    g_free(file);
    g_free(dir);
}

static void test_one_check_per_path(void) {
    path_kind_clear();
    Result r = { 0, 0, 0 };
    g_assert_true(path_kind_check("/", on_done, &r));
    g_assert_false(path_kind_check("/", on_done, &r)); /* still running */
    wait_for(&r, 1);
    g_assert_cmpint(path_kind_lookup("/"), ==, PATH_KIND_DIR);
    g_assert_true(path_kind_check("/", on_done, &r)); /* ended: may run again */
    wait_for(&r, 2);
    g_assert_cmpint(r.calls, ==, 2);
    g_assert_false(path_kind_check("", on_done, &r));
    g_assert_false(path_kind_check(NULL, on_done, &r));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/path_kind/kinds", test_kinds);
    g_test_add_func("/path_kind/one_check_per_path", test_one_check_per_path);
    return g_test_run();
}
