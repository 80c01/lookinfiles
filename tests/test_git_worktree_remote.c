/* Unit tests for git_worktree_remote: the changes read from git's output */
#include "git/git_worktree_remote.h"

#include <string.h>

static void test_parse(void) {
    static const gchar out[] = "MM src/a.c\0" "R  new.c\0old.c\0" "?? notes.txt\0" "UU c.c\0" " D gone.c\0" "!! build/x\0"
                               "\001\0"
                               "3\t1\tsrc/a.c\0" "0\t0\t\0old.c\0new.c\0"
                               "\001\0"
                               "2\t2\tsrc/a.c\0" "-\t-\tc.c\0" "0\t5\tgone.c\0"
                               "\001\0"
                               "notes.txt\0" "12\n";
    gboolean more = TRUE;
    GPtrArray* c = git_remote_parse_changes(out, sizeof(out) - 1, 0, &more);
    g_assert_false(more);
    g_assert_cmpuint(c->len, ==, 6);
    GitChange* x = c->pdata[0];
    g_assert_cmpint(x->group, ==, GIT_CHANGE_STAGED);
    g_assert_cmpstr(x->path, ==, "new.c");
    g_assert_cmpstr(x->old_path, ==, "old.c");
    g_assert_cmpint(x->status, ==, 'R');
    x = c->pdata[1];
    g_assert_cmpstr(x->path, ==, "src/a.c");
    g_assert_cmpint(x->group, ==, GIT_CHANGE_STAGED);
    g_assert_cmpint(x->added, ==, 3);
    g_assert_cmpint(x->removed, ==, 1);
    x = c->pdata[2];
    g_assert_cmpstr(x->path, ==, "c.c");
    g_assert_cmpint(x->status, ==, '!');
    g_assert_cmpint(x->added, ==, -1); /* binary */
    x = c->pdata[3];
    g_assert_cmpstr(x->path, ==, "gone.c");
    g_assert_cmpint(x->status, ==, 'D');
    g_assert_cmpint(x->removed, ==, 5);
    x = c->pdata[4];
    g_assert_cmpstr(x->path, ==, "src/a.c");
    g_assert_cmpint(x->group, ==, GIT_CHANGE_UNSTAGED);
    g_assert_cmpint(x->added, ==, 2);
    x = c->pdata[5];
    g_assert_cmpint(x->group, ==, GIT_CHANGE_UNTRACKED);
    g_assert_cmpstr(x->path, ==, "notes.txt");
    g_assert_cmpint(x->added, ==, 12);
    g_ptr_array_unref(c);

    c = git_remote_parse_changes(out, sizeof(out) - 1, 2, &more);
    g_assert_cmpuint(c->len, ==, 2);
    g_assert_true(more);
    g_ptr_array_unref(c);
}

static void test_patch_of(void) {
    static const gchar diff[] = "diff --git a/x.c b/x.c\n@@ -1 +1 @@\n-a\n+b\n"
                                "diff --git a/old.c b/new.c\nsimilarity index 90%\n";
    gchar* p = git_remote_patch_of(diff, "x.c");
    g_assert_cmpstr(p, ==, "diff --git a/x.c b/x.c\n@@ -1 +1 @@\n-a\n+b\n");
    g_free(p);
    p = git_remote_patch_of(diff, "new.c");
    g_assert_true(g_str_has_prefix(p, "diff --git a/old.c b/new.c\n"));
    g_free(p);
    p = git_remote_patch_of(diff, "none.c");
    g_assert_cmpstr(p, ==, "");
    g_free(p);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_worktree_remote/parse", test_parse);
    g_test_add_func("/git_worktree_remote/patch_of", test_patch_of);
    return g_test_run();
}
