/* Unit tests for src/git_state.c: badges and upstream counts of a git scan. */
#include "git_state.h"

static GHashTable* badges(const gchar* path, const gchar* badge) {
    GHashTable* map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (path != NULL) {
        g_hash_table_insert(map, g_strdup(path), g_strdup(badge));
    }
    return map;
}

static void test_null(void) {
    g_assert_null(git_state_badges(NULL));
    g_assert_null(git_state_badge(NULL, "/a"));
    g_assert_null(git_state_root(NULL));
    g_assert_false(git_state_has_changes(NULL));
    g_assert_false(git_state_has_upstream(NULL));
    g_assert_cmpuint(git_state_ahead(NULL), ==, 0);
    g_assert_true(git_state_same_badges(NULL, NULL));
    g_assert_true(git_state_same_counts(NULL, NULL));
    git_state_free(NULL);
}

static void test_values(void) {
    GitState* s = git_state_new(badges("/r/a.c", "M"), "/r", TRUE, 2, 1);
    g_assert_cmpstr(git_state_badge(s, "/r/a.c"), ==, "M");
    g_assert_null(git_state_badge(s, "/r/b.c"));
    g_assert_cmpstr(git_state_root(s), ==, "/r");
    g_assert_true(git_state_has_changes(s));
    g_assert_true(git_state_has_upstream(s));
    g_assert_cmpuint(git_state_ahead(s), ==, 2);
    g_assert_cmpuint(git_state_behind(s), ==, 1);
    git_state_free(s);

    /* No upstream: no counts; an empty map: no changes */
    s = git_state_new(badges(NULL, NULL), "/r", FALSE, 3, 4);
    g_assert_false(git_state_has_changes(s));
    g_assert_cmpuint(git_state_ahead(s), ==, 0);
    g_assert_cmpuint(git_state_behind(s), ==, 0);
    git_state_free(s);
}

static void test_compare(void) {
    GitState* a = git_state_new(badges("/r/a.c", "M"), "/r", TRUE, 1, 0);
    GitState* b = git_state_new(badges("/r/a.c", "M"), "/other", TRUE, 1, 0);
    GitState* c = git_state_new(badges("/r/a.c", "A"), "/r", TRUE, 0, 0);
    GitState* empty = git_state_new(NULL, "/r", FALSE, 0, 0);
    g_assert_true(git_state_same_badges(a, b));
    g_assert_true(git_state_same_counts(a, b));
    g_assert_false(git_state_same_badges(a, c));
    g_assert_false(git_state_same_counts(a, c));
    g_assert_false(git_state_same_badges(a, NULL));
    g_assert_true(git_state_same_badges(empty, NULL));
    g_assert_true(git_state_same_counts(empty, NULL));
    git_state_free(a);
    git_state_free(b);
    git_state_free(c);
    git_state_free(empty);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_state/null", test_null);
    g_test_add_func("/git_state/values", test_values);
    g_test_add_func("/git_state/compare", test_compare);
    return g_test_run();
}
