/* Unit tests for src/path_globs.c: path patterns. */
#include "path_globs.h"

static gboolean matches(const gchar* list, const gchar* rel) {
    PathGlobs* g = path_globs_new(list);
    gboolean hit = path_globs_match(g, rel);
    path_globs_free(g);
    return hit;
}

static void test_empty(void) {
    g_assert_null(path_globs_new(NULL));
    g_assert_null(path_globs_new(""));
    g_assert_null(path_globs_new(" , ,/ "));
    g_assert_false(path_globs_match(NULL, "a.c"));
}

static void test_names(void) {
    /* No slash: a file or folder name at any depth */
    g_assert_true(matches("*.c", "a.c"));
    g_assert_true(matches("*.c", "src/ui/a.c"));
    g_assert_false(matches("*.c", "a.h"));
    g_assert_true(matches("tests", "tests/ui/run.sh"));
    g_assert_true(matches("tests", "src/tests/x.c"));
    g_assert_false(matches("tests", "src/tests_x.c"));
    g_assert_true(matches("Makefile", "Makefile"));
    g_assert_true(matches("a?.c", "src/ab.c"));
    /* Several patterns, spaces around them */
    g_assert_true(matches(" *.h , *.c ", "x/y.c"));
    g_assert_true(matches(" *.h , *.c ", "y.h"));
    g_assert_false(matches(" *.h , *.c ", "y.md"));
}

static void test_anchored(void) {
    /* A slash: from the root, the path or one of its folders */
    g_assert_true(matches("src/ui", "src/ui/a.c"));
    g_assert_false(matches("src/ui", "lib/src/ui/a.c"));
    g_assert_true(matches("src/*.c", "src/a.c"));
    g_assert_true(matches("docs/user-guide/search.md", "docs/user-guide/search.md"));
    g_assert_true(matches("/build", "build/x.o"));
    g_assert_false(matches("/build", "src/build/x.o"));
    g_assert_true(matches("/README.md", "README.md"));
    g_assert_false(matches("/README.md", "docs/README.md"));
}

static void test_dir_only(void) {
    /* Trailing slash: folders only */
    g_assert_true(matches("build/", "build/x.o"));
    g_assert_true(matches("build/", "src/build/x.o"));
    g_assert_false(matches("build/", "build"));
    g_assert_false(matches("build/", "src/build"));
    g_assert_true(matches("src/", "src/a.c"));
    g_assert_true(matches("src/ui/", "src/ui/a.c"));
    g_assert_false(matches("src/ui/", "src/ui"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/path_globs/empty", test_empty);
    g_test_add_func("/path_globs/names", test_names);
    g_test_add_func("/path_globs/anchored", test_anchored);
    g_test_add_func("/path_globs/dir_only", test_dir_only);
    return g_test_run();
}
