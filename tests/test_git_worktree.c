/* Unit tests for src/git/git_worktree.c. Each test works in a fresh
 * temporary directory; repositories are made with libgit2 (no git program
 * needed). */
#include "git/git_worktree.h"

#include <git2.h>
#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    gchar* dir;            /* the work tree */
    git_repository* repo;
} Fixture;

static void put(Fixture* fx, const gchar* rel, const gchar* text) {
    gchar* full = g_build_filename(fx->dir, rel, NULL);
    gchar* parent = g_path_get_dirname(full);
    g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
    g_assert_true(g_file_set_contents(full, text, -1, NULL));
    g_free(parent);
    g_free(full);
}

static void drop(Fixture* fx, const gchar* rel) {
    gchar* full = g_build_filename(fx->dir, rel, NULL);
    g_assert_cmpint(g_remove(full), ==, 0);
    g_free(full);
}

static void stage(Fixture* fx, const gchar* rel) {
    git_index* index = NULL;
    g_assert_cmpint(git_repository_index(&index, fx->repo), ==, 0);
    g_assert_cmpint(git_index_add_bypath(index, rel), ==, 0);
    g_assert_cmpint(git_index_write(index), ==, 0);
    git_index_free(index);
}

static void stage_removal(Fixture* fx, const gchar* rel) {
    git_index* index = NULL;
    g_assert_cmpint(git_repository_index(&index, fx->repo), ==, 0);
    g_assert_cmpint(git_index_remove_bypath(index, rel), ==, 0);
    g_assert_cmpint(git_index_write(index), ==, 0);
    git_index_free(index);
}

/* Commit the index on HEAD */
static void commit(Fixture* fx) {
    git_index* index = NULL;
    git_oid tree_id;
    git_oid commit_id;
    git_tree* tree = NULL;
    git_signature* sig = NULL;
    git_object* parent = NULL;
    g_assert_cmpint(git_repository_index(&index, fx->repo), ==, 0);
    g_assert_cmpint(git_index_write_tree(&tree_id, index), ==, 0);
    g_assert_cmpint(git_tree_lookup(&tree, fx->repo, &tree_id), ==, 0);
    g_assert_cmpint(git_signature_new(&sig, "Test", "test@example.com", 1700000000, 0), ==, 0);
    gint n = git_revparse_single(&parent, fx->repo, "HEAD") == 0 ? 1 : 0;
    const git_commit* parents[1] = { (const git_commit*)parent };
    g_assert_cmpint(git_commit_create(&commit_id, fx->repo, "HEAD", sig, sig, NULL, "commit", tree, (size_t)n,
                                      parents), ==, 0);
    git_object_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
}

static gboolean remove_tree(const gchar* path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        GDir* d = g_dir_open(path, 0, NULL);
        const gchar* name;
        while (d != NULL && (name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        if (d != NULL) g_dir_close(d);
    }
    return g_remove(path) == 0;
}

static void fixture_setup(Fixture* fx, gconstpointer data) {
    (void)data;
    git_libgit2_init();
    gchar* tmp = g_dir_make_tmp("git_worktree_test_XXXXXX", NULL);
    g_assert_nonnull(tmp);
    fx->dir = g_canonicalize_filename(tmp, NULL);
    g_free(tmp);
    g_assert_cmpint(git_repository_init(&fx->repo, fx->dir, 0), ==, 0);
}

static void fixture_teardown(Fixture* fx, gconstpointer data) {
    (void)data;
    git_repository_free(fx->repo);
    remove_tree(fx->dir);
    g_free(fx->dir);
    git_libgit2_shutdown();
}

/* The change of path in group, NULL if there is none */
static const GitChange* find(GPtrArray* changes, const gchar* path, GitChangeGroup group) {
    for (guint i = 0; i < changes->len; i++) {
        const GitChange* c = g_ptr_array_index(changes, i);
        if (c->group == group && g_strcmp0(c->path, path) == 0) return c;
    }
    return NULL;
}

/* Staged, unstaged and untracked changes, in that order, with their counts */
static void test_groups(Fixture* fx, gconstpointer data) {
    (void)data;
    put(fx, "a.txt", "one\ntwo\nthree\n");
    put(fx, "gone.txt", "bye\n");
    put(fx, "both.txt", "1\n");
    stage(fx, "a.txt");
    stage(fx, "gone.txt");
    stage(fx, "both.txt");
    commit(fx);

    put(fx, "a.txt", "one\nTWO\nthree\nfour\n"); /* unstaged: 2 added, 1 removed */
    put(fx, "new.c", "int x;\n");                 /* staged addition */
    stage(fx, "new.c");
    drop(fx, "gone.txt");                         /* unstaged deletion */
    put(fx, "both.txt", "1\n2\n");                /* staged, then changed again */
    stage(fx, "both.txt");
    put(fx, "both.txt", "1\n2\n3\n");
    put(fx, "dir/u.txt", "x\ny\n");               /* untracked, in a new folder */

    gboolean more = TRUE;
    GPtrArray* changes = git_read_changes(fx->dir, 0, NULL, &more);
    g_assert_nonnull(changes);
    g_assert_false(more);
    g_assert_cmpuint(changes->len, ==, 6);

    const GitChange* c = find(changes, "new.c", GIT_CHANGE_STAGED);
    g_assert_nonnull(c);
    g_assert_cmpint(c->status, ==, 'A');
    g_assert_cmpint(c->added, ==, 1);
    g_assert_cmpint(c->removed, ==, 0);
    c = find(changes, "both.txt", GIT_CHANGE_STAGED);
    g_assert_nonnull(c);
    g_assert_cmpint(c->added, ==, 1);
    c = find(changes, "both.txt", GIT_CHANGE_UNSTAGED);
    g_assert_nonnull(c);
    g_assert_cmpint(c->status, ==, 'M');
    g_assert_cmpint(c->added, ==, 1);
    c = find(changes, "a.txt", GIT_CHANGE_UNSTAGED);
    g_assert_nonnull(c);
    g_assert_cmpint(c->added, ==, 2);
    g_assert_cmpint(c->removed, ==, 1);
    c = find(changes, "gone.txt", GIT_CHANGE_UNSTAGED);
    g_assert_nonnull(c);
    g_assert_cmpint(c->status, ==, 'D');
    g_assert_cmpint(c->removed, ==, 1);
    c = find(changes, "dir/u.txt", GIT_CHANGE_UNTRACKED);
    g_assert_nonnull(c);
    g_assert_cmpint(c->status, ==, 'U');
    g_assert_cmpint(c->added, ==, 2);

    /* Groups in order */
    for (guint i = 1; i < changes->len; i++) {
        const GitChange* prev = g_ptr_array_index(changes, i - 1);
        const GitChange* cur = g_ptr_array_index(changes, i);
        g_assert_cmpint(prev->group, <=, cur->group);
    }
    g_ptr_array_unref(changes);
}

/* A rename staged is one change, with its old name */
static void test_rename(Fixture* fx, gconstpointer data) {
    (void)data;
    put(fx, "old.txt", "a\nb\nc\nd\ne\nf\n");
    stage(fx, "old.txt");
    commit(fx);
    drop(fx, "old.txt");
    put(fx, "renamed.txt", "a\nb\nc\nd\ne\nf\n");
    stage_removal(fx, "old.txt");
    stage(fx, "renamed.txt");

    GPtrArray* changes = git_read_changes(fx->dir, 0, NULL, NULL);
    g_assert_nonnull(changes);
    g_assert_cmpuint(changes->len, ==, 1);
    const GitChange* c = g_ptr_array_index(changes, 0);
    g_assert_cmpint(c->status, ==, 'R');
    g_assert_cmpstr(c->path, ==, "renamed.txt");
    g_assert_cmpstr(c->old_path, ==, "old.txt");
    g_ptr_array_unref(changes);

    gchar* patch = git_change_patch(fx->dir, "renamed.txt", GIT_CHANGE_STAGED);
    g_assert_nonnull(patch);
    g_assert_nonnull(strstr(patch, "rename from old.txt"));
    g_free(patch);
}

/* The patch of each group */
static void test_patch(Fixture* fx, gconstpointer data) {
    (void)data;
    put(fx, "f.txt", "old\n");
    stage(fx, "f.txt");
    commit(fx);
    put(fx, "f.txt", "staged\n");
    stage(fx, "f.txt");
    put(fx, "f.txt", "work\n");
    put(fx, "n.txt", "fresh\n");

    gchar* staged = git_change_patch(fx->dir, "f.txt", GIT_CHANGE_STAGED);
    g_assert_nonnull(strstr(staged, "diff --git a/f.txt b/f.txt"));
    g_assert_nonnull(strstr(staged, "\n-old\n"));
    g_assert_nonnull(strstr(staged, "\n+staged\n"));
    g_free(staged);

    gchar* work = git_change_patch(fx->dir, "f.txt", GIT_CHANGE_UNSTAGED);
    g_assert_nonnull(strstr(work, "\n-staged\n"));
    g_assert_nonnull(strstr(work, "\n+work\n"));
    g_free(work);

    gchar* fresh = git_change_patch(fx->dir, "n.txt", GIT_CHANGE_UNTRACKED);
    g_assert_nonnull(strstr(fresh, "\n+fresh\n"));
    g_free(fresh);

    gchar* none = git_change_patch(fx->dir, "missing.txt", GIT_CHANGE_UNSTAGED);
    g_assert_cmpstr(none, ==, "");
    g_free(none);
}

/* Before the first commit, the files added are staged additions */
static void test_unborn(Fixture* fx, gconstpointer data) {
    (void)data;
    put(fx, "first.txt", "1\n2\n");
    stage(fx, "first.txt");
    GPtrArray* changes = git_read_changes(fx->dir, 0, NULL, NULL);
    g_assert_nonnull(changes);
    g_assert_cmpuint(changes->len, ==, 1);
    const GitChange* c = g_ptr_array_index(changes, 0);
    g_assert_cmpint(c->group, ==, GIT_CHANGE_STAGED);
    g_assert_cmpint(c->status, ==, 'A');
    g_assert_cmpint(c->added, ==, 2);
    g_ptr_array_unref(changes);
}

/* At most max changes, the rest said to be left out */
static void test_max(Fixture* fx, gconstpointer data) {
    (void)data;
    put(fx, "a", "a\n");
    put(fx, "b", "b\n");
    put(fx, "c", "c\n");
    gboolean more = FALSE;
    GPtrArray* changes = git_read_changes(fx->dir, 2, NULL, &more);
    g_assert_nonnull(changes);
    g_assert_cmpuint(changes->len, ==, 2);
    g_assert_true(more);
    g_ptr_array_unref(changes);
}

/* A binary file has no line counts */
static void test_binary(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* full = g_build_filename(fx->dir, "b.bin", NULL);
    const gchar bytes[] = { 'a', 0, 'b', 0, 'c' };
    g_assert_true(g_file_set_contents(full, bytes, sizeof bytes, NULL));
    g_free(full);
    GPtrArray* changes = git_read_changes(fx->dir, 0, NULL, NULL);
    g_assert_nonnull(changes);
    g_assert_cmpuint(changes->len, ==, 1);
    const GitChange* c = g_ptr_array_index(changes, 0);
    g_assert_cmpint(c->added, ==, -1);
    g_assert_cmpint(c->removed, ==, -1);
    g_ptr_array_unref(changes);
}

static void test_not_a_repo(void) {
    gchar* tmp = g_dir_make_tmp("git_worktree_none_XXXXXX", NULL);
    g_assert_null(git_read_changes(tmp, 0, NULL, NULL));
    g_assert_null(git_change_patch(tmp, "x", GIT_CHANGE_UNSTAGED));
    g_assert_null(git_read_changes(NULL, 0, NULL, NULL));
    g_rmdir(tmp);
    g_free(tmp);
}

static void test_cancelled(Fixture* fx, gconstpointer data) {
    (void)data;
    put(fx, "a", "a\n");
    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_assert_null(git_read_changes(fx->dir, 0, cancel, NULL));
    g_object_unref(cancel);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add("/git_worktree/groups", Fixture, NULL, fixture_setup, test_groups, fixture_teardown);
    g_test_add("/git_worktree/rename", Fixture, NULL, fixture_setup, test_rename, fixture_teardown);
    g_test_add("/git_worktree/patch", Fixture, NULL, fixture_setup, test_patch, fixture_teardown);
    g_test_add("/git_worktree/unborn", Fixture, NULL, fixture_setup, test_unborn, fixture_teardown);
    g_test_add("/git_worktree/max", Fixture, NULL, fixture_setup, test_max, fixture_teardown);
    g_test_add("/git_worktree/binary", Fixture, NULL, fixture_setup, test_binary, fixture_teardown);
    g_test_add("/git_worktree/cancelled", Fixture, NULL, fixture_setup, test_cancelled, fixture_teardown);
    g_test_add_func("/git_worktree/not_a_repo", test_not_a_repo);
    return g_test_run();
}
