/* Unit tests for src/git/git_space.c: the room of a work tree in a disk
 * scan (tracked, untracked, ignored, history, nested repositories).
 * Repositories are made with libgit2 (no git program needed). */
#include "git/git_space.h"
#include "git/git_utils.h"

#include "git_repo_fixture.h"

static const gchar* base(const GitSpaceEntry* e) {
    const gchar* slash = strrchr(e->path, G_DIR_SEPARATOR);
    return slash != NULL ? slash + 1 : e->path;
}

static void test_top(void) {
    DiskScan* scan = scan_of("repo", NULL);
    GitSpace* sp = git_space_compute(scan, NULL);
    g_assert_nonnull(sp);
    g_assert_false(sp->inside);
    g_assert_cmpuint(sp->tracked.size, ==, 411);
    g_assert_cmpuint(sp->tracked.count, ==, 3);
    g_assert_cmpuint(sp->untracked.size, ==, 50);
    g_assert_cmpuint(sp->untracked.count, ==, 1);
    g_assert_cmpuint(sp->ignored.size, ==, 1570);
    g_assert_cmpuint(sp->ignored.count, ==, 3);

    gchar* git = g_build_filename(tmp_dir, "repo", ".git", NULL);
    DiskNode* history = disk_scan_find(scan, git);
    g_free(git);
    g_assert_nonnull(history);
    g_assert_cmpuint(sp->history.size, ==, history->size);
    g_assert_cmpuint(sp->history.size, >, 0);
    gchar* lib = g_build_filename(tmp_dir, "repo", "vendor", "lib", NULL);
    DiskNode* nested = disk_scan_find(scan, lib);
    g_free(lib);
    g_assert_nonnull(nested);
    g_assert_cmpuint(sp->nested.size, ==, nested->size);
    g_assert_cmpuint(sp->nested.size, >=, 40);
    DiskNode* root = disk_scan_root(scan);
    g_assert_cmpuint(sp->tracked.size + sp->untracked.size + sp->ignored.size + sp->history.size + sp->nested.size,
                     ==, root->size);

    /* The ignored folder, then the ignored file beside sources */
    g_assert_cmpuint(sp->ignored_top->len, ==, 2);
    const GitSpaceEntry* e = &g_array_index(sp->ignored_top, GitSpaceEntry, 0);
    g_assert_cmpstr(base(e), ==, "build");
    g_assert_true(e->dir);
    g_assert_cmpuint(e->size, ==, 1500);
    g_assert_cmpuint(e->count, ==, 2);
    e = &g_array_index(sp->ignored_top, GitSpaceEntry, 1);
    g_assert_cmpstr(base(e), ==, "main.o");
    g_assert_false(e->dir);

    g_assert_cmpuint(sp->tracked_top->len, ==, 3);
    g_assert_cmpstr(base(&g_array_index(sp->tracked_top, GitSpaceEntry, 0)), ==, "big.c");
    g_assert_cmpstr(base(&g_array_index(sp->tracked_top, GitSpaceEntry, 1)), ==, "main.c");
    g_assert_cmpstr(base(&g_array_index(sp->tracked_top, GitSpaceEntry, 2)), ==, ".gitignore");
    git_space_free(sp);
    disk_scan_free(scan);
}

static void test_inside(void) {
    DiskScan* scan = scan_of("repo/src", NULL);
    GitSpace* sp = git_space_compute(scan, NULL);
    g_assert_nonnull(sp);
    g_assert_true(sp->inside);
    g_assert_cmpuint(sp->tracked.size, ==, 400);
    g_assert_cmpuint(sp->tracked.count, ==, 2);
    g_assert_cmpuint(sp->untracked.size, ==, 50);
    g_assert_cmpuint(sp->ignored.size, ==, 70);
    g_assert_cmpuint(sp->ignored.count, ==, 1);
    g_assert_cmpuint(sp->history.size, ==, 0);
    g_assert_cmpuint(sp->nested.size, ==, 0);
    g_assert_cmpuint(sp->ignored_top->len, ==, 1);
    git_space_free(sp);
    disk_scan_free(scan);
}

static void test_excluded(void) {
    static const gchar* const excluded[] = { "build", "src/big.c", NULL };
    DiskScan* scan = scan_of("repo", excluded);
    GitSpace* sp = git_space_compute(scan, NULL);
    g_assert_nonnull(sp);
    g_assert_cmpuint(sp->tracked.size, ==, 111);
    g_assert_cmpuint(sp->tracked.count, ==, 2);
    g_assert_cmpuint(sp->ignored.size, ==, 70);
    g_assert_cmpuint(sp->ignored_top->len, ==, 1);
    git_space_free(sp);
    disk_scan_free(scan);
}

static void test_not_a_repo(void) {
    put("plain/a.txt", 10);
    gchar* plain = g_build_filename(tmp_dir, "plain", NULL);
    gchar* top = find_git_root(plain);
    g_free(plain);
    DiskScan* scan = scan_of("plain", NULL);
    GitSpace* sp = git_space_compute(scan, NULL);
    /* The temporary folder may itself be in a repository on some machines */
    if (top == NULL) g_assert_null(sp);
    git_space_free(sp);
    disk_scan_free(scan);
    g_free(top);

    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    scan = scan_of("repo", NULL);
    g_assert_null(git_space_compute(scan, cancel));
    disk_scan_free(scan);
    g_object_unref(cancel);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    make_repo();
    g_test_add_func("/git_space/top", test_top);
    g_test_add_func("/git_space/inside", test_inside);
    g_test_add_func("/git_space/excluded", test_excluded);
    g_test_add_func("/git_space/not_a_repo", test_not_a_repo);
    int status = g_test_run();
    drop_repo();
    return status;
}
