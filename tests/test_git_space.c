/* Unit tests for src/git/git_space.c: the room of a work tree in a disk
 * scan (tracked, untracked, ignored, history, nested repositories).
 * Repositories are made with libgit2 (no git program needed). */
#include "git/git_space.h"
#include "git/git_utils.h"

#include <git2.h>
#include <glib/gstdio.h>
#include <string.h>

static gchar* tmp_dir;

static void put(const gchar* rel, gsize size) {
    gchar* full = g_build_filename(tmp_dir, rel, NULL);
    gchar* parent = g_path_get_dirname(full);
    g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
    gchar* data = g_malloc0(size + 1);
    memset(data, 'x', size);
    g_assert_true(g_file_set_contents(full, data, (gssize)size, NULL));
    g_free(data);
    g_free(parent);
    g_free(full);
}

static void put_text(const gchar* rel, const gchar* text) {
    gchar* full = g_build_filename(tmp_dir, rel, NULL);
    g_assert_true(g_file_set_contents(full, text, -1, NULL));
    g_free(full);
}

static void remove_tree(const gchar* path) {
    GStatBuf st;
    if (g_lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        GDir* dir = g_dir_open(path, 0, NULL);
        const gchar* name;
        while (dir != NULL && (name = g_dir_read_name(dir)) != NULL) {
            gchar* full = g_build_filename(path, name, NULL);
            remove_tree(full);
            g_free(full);
        }
        if (dir != NULL) g_dir_close(dir);
    }
    g_remove(path);
}

static git_repository* repo_init(const gchar* rel) {
    gchar* dir = g_build_filename(tmp_dir, rel, NULL);
    git_repository* repo = NULL;
    g_assert_cmpint(git_repository_init(&repo, dir, 0), ==, 0);
    g_free(dir);
    return repo;
}

static void repo_add(git_repository* repo, const gchar* rel) {
    git_index* index = NULL;
    g_assert_cmpint(git_repository_index(&index, repo), ==, 0);
    g_assert_cmpint(git_index_add_bypath(index, rel), ==, 0);
    g_assert_cmpint(git_index_write(index), ==, 0);
    git_index_free(index);
}

/* repo/: .gitignore (11) src/main.c (100) src/big.c (300) tracked,
 * src/util.c (50) untracked, src/main.o (70) build/out.bin (1000)
 * build/x/y.bin (500) ignored, vendor/lib/ a repository of its own */
static void make_repo(void) {
    git_libgit2_init();
    tmp_dir = g_dir_make_tmp("git-space-XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);
    git_repository* repo = repo_init("repo");
    put_text("repo/.gitignore", "build/\n*.o\n");
    put("repo/src/main.c", 100);
    put("repo/src/big.c", 300);
    repo_add(repo, ".gitignore");
    repo_add(repo, "src/main.c");
    repo_add(repo, "src/big.c");
    put("repo/src/util.c", 50);
    put("repo/src/main.o", 70);
    put("repo/build/out.bin", 1000);
    put("repo/build/x/y.bin", 500);
    git_repository_free(repo);
    git_repository* lib = repo_init("repo/vendor/lib");
    put("repo/vendor/lib/a.txt", 40);
    git_repository_free(lib);
}

static DiskScan* scan_of(const gchar* rel, const gchar* const* excluded) {
    gchar* root = g_build_filename(tmp_dir, rel, NULL);
    DiskScan* scan = disk_scan_new(root, TRUE);
    for (gsize i = 0; excluded != NULL && excluded[i] != NULL; i++) {
        gchar* path = g_build_filename(root, excluded[i], NULL);
        disk_scan_exclude(scan, path);
        g_free(path);
    }
    disk_scan_run(scan, NULL);
    g_free(root);
    return scan;
}

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
    remove_tree(tmp_dir);
    g_free(tmp_dir);
    git_libgit2_shutdown();
    return status;
}
