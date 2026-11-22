/* Unit tests for git_remote: the state read from `git status` run elsewhere */
#include "git/git_remote.h"
#include "git/git_utils.h"

#include <string.h>

static void test_parse(void) {
    /* Run in /m/proj/src: the prefix "src/", so the root is /m/proj */
    static const gchar out[] = "src/\n\0"
                               "## main...origin/main [ahead 2, behind 1]\0"
                               " M src/a.c\0"
                               "?? notes/\0"
                               "R  src/new.c\0src/old.c\0"
                               "A  b.c\0"
                               "UU c.c\0";
    GitState* s = git_remote_parse("/m/proj/src", out, sizeof(out) - 1);
    g_assert_nonnull(s);
    g_assert_cmpstr(git_state_root(s), ==, "/m/proj");
    g_assert_cmpstr(git_state_branch(s), ==, "main");
    g_assert_true(git_state_has_upstream(s));
    g_assert_cmpuint(git_state_ahead(s), ==, 2);
    g_assert_cmpuint(git_state_behind(s), ==, 1);
    g_assert_true(git_state_has_changes(s));
    g_assert_nonnull(strstr(git_state_badge(s, "/m/proj/src/a.c"), ">M<"));
    g_assert_nonnull(strstr(git_state_badge(s, "/m/proj/src"), ">M<")); /* rolled up */
    g_assert_nonnull(strstr(git_state_badge(s, "/m/proj/notes"), ">U<"));
    g_assert_nonnull(strstr(git_state_badge(s, "/m/proj/src/new.c"), ">R<"));
    g_assert_null(git_state_badge(s, "/m/proj/src/old.c"));
    g_assert_nonnull(strstr(git_state_badge(s, "/m/proj/b.c"), ">A<"));
    g_assert_nonnull(strstr(git_state_badge(s, "/m/proj/c.c"), ">!<"));
    git_state_free(s);

    /* At the root, clean, no upstream */
    static const gchar clean[] = "\n\0## No commits yet on dev\0";
    s = git_remote_parse("/m/proj", clean, sizeof(clean) - 1);
    g_assert_cmpstr(git_state_root(s), ==, "/m/proj");
    g_assert_cmpstr(git_state_branch(s), ==, "dev");
    g_assert_false(git_state_has_upstream(s));
    g_assert_false(git_state_has_changes(s));
    git_state_free(s);

    static const gchar detached[] = "\n\0## HEAD (no branch)\0";
    s = git_remote_parse("/m/proj", detached, sizeof(detached) - 1);
    g_assert_cmpstr(git_state_branch(s), ==, "HEAD");
    git_state_free(s);

    /* Not an output of the command */
    g_assert_null(git_remote_parse("/m/proj", "fatal", 5));
    g_assert_null(git_remote_parse("/m", "a/b/c/\n\0", 8)); /* deeper than the path */
}

static void test_branches(void) {
    static const gchar out[] = "*\trefs/heads/main\n"
                               " \trefs/remotes/origin/HEAD\n"
                               " \trefs/remotes/origin/main\n"
                               " \trefs/heads/dev\n"
                               " \trefs/tags/v1\n";
    GPtrArray* b = git_remote_parse_branches(out, sizeof(out) - 1);
    g_assert_cmpuint(b->len, ==, 3);
    GitBranchInfo* i = b->pdata[0];
    g_assert_cmpstr(i->name, ==, "dev");
    g_assert_false(i->is_head);
    i = b->pdata[1];
    g_assert_cmpstr(i->name, ==, "main");
    g_assert_true(i->is_head);
    g_assert_false(i->is_remote);
    i = b->pdata[2];
    g_assert_cmpstr(i->name, ==, "origin/main");
    g_assert_true(i->is_remote);
    g_ptr_array_unref(b);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_remote/parse", test_parse);
    g_test_add_func("/git_remote/branches", test_branches);
    return g_test_run();
}
