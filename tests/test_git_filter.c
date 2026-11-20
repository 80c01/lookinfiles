/* Unit tests for src/git/git_filter.c: a disk scan of the files of the
 * repository only, or of the ignored files only. */
#include "git/git_filter.h"

#include "git_repo_fixture.h"

static DiskNode* child_named(DiskNode* node, const gchar* name) {
    for (guint i = 0; node != NULL && node->children != NULL && i < node->children->len; i++) {
        DiskNode* c = g_ptr_array_index(node->children, i);
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

static gboolean has_file(DiskNode* node, const gchar* name) {
    for (guint i = 0; node != NULL && node->files != NULL && i < node->files->len; i++) {
        if (strcmp(g_array_index(node->files, DiskFile, i).name, name) == 0) return TRUE;
    }
    return FALSE;
}

static DiskScan* filtered_scan(const gchar* rel, GitFilterMode mode, GitFilter** out) {
    gchar* root = g_build_filename(tmp_dir, rel, NULL);
    GitFilter* f = git_filter_new(root, mode, NULL);
    g_assert_nonnull(f);
    DiskScan* scan = disk_scan_new(root, TRUE);
    git_filter_apply(f, scan);
    disk_scan_run(scan, NULL);
    g_free(root);
    *out = f;
    return scan;
}

static void test_repo_files(void) {
    GitFilter* f = NULL;
    DiskScan* scan = filtered_scan("repo", GIT_FILTER_REPO, &f);
    DiskNode* root = disk_scan_root(scan);
    DiskNode* src = child_named(root, "src");
    g_assert_true(has_file(root, ".gitignore"));
    g_assert_true(has_file(src, "main.c") && has_file(src, "big.c") && has_file(src, "util.c"));
    g_assert_false(has_file(src, "main.o"));
    g_assert_null(child_named(root, "build"));
    g_assert_null(child_named(root, ".git"));
    /* The nested repository whole, its history too */
    DiskNode* lib = child_named(child_named(root, "vendor"), "lib");
    g_assert_nonnull(lib);
    g_assert_true(has_file(lib, "a.txt"));
    g_assert_nonnull(child_named(lib, ".git"));
    g_assert_cmpuint(root->size, ==, 461 + lib->size);
    disk_scan_free(scan);
    git_filter_free(f);
}

static void test_ignored_files(void) {
    GitFilter* f = NULL;
    DiskScan* scan = filtered_scan("repo", GIT_FILTER_IGNORED, &f);
    DiskNode* root = disk_scan_root(scan);
    g_assert_cmpuint(root->size, ==, 1570);
    g_assert_cmpuint(root->n_files, ==, 3);
    g_assert_nonnull(child_named(child_named(root, "build"), "x"));
    DiskNode* src = child_named(root, "src");
    g_assert_true(has_file(src, "main.o"));
    g_assert_false(has_file(src, "main.c"));
    g_assert_null(child_named(root, ".git"));
    g_assert_null(child_named(child_named(root, "vendor"), "lib"));
    disk_scan_free(scan);
    git_filter_free(f);
}

static void test_inside(void) {
    GitFilter* f = NULL;
    DiskScan* scan = filtered_scan("repo/src", GIT_FILTER_REPO, &f);
    g_assert_cmpuint(disk_scan_root(scan)->size, ==, 450);
    disk_scan_free(scan);
    git_filter_free(f);
    scan = filtered_scan("repo/src", GIT_FILTER_IGNORED, &f);
    g_assert_cmpuint(disk_scan_root(scan)->size, ==, 70);
    disk_scan_free(scan);
    git_filter_free(f);
}

static void test_none(void) {
    gchar* root = g_build_filename(tmp_dir, "repo", NULL);
    g_assert_null(git_filter_new(root, GIT_FILTER_NONE, NULL));
    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_assert_null(git_filter_new(root, GIT_FILTER_REPO, cancel));
    g_object_unref(cancel);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    make_repo();
    g_test_add_func("/git_filter/repo_files", test_repo_files);
    g_test_add_func("/git_filter/ignored_files", test_ignored_files);
    g_test_add_func("/git_filter/inside", test_inside);
    g_test_add_func("/git_filter/none", test_none);
    int status = g_test_run();
    drop_repo();
    return status;
}
