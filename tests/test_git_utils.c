/* Unit tests for src/git_utils.c. Each test works in a fresh temporary directory;
 * the 'git status' test is skipped when git is not installed. */
#include "git_utils.h"

#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    gchar* dir; /* canonical temporary directory */
} Fixture;

static void fixture_setup(Fixture* fx, gconstpointer data) {
    (void)data;
    GError* err = NULL;
    gchar* tmp = g_dir_make_tmp("git_utils_test_XXXXXX", &err);
    g_assert_no_error(err);
    fx->dir = g_canonicalize_filename(tmp, NULL);
    g_free(tmp);
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
        if (d != NULL) {
            g_dir_close(d);
        }
    }
    return g_remove(path) == 0;
}

static void fixture_teardown(Fixture* fx, gconstpointer data) {
    (void)data;
    remove_tree(fx->dir);
    g_free(fx->dir);
}

/* Create dir/<rel> (and parents) with the given contents; NULL contents = directory */
static gchar* make_path(Fixture* fx, const gchar* rel, const gchar* contents) {
    gchar* path = g_build_filename(fx->dir, rel, NULL);
    if (contents == NULL) {
        g_assert_cmpint(g_mkdir_with_parents(path, 0755), ==, 0);
    } else {
        gchar* parent = g_path_get_dirname(path);
        g_assert_cmpint(g_mkdir_with_parents(parent, 0755), ==, 0);
        g_free(parent);
        g_assert_true(g_file_set_contents(path, contents, -1, NULL));
    }
    return path;
}

static void test_root_outside_repo(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* sub = make_path(fx, "plain/sub", NULL);
    /* The temporary directory itself may live inside a repository on some
     * machines: only check that no root is found below fx->dir. */
    gchar* root = find_git_root(sub);
    g_assert_true(root == NULL || !g_str_has_prefix(root, fx->dir));
    g_free(root);
    g_free(sub);
    g_assert_null(find_git_root(NULL));
    g_assert_null(find_git_root(""));
}

static void test_root_from_subdir(Fixture* fx, gconstpointer data) {
    (void)data;
    g_free(make_path(fx, "repo/.git", NULL));
    gchar* sub = make_path(fx, "repo/src/deep", NULL);
    gchar* expected = g_build_filename(fx->dir, "repo", NULL);
    gchar* root = find_git_root(sub);
    g_assert_cmpstr(root, ==, expected);
    g_free(root);

    /* Paths inside .git itself are not treated as part of the working tree */
    gchar* inside = g_build_filename(expected, ".git", "objects", NULL);
    g_assert_null(find_git_root(inside));
    g_free(inside);
    g_free(expected);
    g_free(sub);
}

static void test_resolve_dir(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* repo = make_path(fx, "repo", NULL);
    g_free(make_path(fx, "repo/.git", NULL));
    gchar* git_dir = git_resolve_dir(repo);
    gchar* expected = g_build_filename(repo, ".git", NULL);
    g_assert_cmpstr(git_dir, ==, expected);
    g_free(git_dir);
    g_free(expected);

    /* Worktree / submodule: ".git" file pointing elsewhere (relative path) */
    gchar* wt = make_path(fx, "wt", NULL);
    g_free(make_path(fx, "wt/.git", "gitdir: ../repo/.git/worktrees/wt\n"));
    git_dir = git_resolve_dir(wt);
    expected = g_build_filename(wt, "../repo/.git/worktrees/wt", NULL);
    g_assert_cmpstr(git_dir, ==, expected);
    g_free(git_dir);
    g_free(expected);

    g_assert_null(git_resolve_dir(NULL));
    g_free(wt);
    g_free(repo);
}

static void test_read_branch(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* head = make_path(fx, "repo/.git/HEAD", "ref: refs/heads/feature/x\n");
    gchar* repo = g_build_filename(fx->dir, "repo", NULL);
    gchar* branch = git_read_branch(repo);
    g_assert_cmpstr(branch, ==, "feature/x");
    g_free(branch);

    /* Detached HEAD: short SHA */
    g_assert_true(g_file_set_contents(head, "0123456789abcdef0123456789abcdef01234567\n", -1, NULL));
    branch = git_read_branch(repo);
    g_assert_cmpstr(branch, ==, "0123456");
    g_free(branch);
    g_free(repo);
    g_free(head);
}

static gboolean run_git(const gchar* dir, const gchar* const* args) {
    GPtrArray* argv = g_ptr_array_new();
    g_ptr_array_add(argv, "git");
    for (gint i = 0; args[i] != NULL; i++) {
        g_ptr_array_add(argv, (gpointer)args[i]);
    }
    g_ptr_array_add(argv, NULL);
    gint status = -1;
    gboolean ok = g_spawn_sync(dir, (gchar**)argv->pdata, NULL,
                               G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                                   G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, NULL, NULL, &status, NULL);
    g_ptr_array_free(argv, TRUE);
    return ok && g_spawn_check_wait_status(status, NULL);
}

static void test_scan_statuses(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* git = g_find_program_in_path("git");
    if (git == NULL) {
        g_test_skip("git not installed");
        return;
    }
    g_free(git);

    gchar* repo = make_path(fx, "repo", NULL);
    const gchar* init[] = {"init", "-q", NULL};
    g_assert_true(run_git(repo, init));
    gchar* file = make_path(fx, "repo/dir/new.txt", "hello\n");

    GHashTable* st = scan_git_statuses(repo);
    g_assert_nonnull(st);
    const gchar* badge = g_hash_table_lookup(st, file);
    g_assert_nonnull(badge);
    g_assert_nonnull(strstr(badge, ">U<")); /* untracked */
    g_hash_table_destroy(st);

    g_free(file);
    g_free(repo);
}

static void test_read_head_blob(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* git = g_find_program_in_path("git");
    if (git == NULL) {
        g_test_skip("git not installed");
        return;
    }
    g_free(git);

    gchar* repo = make_path(fx, "repo", NULL);
    const gchar* init[] = {"init", "-q", NULL};
    g_assert_true(run_git(repo, init));
    gchar* file = make_path(fx, "repo/dir/a.txt", "one\n");
    gsize len = 99;
    g_assert_null(git_read_head_blob(file, &len)); /* no commit yet */
    g_assert_cmpuint(len, ==, 0);

    const gchar* add[] = {"add", "dir/a.txt", NULL};
    const gchar* commit[] = {"-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "c", NULL};
    g_assert_true(run_git(repo, add));
    g_assert_true(run_git(repo, commit));
    g_assert_true(g_file_set_contents(file, "two\n", -1, NULL));

    gchar* blob = git_read_head_blob(file, &len);
    g_assert_cmpstr(blob, ==, "one\n"); /* committed version, not the file on disk */
    g_assert_cmpuint(len, ==, 4);
    g_free(blob);

    gchar* untracked = make_path(fx, "repo/dir/b.txt", "x\n");
    g_assert_null(git_read_head_blob(untracked, &len));
    g_free(untracked);

    gchar* outside = make_path(fx, "c.txt", "x\n");
    g_assert_null(git_read_head_blob(outside, &len));
    g_free(outside);

    g_free(file);
    g_free(repo);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add("/git_utils/root_outside_repo", Fixture, NULL, fixture_setup, test_root_outside_repo, fixture_teardown);
    g_test_add("/git_utils/root_from_subdir", Fixture, NULL, fixture_setup, test_root_from_subdir, fixture_teardown);
    g_test_add("/git_utils/resolve_dir", Fixture, NULL, fixture_setup, test_resolve_dir, fixture_teardown);
    g_test_add("/git_utils/read_branch", Fixture, NULL, fixture_setup, test_read_branch, fixture_teardown);
    g_test_add("/git_utils/scan_statuses", Fixture, NULL, fixture_setup, test_scan_statuses, fixture_teardown);
    g_test_add("/git_utils/read_head_blob", Fixture, NULL, fixture_setup, test_read_head_blob, fixture_teardown);
    return g_test_run();
}
