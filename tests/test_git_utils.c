/* Unit tests for src/git_utils.c. Each test works in a fresh temporary
 * directory; repositories are made with libgit2 (no git program needed). */
#include "git_utils.h"

#include <git2.h>
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

/* A new repository with a work tree at dir */
static git_repository* repo_init(const gchar* dir) {
    git_repository* repo = NULL;
    g_assert_cmpint(git_repository_init(&repo, dir, 0), ==, 0);
    return repo;
}

/* Stage rel (relative to the work tree) */
static void repo_add(git_repository* repo, const gchar* rel) {
    git_index* index = NULL;
    g_assert_cmpint(git_repository_index(&index, repo), ==, 0);
    g_assert_cmpint(git_index_add_bypath(index, rel), ==, 0);
    g_assert_cmpint(git_index_write(index), ==, 0);
    git_index_free(index);
}

/* Commit the index on HEAD, made seconds_ago before now */
static void repo_commit(git_repository* repo, const gchar* message, gint64 seconds_ago) {
    git_index* index = NULL;
    git_oid tree_id, commit_id, parent_id;
    git_tree* tree = NULL;
    git_commit* parent = NULL;
    git_signature* sig = NULL;
    g_assert_cmpint(git_repository_index(&index, repo), ==, 0);
    g_assert_cmpint(git_index_write_tree(&tree_id, index), ==, 0);
    git_index_free(index);
    g_assert_cmpint(git_tree_lookup(&tree, repo, &tree_id), ==, 0);
    gint64 when = g_get_real_time() / G_USEC_PER_SEC - seconds_ago;
    g_assert_cmpint(git_signature_new(&sig, "Ada", "ada@example.org", (git_time_t)when, 60), ==, 0);
    if (git_reference_name_to_id(&parent_id, repo, "HEAD") == 0) {
        g_assert_cmpint(git_commit_lookup(&parent, repo, &parent_id), ==, 0);
    }
    const git_commit* parents[] = {parent};
    g_assert_cmpint(git_commit_create(&commit_id, repo, "HEAD", sig, sig, NULL, message, tree,
                                      parent != NULL ? 1 : 0, parents),
                    ==, 0);
    git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
}

static void test_read_ahead_behind(Fixture* fx, gconstpointer data) {
    (void)data;
    guint ahead = 99, behind = 99;

    /* Outside repo or NULL */
    g_assert_false(git_read_ahead_behind(NULL, &ahead, &behind));
    g_assert_cmpuint(ahead, ==, 0);
    g_assert_cmpuint(behind, ==, 0);

    gchar* plain = make_path(fx, "plain", NULL);
    g_assert_false(git_read_ahead_behind(plain, &ahead, &behind));
    g_free(plain);

    /* Repo without upstream */
    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);
    g_free(make_path(fx, "repo/file.txt", "initial\n"));
    repo_add(repo, "file.txt");
    repo_commit(repo, "commit 1", 100);

    g_assert_false(git_read_ahead_behind(repo_dir, &ahead, &behind));
    g_assert_cmpuint(ahead, ==, 0);
    g_assert_cmpuint(behind, ==, 0);

    /* Set up an upstream tracking branch */
    git_reference* head_ref = NULL;
    g_assert_cmpint(git_repository_head(&head_ref, repo), ==, 0);
    const git_oid* head_oid = git_reference_target(head_ref);

    git_remote* remote = NULL;
    g_assert_cmpint(git_remote_create(&remote, repo, "origin", "https://example.invalid/repo.git"), ==, 0);
    git_remote_free(remote);

    git_reference* upstream_ref = NULL;
    g_assert_cmpint(git_reference_create(&upstream_ref, repo, "refs/remotes/origin/main", head_oid, 1, "create remote ref"), ==, 0);
    git_reference_free(upstream_ref);

    g_assert_cmpint(git_branch_set_upstream(head_ref, "origin/main"), ==, 0);
    git_reference_free(head_ref);

    /* Clean and in sync with upstream */
    g_assert_true(git_read_ahead_behind(repo_dir, &ahead, &behind));
    g_assert_cmpuint(ahead, ==, 0);
    g_assert_cmpuint(behind, ==, 0);

    /* One commit ahead */
    g_free(make_path(fx, "repo/file.txt", "second\n"));
    repo_add(repo, "file.txt");
    repo_commit(repo, "commit 2", 50);

    g_assert_true(git_read_ahead_behind(repo_dir, &ahead, &behind));
    g_assert_cmpuint(ahead, ==, 1);
    g_assert_cmpuint(behind, ==, 0);

    /* Also works when given a subdirectory of the repo */
    gchar* sub = make_path(fx, "repo/subdir", NULL);
    g_assert_true(git_read_ahead_behind(sub, &ahead, &behind));
    g_assert_cmpuint(ahead, ==, 1);
    g_assert_cmpuint(behind, ==, 0);
    g_free(sub);

    git_repository_free(repo);
    g_free(repo_dir);
}


static void test_scan_statuses(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);
    g_free(make_path(fx, "repo/kept.txt", "a\n"));
    g_free(make_path(fx, "repo/staged.txt", "b\n"));
    repo_add(repo, "kept.txt");
    repo_commit(repo, "first", 60);
    repo_add(repo, "staged.txt");
    gchar* file = make_path(fx, "repo/dir/new.txt", "hello\n");
    gchar* kept = make_path(fx, "repo/kept.txt", "changed\n");

    GHashTable* st = scan_git_statuses(repo_dir);
    g_assert_nonnull(st);
    const gchar* badge = g_hash_table_lookup(st, file);
    g_assert_nonnull(badge);
    g_assert_nonnull(strstr(badge, ">U<")); /* untracked */
    gchar* dir = g_build_filename(repo_dir, "dir", NULL);
    g_assert_nonnull(strstr(g_hash_table_lookup(st, dir), ">U<")); /* rolled up */
    g_assert_nonnull(strstr(g_hash_table_lookup(st, kept), ">M<"));
    gchar* staged = g_build_filename(repo_dir, "staged.txt", NULL);
    g_assert_nonnull(strstr(g_hash_table_lookup(st, staged), ">A<"));
    g_hash_table_destroy(st);

    g_free(staged);
    g_free(dir);
    g_free(kept);
    g_free(file);
    git_repository_free(repo);
    g_free(repo_dir);
}

static void test_read_head_blob(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);
    gchar* file = make_path(fx, "repo/dir/a.txt", "one\n");
    gsize len = 99;
    g_assert_null(git_read_head_blob(file, &len)); /* no commit yet */
    g_assert_cmpuint(len, ==, 0);

    repo_add(repo, "dir/a.txt");
    repo_commit(repo, "c", 0);
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
    git_repository_free(repo);
    g_free(repo_dir);
}

static void test_read_log(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);

    GPtrArray* log = git_read_log(repo_dir, 10);
    g_assert_nonnull(log);
    g_assert_cmpuint(log->len, ==, 0); /* no commit yet */
    g_ptr_array_unref(log);

    g_free(make_path(fx, "repo/a.txt", "1\n"));
    repo_add(repo, "a.txt");
    repo_commit(repo, "First commit\n\nBody", 3 * 86400);
    g_free(make_path(fx, "repo/a.txt", "2\n"));
    repo_add(repo, "a.txt");
    repo_commit(repo, "Second\ncommit", 120);

    log = git_read_log(repo_dir, 10);
    g_assert_cmpuint(log->len, ==, 2);
    GitCommitInfo* newest = log->pdata[0];
    g_assert_cmpstr(newest->subject, ==, "Second commit"); /* first paragraph on one line */
    g_assert_cmpstr(newest->author, ==, "Ada");
    g_assert_cmpstr(newest->date, ==, "2 minutes ago");
    g_assert_cmpuint(strlen(newest->hash), ==, 40);
    g_assert_true(g_str_has_prefix(newest->hash, newest->short_hash));
    GitCommitInfo* oldest = log->pdata[1];
    g_assert_cmpstr(oldest->subject, ==, "First commit");
    g_assert_cmpstr(oldest->date, ==, "3 days ago");
    g_ptr_array_unref(log);

    log = git_read_log(repo_dir, 1);
    g_assert_cmpuint(log->len, ==, 1);
    g_ptr_array_unref(log);

    gchar* plain = make_path(fx, "plain", NULL);
    g_assert_null(git_read_log(plain, 10));
    g_free(plain);
    git_repository_free(repo);
    g_free(repo_dir);
}

static void test_show_commit(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);
    g_free(make_path(fx, "repo/a.txt", "one\ntwo\n"));
    repo_add(repo, "a.txt");
    repo_commit(repo, "Add a", 0);
    g_free(make_path(fx, "repo/a.txt", "one\nthree\n"));
    repo_add(repo, "a.txt");
    repo_commit(repo, "Change a\n\nWhy it changed.", 0);

    GPtrArray* log = git_read_log(repo_dir, 1);
    const gchar* hash = ((GitCommitInfo*)log->pdata[0])->hash;
    gchar* text = git_show_commit(repo_dir, hash);
    g_assert_nonnull(text);
    gchar* first = g_strdup_printf("commit %s\nAuthor: Ada <ada@example.org>\nDate:   ", hash);
    g_assert_true(g_str_has_prefix(text, first));
    g_free(first);
    g_assert_nonnull(strstr(text, " +0100\n\n    Change a\n    \n    Why it changed.\n---\n"));
    g_assert_nonnull(strstr(text, " a.txt | 2 +-\n 1 file changed, 1 insertion(+), 1 deletion(-)\n\n"));
    g_assert_nonnull(strstr(text, "diff --git a/a.txt b/a.txt\n"));
    g_assert_nonnull(strstr(text, "\n-two\n+three\n"));
    g_free(text);

    /* Root commit: everything added */
    text = git_show_commit(repo_dir, "HEAD~1");
    g_assert_nonnull(strstr(text, " 1 file changed, 2 insertions(+)\n"));
    g_free(text);

    g_assert_null(git_show_commit(repo_dir, "0000000000000000000000000000000000000000"));
    g_ptr_array_unref(log);
    git_repository_free(repo);
    g_free(repo_dir);
}

static gboolean collect(const gchar* rel, gpointer data) {
    g_ptr_array_add(data, g_strdup(rel));
    return TRUE;
}

static gboolean stop_at_first(const gchar* rel, gpointer data) {
    (void)rel;
    (*(guint*)data)++;
    return FALSE;
}

static gboolean has(GPtrArray* files, const gchar* rel) {
    for (guint i = 0; i < files->len; i++) {
        if (strcmp(files->pdata[i], rel) == 0) return TRUE;
    }
    return FALSE;
}

static void test_list_files(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);
    g_free(make_path(fx, "repo/.gitignore", "*.o\n"));
    g_free(make_path(fx, "repo/src/main.c", "int main;\n"));
    repo_add(repo, ".gitignore");
    repo_add(repo, "src/main.c");
    repo_commit(repo, "c", 0);
    g_free(make_path(fx, "repo/src/util.c", "\n"));  /* untracked */
    g_free(make_path(fx, "repo/src/main.o", "\n"));  /* ignored */

    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    g_assert_true(git_list_files(repo_dir, FALSE, collect, files));
    g_assert_cmpuint(files->len, ==, 3);
    g_assert_true(has(files, ".gitignore") && has(files, "src/main.c") && has(files, "src/util.c"));
    g_ptr_array_set_size(files, 0);

    g_assert_true(git_list_files(repo_dir, TRUE, collect, files));
    g_assert_cmpuint(files->len, ==, 4);
    g_assert_true(has(files, "src/main.o"));
    g_ptr_array_unref(files);

    guint calls = 0;
    g_assert_true(git_list_files(repo_dir, FALSE, stop_at_first, &calls));
    g_assert_cmpuint(calls, ==, 1);

    gchar* sub = g_build_filename(repo_dir, "src", NULL);
    g_assert_false(git_list_files(sub, FALSE, collect, NULL)); /* not the top of the work tree */
    g_free(sub);
    git_repository_free(repo);
    g_free(repo_dir);
}

static void test_relative_date(void) {
    static const struct {
        gint64 seconds;
        const gchar* text;
    } CASES[] = {
        {-5, "in the future"},
        {1, "1 second ago"},
        {89, "89 seconds ago"},
        {90, "2 minutes ago"},
        {3600, "60 minutes ago"},
        {5400, "2 hours ago"},
        {35 * 3600, "35 hours ago"},
        {2 * 86400, "2 days ago"},
        {20 * 86400, "3 weeks ago"},
        {100 * 86400, "3 months ago"},
        {365 * 86400, "1 year ago"},
        {430 * 86400, "1 year, 2 months ago"},
        {3000 * 86400, "8 years ago"},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(CASES); i++) {
        gchar* text = git_relative_date(CASES[i].seconds);
        g_assert_cmpstr(text, ==, CASES[i].text);
        g_free(text);
    }
}

static void test_list_branches(Fixture* fx, gconstpointer data) {
    (void)data;
    g_assert_null(git_list_branches(NULL));
    gchar* plain = make_path(fx, "plain", NULL);
    g_assert_null(git_list_branches(plain));
    g_free(plain);

    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);
    g_free(make_path(fx, "repo/f.txt", "content\n"));
    repo_add(repo, "f.txt");
    repo_commit(repo, "commit 1", 100);

    /* Create branches: feature-b, feature-a */
    git_reference* head_ref = NULL;
    g_assert_cmpint(git_repository_head(&head_ref, repo), ==, 0);
    const git_oid* head_oid = git_reference_target(head_ref);
    git_commit* target_commit = NULL;
    g_assert_cmpint(git_commit_lookup(&target_commit, repo, head_oid), ==, 0);

    git_reference* b1 = NULL;
    git_reference* b2 = NULL;
    g_assert_cmpint(git_branch_create(&b1, repo, "feature-b", target_commit, 0), ==, 0);
    g_assert_cmpint(git_branch_create(&b2, repo, "feature-a", target_commit, 0), ==, 0);
    git_reference_free(b1);
    git_reference_free(b2);
    git_commit_free(target_commit);
    git_reference_free(head_ref);

    GPtrArray* branches = git_list_branches(repo_dir);
    g_assert_nonnull(branches);
    g_assert_cmpuint(branches->len, ==, 3);

    /* Current branch should be first with is_head == TRUE */
    GitBranchInfo* b = g_ptr_array_index(branches, 0);
    g_assert_true(b->is_head);
    g_assert_false(b->is_remote);

    /* Next branches should be sorted alphabetically: feature-a, feature-b */
    GitBranchInfo* b_next1 = g_ptr_array_index(branches, 1);
    GitBranchInfo* b_next2 = g_ptr_array_index(branches, 2);
    g_assert_false(b_next1->is_head);
    g_assert_false(b_next2->is_head);
    g_assert_cmpstr(b_next1->name, ==, "feature-a");
    g_assert_cmpstr(b_next2->name, ==, "feature-b");

    g_ptr_array_unref(branches);
    git_repository_free(repo);
    g_free(repo_dir);
}

static void test_checkout_branch(Fixture* fx, gconstpointer data) {
    (void)data;
    gchar* err = NULL;
    g_assert_false(git_checkout_branch(NULL, "main", &err));
    g_assert_nonnull(err);
    g_free(err);
    err = NULL;

    gchar* repo_dir = make_path(fx, "repo", NULL);
    git_repository* repo = repo_init(repo_dir);
    g_free(make_path(fx, "repo/f.txt", "v1\n"));
    repo_add(repo, "f.txt");
    repo_commit(repo, "commit 1", 100);

    git_reference* head_ref = NULL;
    g_assert_cmpint(git_repository_head(&head_ref, repo), ==, 0);
    const git_oid* head_oid = git_reference_target(head_ref);
    git_commit* target_commit = NULL;
    g_assert_cmpint(git_commit_lookup(&target_commit, repo, head_oid), ==, 0);

    git_reference* b1 = NULL;
    g_assert_cmpint(git_branch_create(&b1, repo, "feature-x", target_commit, 0), ==, 0);
    git_reference_free(b1);
    git_commit_free(target_commit);
    git_reference_free(head_ref);

    /* Checkout feature-x */
    g_assert_true(git_checkout_branch(repo_dir, "feature-x", &err));
    g_assert_null(err);

    gchar* cur = git_read_branch(repo_dir);
    g_assert_cmpstr(cur, ==, "feature-x");
    g_free(cur);

    /* Checkout invalid branch fails */
    g_assert_false(git_checkout_branch(repo_dir, "nonexistent", &err));
    g_assert_nonnull(err);
    g_free(err);

    git_repository_free(repo);
    g_free(repo_dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    git_libgit2_init();
    g_test_add("/git_utils/root_outside_repo", Fixture, NULL, fixture_setup, test_root_outside_repo, fixture_teardown);
    g_test_add("/git_utils/root_from_subdir", Fixture, NULL, fixture_setup, test_root_from_subdir, fixture_teardown);
    g_test_add("/git_utils/resolve_dir", Fixture, NULL, fixture_setup, test_resolve_dir, fixture_teardown);
    g_test_add("/git_utils/read_branch", Fixture, NULL, fixture_setup, test_read_branch, fixture_teardown);
    g_test_add("/git_utils/read_ahead_behind", Fixture, NULL, fixture_setup, test_read_ahead_behind, fixture_teardown);
    g_test_add("/git_utils/scan_statuses", Fixture, NULL, fixture_setup, test_scan_statuses, fixture_teardown);
    g_test_add("/git_utils/read_head_blob", Fixture, NULL, fixture_setup, test_read_head_blob, fixture_teardown);
    g_test_add("/git_utils/read_log", Fixture, NULL, fixture_setup, test_read_log, fixture_teardown);
    g_test_add("/git_utils/show_commit", Fixture, NULL, fixture_setup, test_show_commit, fixture_teardown);
    g_test_add("/git_utils/list_files", Fixture, NULL, fixture_setup, test_list_files, fixture_teardown);
    g_test_add_func("/git_utils/relative_date", test_relative_date);
    g_test_add("/git_utils/list_branches", Fixture, NULL, fixture_setup, test_list_branches, fixture_teardown);
    g_test_add("/git_utils/checkout_branch", Fixture, NULL, fixture_setup, test_checkout_branch, fixture_teardown);
    return g_test_run();
}
