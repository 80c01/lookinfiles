/* A git repository to scan for the tests of git_space and git_filter:
 * made with libgit2 in a temporary folder (no git program needed). */
#ifndef GIT_REPO_FIXTURE_H
#define GIT_REPO_FIXTURE_H

#include "core/disk_scan.h"

#include <git2.h>
#include <glib/gstdio.h>
#include <string.h>

static gchar* tmp_dir;

static inline void put(const gchar* rel, gsize size) {
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

static inline void put_text(const gchar* rel, const gchar* text) {
    gchar* full = g_build_filename(tmp_dir, rel, NULL);
    g_assert_true(g_file_set_contents(full, text, -1, NULL));
    g_free(full);
}

static inline void remove_tree(const gchar* path) {
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

static inline git_repository* repo_init(const gchar* rel) {
    gchar* dir = g_build_filename(tmp_dir, rel, NULL);
    git_repository* repo = NULL;
    g_assert_cmpint(git_repository_init(&repo, dir, 0), ==, 0);
    g_free(dir);
    return repo;
}

static inline void repo_add(git_repository* repo, const gchar* rel) {
    git_index* index = NULL;
    g_assert_cmpint(git_repository_index(&index, repo), ==, 0);
    g_assert_cmpint(git_index_add_bypath(index, rel), ==, 0);
    g_assert_cmpint(git_index_write(index), ==, 0);
    git_index_free(index);
}

/* repo/: .gitignore (11) src/main.c (100) src/big.c (300) tracked,
 * src/util.c (50) untracked, src/main.o (70) build/out.bin (1000)
 * build/x/y.bin (500) ignored, vendor/lib/ a repository of its own */
static inline void make_repo(void) {
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

static inline DiskScan* scan_of(const gchar* rel, const gchar* const* excluded) {
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

static inline void drop_repo(void) {
    remove_tree(tmp_dir);
    g_free(tmp_dir);
    git_libgit2_shutdown();
}

#endif /* GIT_REPO_FIXTURE_H */
