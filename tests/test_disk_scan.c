/* Unit tests for src/core/disk_scan.c and src/core/disk_stats.c: sizes
 * rolled up, the largest files, links, unreadable folders, statistics. */
#include "core/disk_scan.h"
#include "core/disk_stats.h"

#include <glib/gstdio.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <unistd.h>
#endif

static void put(const gchar* dir, const gchar* rel, gsize size) {
    gchar* full = g_build_filename(dir, rel, NULL);
    gchar* parent = g_path_get_dirname(full);
    g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
    gchar* data = g_malloc0(size + 1);
    memset(data, 'x', size);
    g_assert_true(g_file_set_contents(full, data, (gssize)size, NULL));
    g_free(data);
    g_free(parent);
    g_free(full);
}

static void put_dir(const gchar* dir, const gchar* rel) {
    gchar* full = g_build_filename(dir, rel, NULL);
    g_assert_cmpint(g_mkdir_with_parents(full, 0700), ==, 0);
    g_free(full);
}

static void remove_tree(const gchar* path) {
    GStatBuf st;
    if (g_lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        g_chmod(path, 0700);
        GDir* dir = g_dir_open(path, 0, NULL);
        if (dir != NULL) {
            const gchar* name;
            while ((name = g_dir_read_name(dir)) != NULL) {
                gchar* full = g_build_filename(path, name, NULL);
                remove_tree(full);
                g_free(full);
            }
            g_dir_close(dir);
        }
    }
    g_remove(path);
}

static DiskNode* child_named(DiskNode* node, const gchar* name) {
    for (guint i = 0; node->children != NULL && i < node->children->len; i++) {
        DiskNode* c = g_ptr_array_index(node->children, i);
        if (g_strcmp0(c->name, name) == 0) return c;
    }
    return NULL;
}

/* root: a.txt 100, b.log 10, docs/ (r.md 1000, deep/x/y.png 5000), empty/ */
static gchar* make_tree(void) {
    gchar* dir = g_dir_make_tmp("disk-scan-XXXXXX", NULL);
    g_assert_nonnull(dir);
    put(dir, "a.txt", 100);
    put(dir, "b.log", 10);
    put(dir, "docs/r.md", 1000);
    put(dir, "docs/deep/x/y.png", 5000);
    put_dir(dir, "empty");
    return dir;
}

static DiskScan* scan_dir(const gchar* dir) {
    DiskScan* s = disk_scan_new(dir, TRUE);
    disk_scan_run(s, NULL);
    return s;
}

static void test_sizes(void) {
    gchar* dir = make_tree();
    DiskScan* s = scan_dir(dir);
    DiskNode* root = disk_scan_root(s);
    g_assert_cmpuint(root->size, ==, 6110);
    g_assert_cmpuint(root->n_files, ==, 4);
    g_assert_cmpuint(root->n_dirs, ==, 4); /* docs, deep, x, empty */
    g_assert_cmpuint(root->files_size, ==, 110);
    g_assert_cmpuint(root->files_count, ==, 2);
    g_assert_false(disk_scan_cancelled(s));

    /* Folders largest first, files largest first */
    g_assert_cmpuint(root->children->len, ==, 2);
    DiskNode* docs = g_ptr_array_index(root->children, 0);
    g_assert_cmpstr(docs->name, ==, "docs");
    g_assert_cmpuint(docs->size, ==, 6000);
    g_assert_cmpuint(docs->n_dirs, ==, 2);
    g_assert_cmpstr(((DiskNode*)g_ptr_array_index(root->children, 1))->name, ==, "empty");
    g_assert_cmpstr(g_array_index(root->files, DiskFile, 0).name, ==, "a.txt");
    g_assert_cmpstr(g_array_index(root->files, DiskFile, 1).name, ==, "b.log");

    DiskNode* x = child_named(child_named(docs, "deep"), "x");
    g_assert_nonnull(x);
    gchar* path = disk_node_path(x);
    gchar* want = g_build_filename(dir, "docs", "deep", "x", NULL);
    g_assert_cmpstr(path, ==, want);
    g_free(want);
    g_free(path);

    const DiskStats* st = disk_scan_stats(s);
    g_assert_cmpuint(st->size, ==, 6110);
    g_assert_cmpuint(st->n_files, ==, 4);
    g_assert_cmpuint(st->n_dirs, ==, 4);
    g_assert_cmpuint(st->n_empty_dirs, ==, 1);
    g_assert_cmpuint(st->max_depth, ==, 3);
    g_assert_true(g_str_has_suffix(st->deepest, "x"));
    g_assert_cmpuint(st->largest->len, ==, 4);
    g_assert_true(g_str_has_suffix(g_array_index(st->largest, DiskBigFile, 0).path, "y.png"));
    g_assert_cmpuint(st->ages[DISK_AGE_DAY].count, ==, 4);
    g_assert_cmpuint(st->sizes[DISK_SIZE_TINY].count, ==, 3); /* 100, 10, 1000 bytes */
    g_assert_cmpuint(st->sizes[DISK_SIZE_TINY].size, ==, 1110);
    g_assert_cmpuint(st->sizes[DISK_SIZE_1K].count, ==, 1);
    g_assert_cmpuint(st->sizes[DISK_SIZE_1K].size, ==, 5000);
    g_assert_cmpuint(st->sizes[DISK_SIZE_EMPTY].count, ==, 0);
    /* Each folder keeps the groups of its subtree */
    for (gint g = 0; g < DISK_SIZE_COUNT; g++) {
        g_assert_cmpuint(root->sizes[g].count, ==, st->sizes[g].count);
        g_assert_cmpuint(root->sizes[g].size, ==, st->sizes[g].size);
    }
    g_assert_cmpuint(root->ages[DISK_AGE_DAY].count, ==, 4);
    g_assert_cmpuint(docs->sizes[DISK_SIZE_TINY].count, ==, 1); /* r.md */
    g_assert_cmpuint(docs->sizes[DISK_SIZE_1K].count, ==, 1);   /* deep/x/y.png */
    g_assert_cmpuint(docs->sizes[DISK_SIZE_1K].size, ==, 5000);
    g_assert_cmpuint(docs->ages[DISK_AGE_DAY].count, ==, 2);
    g_assert_cmpuint(g_hash_table_size(st->extensions), ==, 4);
    disk_scan_free(s);
    remove_tree(dir);
    g_free(dir);
}

static void test_entries(void) {
    gchar* dir = make_tree();
    DiskScan* s = scan_dir(dir);
    DiskNode* root = disk_scan_root(s);
    GArray* e = disk_node_entries(root, 0);
    g_assert_cmpuint(e->len, ==, 3); /* docs, a.txt, b.log ("empty" has no size) */
    g_assert_true(g_array_index(e, DiskEntry, 0).dir == child_named(root, "docs"));
    g_assert_cmpstr(g_array_index(e, DiskEntry, 1).file, ==, "a.txt");
    g_assert_cmpuint(g_array_index(e, DiskEntry, 2).size, ==, 10);
    g_array_unref(e);
    e = disk_node_entries(root, 2); /* the smallest summed up */
    g_assert_cmpuint(e->len, ==, 2);
    const DiskEntry* rest = &g_array_index(e, DiskEntry, 1);
    g_assert_null(rest->dir);
    g_assert_null(rest->file);
    g_assert_cmpuint(rest->size, ==, 110);
    g_assert_cmpuint(rest->count, ==, 2);
    g_array_unref(e);
    disk_scan_free(s);
    remove_tree(dir);
    g_free(dir);
}

static void test_snapshot(void) {
    gchar* dir = make_tree();
    DiskScan* s = disk_scan_new(dir, TRUE);
    DiskScanSnapshot snap;
    disk_scan_snapshot(s, &snap);
    g_assert_false(snap.done);
    g_assert_cmpuint(snap.top->len, ==, 0);
    disk_scan_snapshot_clear(&snap);
    disk_scan_run(s, NULL);
    disk_scan_snapshot(s, &snap);
    g_assert_true(snap.done);
    g_assert_null(snap.current);
    g_assert_cmpuint(snap.files, ==, 4);
    g_assert_cmpuint(snap.dirs, ==, 4);
    g_assert_cmpuint(snap.size, ==, 6110);
    g_assert_cmpuint(snap.top_total, ==, 2);
    g_assert_cmpuint(snap.top_done, ==, 2);
    g_assert_cmpuint(snap.top->len, ==, 2);
    g_assert_cmpuint(snap.root_files_size, ==, 110);
    g_assert_cmpuint(snap.root_files_count, ==, 2);
    gboolean docs = FALSE;
    for (guint i = 0; i < snap.top->len; i++) {
        DiskNode* n = g_ptr_array_index(snap.top, i);
        g_assert_null(n->children);
        if (g_strcmp0(n->name, "docs") == 0) {
            docs = TRUE;
            g_assert_cmpuint(n->size, ==, 6000);
        }
    }
    g_assert_true(docs);
    disk_scan_snapshot_clear(&snap);
    disk_scan_free(s);
    remove_tree(dir);
    g_free(dir);
}

/* Only the largest files of a folder are listed, the rest summed up */
static void test_other_files(void) {
    gchar* dir = g_dir_make_tmp("disk-scan-XXXXXX", NULL);
    guint64 total = 0;
    for (guint i = 1; i <= DISK_SCAN_DIR_FILES + 5; i++) {
        gchar* name = g_strdup_printf("f%02u.bin", i);
        put(dir, name, i);
        total += i;
        g_free(name);
    }
    DiskScan* s = scan_dir(dir);
    DiskNode* root = disk_scan_root(s);
    g_assert_cmpuint(root->files->len, ==, DISK_SCAN_DIR_FILES);
    g_assert_cmpuint(g_array_index(root->files, DiskFile, 0).size, ==, DISK_SCAN_DIR_FILES + 5);
    g_assert_cmpuint(disk_node_other_files_count(root), ==, 5);
    g_assert_cmpuint(disk_node_other_files_size(root), ==, 1 + 2 + 3 + 4 + 5);
    g_assert_cmpuint(root->size, ==, total);
    GArray* ext = disk_stats_top_extensions(disk_scan_stats(s), 10);
    g_assert_cmpuint(ext->len, ==, 1);
    g_assert_cmpstr(g_array_index(ext, DiskExtension, 0).ext, ==, "bin");
    g_assert_cmpuint(g_array_index(ext, DiskExtension, 0).tally.count, ==, DISK_SCAN_DIR_FILES + 5);
    g_array_unref(ext);
    disk_scan_free(s);
    remove_tree(dir);
    g_free(dir);
}

static void test_missing_root(void) {
    DiskScan* s = disk_scan_new("/nonexistent/disk-scan-test", TRUE);
    disk_scan_run(s, NULL);
    g_assert_true(disk_scan_root(s)->unreadable);
    g_assert_cmpuint(disk_scan_stats(s)->n_errors, ==, 1);
    g_assert_cmpuint(disk_scan_root(s)->size, ==, 0);
    disk_scan_free(s);
}

static void test_cancelled(void) {
    gchar* dir = make_tree();
    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    DiskScan* s = disk_scan_new(dir, TRUE);
    disk_scan_run(s, cancel);
    g_assert_true(disk_scan_cancelled(s));
    g_assert_cmpuint(disk_scan_root(s)->n_files, ==, 0);
    disk_scan_free(s);
    g_object_unref(cancel);
    remove_tree(dir);
    g_free(dir);
}

/* A trailing separator is dropped, a filesystem root keeps its own */
static void test_root_path(void) {
    gchar* dir = make_tree();
    gchar* slash = g_strconcat(dir, G_DIR_SEPARATOR_S, NULL);
    DiskScan* s = disk_scan_new(slash, TRUE);
    g_assert_cmpstr(disk_scan_root_path(s), ==, dir);
    disk_scan_free(s);
    g_free(slash);
#ifdef G_OS_UNIX
    s = disk_scan_new("/", TRUE);
    g_assert_cmpstr(disk_scan_root_path(s), ==, "/");
    disk_scan_free(s);
#endif
    remove_tree(dir);
    g_free(dir);
}

#ifdef G_OS_UNIX
static void test_links(void) {
    gchar* dir = make_tree();
    gchar* target = g_build_filename(dir, "docs", NULL);
    gchar* sym = g_build_filename(dir, "link-to-docs", NULL);
    g_assert_cmpint(symlink(target, sym), ==, 0);
    gchar* a = g_build_filename(dir, "a.txt", NULL);
    gchar* hard = g_build_filename(dir, "empty", "hard.txt", NULL);
    g_assert_cmpint(link(a, hard), ==, 0);
    DiskScan* s = scan_dir(dir);
    const DiskStats* st = disk_scan_stats(s);
    /* The symbolic link is not followed, the hard link counted once */
    g_assert_cmpuint(disk_scan_root(s)->size, ==, 6110);
    g_assert_cmpuint(st->n_symlinks, ==, 1);
    g_assert_cmpuint(st->n_hardlinks, ==, 1);
    g_assert_cmpuint(st->n_files, ==, 4);
    g_assert_cmpuint(st->n_empty_dirs, ==, 0);
    disk_scan_free(s);
    g_free(a);
    g_free(hard);
    g_free(target);
    g_free(sym);
    remove_tree(dir);
    g_free(dir);
}

static void test_unreadable(void) {
    if (geteuid() == 0) {
        g_test_skip("root reads every folder");
        return;
    }
    gchar* dir = make_tree();
    put(dir, "locked/secret.bin", 50);
    gchar* locked = g_build_filename(dir, "locked", NULL);
    g_assert_cmpint(g_chmod(locked, 0), ==, 0);
    DiskScan* s = scan_dir(dir);
    DiskNode* node = child_named(disk_scan_root(s), "locked");
    g_assert_nonnull(node);
    g_assert_true(node->unreadable);
    const DiskStats* st = disk_scan_stats(s);
    g_assert_cmpuint(st->n_errors, ==, 1);
    g_assert_cmpstr(g_array_index(st->errors, DiskError, 0).path, ==, locked);
    g_assert_cmpuint(disk_scan_root(s)->size, ==, 6110);
    disk_scan_free(s);
    g_free(locked);
    remove_tree(dir);
    g_free(dir);
}
#endif

static void test_ages(void) {
    const gint64 day = 24 * 3600;
    gint64 now = 1800000000;
    g_assert_cmpint(disk_age_of(now, now), ==, DISK_AGE_DAY);
    g_assert_cmpint(disk_age_of(now + 50, now), ==, DISK_AGE_DAY); /* in the future */
    g_assert_cmpint(disk_age_of(now - 2 * day, now), ==, DISK_AGE_WEEK);
    g_assert_cmpint(disk_age_of(now - 10 * day, now), ==, DISK_AGE_MONTH);
    g_assert_cmpint(disk_age_of(now - 100 * day, now), ==, DISK_AGE_YEAR);
    g_assert_cmpint(disk_age_of(now - 400 * day, now), ==, DISK_AGE_3_YEARS);
    g_assert_cmpint(disk_age_of(now - 2000 * day, now), ==, DISK_AGE_OLDER);
    g_assert_cmpint(disk_age_of(0, now), ==, DISK_AGE_OLDER);
    for (gint a = 0; a < DISK_AGE_COUNT; a++) {
        g_assert_cmpstr(disk_age_name((DiskAge)a), !=, "");
        g_assert_cmpstr(disk_age_label((DiskAge)a), !=, "");
    }
}

static void test_size_groups(void) {
    g_assert_cmpint(disk_size_of(0), ==, DISK_SIZE_EMPTY);
    g_assert_cmpint(disk_size_of(1), ==, DISK_SIZE_TINY);
    g_assert_cmpint(disk_size_of(1023), ==, DISK_SIZE_TINY);
    g_assert_cmpint(disk_size_of(1024), ==, DISK_SIZE_1K);
    g_assert_cmpint(disk_size_of(10 * 1024 - 1), ==, DISK_SIZE_1K);
    g_assert_cmpint(disk_size_of(10 * 1024), ==, DISK_SIZE_10K);
    g_assert_cmpint(disk_size_of(100 * 1024), ==, DISK_SIZE_100K);
    g_assert_cmpint(disk_size_of(1024 * 1024 - 1), ==, DISK_SIZE_100K);
    g_assert_cmpint(disk_size_of(1024 * 1024), ==, DISK_SIZE_1M);
    g_assert_cmpint(disk_size_of(50ULL << 20), ==, DISK_SIZE_10M);
    g_assert_cmpint(disk_size_of(500ULL << 20), ==, DISK_SIZE_100M);
    g_assert_cmpint(disk_size_of(1ULL << 30), ==, DISK_SIZE_1G);
    g_assert_cmpint(disk_size_of((10ULL << 30) - 1), ==, DISK_SIZE_1G);
    g_assert_cmpint(disk_size_of(10ULL << 30), ==, DISK_SIZE_HUGE);
    g_assert_cmpint(disk_size_of(G_MAXUINT64), ==, DISK_SIZE_HUGE);
    for (gint g = 0; g < DISK_SIZE_COUNT; g++) {
        g_assert_cmpstr(disk_size_name((DiskSize)g), !=, "");
        g_assert_cmpstr(disk_size_label((DiskSize)g), !=, "");
    }
    g_assert_cmpstr(disk_size_label(DISK_SIZE_1M), ==, "1M");
    gint64 lo, hi;
    disk_size_bounds(DISK_SIZE_EMPTY, &lo, &hi);
    g_assert_cmpint(lo, ==, 0);
    g_assert_cmpint(hi, ==, 0);
    disk_size_bounds(DISK_SIZE_TINY, &lo, &hi);
    g_assert_cmpint(lo, ==, 1);
    g_assert_cmpint(hi, ==, 1023);
    disk_size_bounds(DISK_SIZE_10K, &lo, &hi);
    g_assert_cmpint(lo, ==, 10 * 1024);
    g_assert_cmpint(hi, ==, 100 * 1024 - 1);
    disk_size_bounds(DISK_SIZE_HUGE, &lo, &hi);
    g_assert_cmpint(lo, ==, (gint64)(10ULL << 30));
    g_assert_cmpint(hi, ==, -1);
    /* Every size falls in the group whose bounds hold it */
    for (gint g = DISK_SIZE_EMPTY; g < DISK_SIZE_HUGE; g++) {
        disk_size_bounds((DiskSize)g, &lo, &hi);
        g_assert_cmpint(disk_size_of((guint64)lo), ==, g);
        g_assert_cmpint(disk_size_of((guint64)hi), ==, g);
    }
}

static void test_kinds(void) {
    g_assert_cmpint(disk_kind_of_extension("png"), ==, DISK_KIND_IMAGES);
    g_assert_cmpint(disk_kind_of_extension("mp4"), ==, DISK_KIND_VIDEOS);
    g_assert_cmpint(disk_kind_of_extension("pdf"), ==, DISK_KIND_DOCUMENTS);
    g_assert_cmpint(disk_kind_of_extension("zip"), ==, DISK_KIND_ARCHIVES);
    g_assert_cmpint(disk_kind_of_extension("c"), ==, DISK_KIND_CODE);
    g_assert_cmpint(disk_kind_of_extension(""), ==, DISK_KIND_OTHER);

    gchar* dir = make_tree();
    DiskScan* s = scan_dir(dir);
    DiskTally kinds[DISK_KIND_COUNT];
    disk_stats_kinds(disk_scan_stats(s), kinds);
    g_assert_cmpuint(kinds[DISK_KIND_IMAGES].size, ==, 5000);
    g_assert_cmpuint(kinds[DISK_KIND_DOCUMENTS].size, ==, 1000);
    g_assert_cmpuint(kinds[DISK_KIND_CODE].size, ==, 110); /* .txt, .log */
    GArray* ext = disk_stats_top_extensions(disk_scan_stats(s), 2);
    g_assert_cmpuint(ext->len, ==, 2);
    g_assert_cmpstr(g_array_index(ext, DiskExtension, 0).ext, ==, "png");
    g_assert_cmpstr(g_array_index(ext, DiskExtension, 1).ext, ==, "md");
    g_array_unref(ext);
    disk_scan_free(s);
    remove_tree(dir);
    g_free(dir);
}

static void test_format(void) {
    gchar* p = disk_format_percent(1, 4);
    g_assert_cmpstr(p, ==, "25.0%");
    g_free(p);
    p = disk_format_percent(1, 100000);
    g_assert_cmpstr(p, ==, "< 0.1%");
    g_free(p);
    p = disk_format_percent(0, 0);
    g_assert_cmpstr(p, ==, "0.0%");
    g_free(p);
    p = disk_format_files(1);
    g_assert_cmpstr(p, ==, "1 file");
    g_free(p);
    DiskTally t = { 2000, 3 };
    p = disk_format_tally(&t);
    g_assert_cmpstr(p, ==, "2.0\u00a0kB (3 files)");
    g_free(p);
    p = disk_format_group("1k to 10k", &t, 12, 8000);
    g_assert_cmpstr(p, ==, "1k to 10k\n3 files (25.0%)\n2.0\u00a0kB (25.0%)");
    g_free(p);
}

/* Left out of the reading: a folder and a file */
static void test_exclude(void) {
    gchar* dir = make_tree();
    DiskScan* s = disk_scan_new(dir, TRUE);
    gchar* deep = g_build_filename(dir, "docs", "deep", NULL);
    gchar* a = g_build_filename(dir, "a.txt", NULL);
    disk_scan_exclude(s, deep);
    disk_scan_exclude(s, a);
    disk_scan_run(s, NULL);
    DiskNode* root = disk_scan_root(s);
    g_assert_cmpuint(root->size, ==, 1010);
    g_assert_cmpuint(root->n_files, ==, 2);
    g_assert_cmpuint(root->n_dirs, ==, 2); /* docs, empty */
    g_assert_null(child_named(child_named(root, "docs"), "deep"));
    const DiskStats* st = disk_scan_stats(s);
    g_assert_cmpuint(st->n_excluded, ==, 2);
    g_assert_cmpuint(st->size, ==, 1010);
    g_assert_cmpuint(st->largest->len, ==, 2);
    g_assert_cmpuint(g_hash_table_size(st->extensions), ==, 2);
    disk_scan_free(s);
    g_free(deep);
    g_free(a);
    remove_tree(dir);
    g_free(dir);
}

/* docs kept and filtered, docs/deep read whole, empty and a.txt and
 * docs/r.md left out */
static DiskFilterVerdict filter_some(const gchar* rel, gboolean dir, gpointer data) {
    g_ptr_array_add(data, g_strdup_printf("%s%s", rel, dir ? "/" : ""));
    gchar* norm = g_strdelimit(g_strdup(rel), G_DIR_SEPARATOR_S, '/');
    DiskFilterVerdict v = DISK_FILTER_KEEP;
    if (strcmp(norm, "docs/deep") == 0) v = DISK_FILTER_ALL;
    else if (strcmp(norm, "empty") == 0 || strcmp(norm, "a.txt") == 0 || strcmp(norm, "docs/r.md") == 0)
        v = DISK_FILTER_SKIP;
    g_free(norm);
    return v;
}

static void test_filter(void) {
    gchar* dir = make_tree();
    DiskScan* s = disk_scan_new(dir, TRUE);
    GPtrArray* seen = g_ptr_array_new_with_free_func(g_free);
    disk_scan_set_filter(s, filter_some, seen);
    disk_scan_run(s, NULL);
    DiskNode* root = disk_scan_root(s);
    g_assert_cmpuint(root->size, ==, 5010);
    g_assert_cmpuint(root->n_files, ==, 2);
    g_assert_cmpuint(root->n_dirs, ==, 3); /* docs, deep, x */
    g_assert_null(child_named(root, "empty"));
    g_assert_cmpuint(seen->len, ==, 6); /* a.txt b.log docs/ empty/ docs/r.md docs/deep/: not below deep */
    disk_scan_free(s);
    g_ptr_array_unref(seen);
    remove_tree(dir);
    g_free(dir);
}

/* Taken out of the tree read */
static void test_remove(void) {
    gchar* dir = make_tree();
    DiskScan* s = scan_dir(dir);
    DiskNode* root = disk_scan_root(s);
    const DiskStats* st = disk_scan_stats(s);
    guint64 disk = st->disk;
    guint64 x_disk = child_named(child_named(child_named(root, "docs"), "deep"), "x")->disk;
    g_assert_true(disk_scan_find(s, dir) == root);

    gchar* x = g_build_filename(dir, "docs", "deep", "x", NULL);
    gchar* deep = g_build_filename(dir, "docs", "deep", NULL);
    g_assert_true(disk_scan_find(s, x) != NULL);
    g_assert_true(disk_scan_remove(s, x));
    g_assert_null(disk_scan_find(s, x));
    g_assert_false(disk_scan_remove(s, x)); /* gone */
    DiskNode* docs = child_named(root, "docs");
    g_assert_true(disk_scan_find(s, deep) == child_named(docs, "deep"));
    g_assert_null(child_named(docs, "deep")->children);
    g_assert_cmpuint(docs->size, ==, 1000);
    g_assert_cmpuint(docs->n_files, ==, 1);
    g_assert_cmpuint(docs->n_dirs, ==, 1);
    g_assert_cmpuint(root->size, ==, 1110);
    g_assert_cmpuint(root->n_files, ==, 3);
    g_assert_cmpuint(root->n_dirs, ==, 3);
    g_assert_cmpuint(st->size, ==, 1110);
    g_assert_cmpuint(st->disk, ==, disk - x_disk);
    g_assert_cmpuint(st->n_files, ==, 3);
    g_assert_cmpuint(st->n_dirs, ==, 3);
    g_assert_cmpuint(st->largest->len, ==, 3);
    g_assert_cmpuint(st->n_excluded, ==, 1);
    g_assert_cmpuint(docs->sizes[DISK_SIZE_1K].count, ==, 0); /* y.png went with x */
    g_assert_cmpuint(root->sizes[DISK_SIZE_1K].count, ==, 0);
    g_assert_cmpuint(st->sizes[DISK_SIZE_1K].count, ==, 0);
    g_assert_cmpuint(st->ages[DISK_AGE_DAY].count, ==, 3);

    /* A listed file; then the folders are sorted again by size */
    gchar* r = g_build_filename(dir, "docs", "r.md", NULL);
    g_assert_true(disk_scan_remove(s, r));
    g_assert_cmpuint(docs->size, ==, 0);
    g_assert_cmpuint(docs->files_count, ==, 0);
    g_assert_null(docs->files);
    g_assert_cmpuint(root->size, ==, 110);
    g_assert_cmpuint(st->n_files, ==, 2);
    g_assert_cmpuint(st->largest->len, ==, 2);
    g_assert_cmpuint(docs->sizes[DISK_SIZE_TINY].count, ==, 0);
    g_assert_cmpuint(root->sizes[DISK_SIZE_TINY].count, ==, 2);
    g_assert_cmpuint(root->sizes[DISK_SIZE_TINY].size, ==, 110);
    g_assert_cmpuint(st->ages[DISK_AGE_DAY].count, ==, 2);
    g_assert_cmpstr(((DiskNode*)g_ptr_array_index(root->children, 0))->name, ==, "docs");

    /* Not below the root, the root itself */
    g_assert_false(disk_scan_remove(s, "/nowhere/at/all"));
    g_assert_false(disk_scan_remove(s, dir));
    gchar* outside = g_strconcat(dir, "x", NULL); /* a sibling sharing the prefix */
    g_assert_null(disk_scan_find(s, outside));
    g_free(outside);
    g_free(r);
    g_free(x);
    g_free(deep);
    disk_scan_free(s);
    remove_tree(dir);
    g_free(dir);
}

/* The Deep Search criteria of a group find its files */
static void test_criteria(void) {
    gchar *min = NULL, *max = NULL;
    disk_size_criteria(DISK_SIZE_EMPTY, &min, &max);
    g_assert_cmpstr(min, ==, "0");
    g_assert_cmpstr(max, ==, "0");
    g_free(min);
    g_free(max);
    disk_size_criteria(DISK_SIZE_1K, &min, &max);
    g_assert_cmpstr(min, ==, "1024");
    g_assert_cmpstr(max, ==, "10239");
    g_free(min);
    g_free(max);
    disk_size_criteria(DISK_SIZE_HUGE, &min, &max);
    g_assert_cmpstr(min, ==, "10737418240");
    g_assert_null(max);
    g_free(min);

    gchar *after = NULL, *before = NULL;
    disk_age_criteria(DISK_AGE_DAY, &after, &before);
    g_assert_cmpstr(after, ==, "1d");
    g_assert_null(before);
    g_free(after);
    disk_age_criteria(DISK_AGE_MONTH, &after, &before);
    g_assert_cmpstr(after, ==, "30d");
    g_assert_cmpstr(before, ==, "7d");
    g_free(after);
    g_free(before);
    disk_age_criteria(DISK_AGE_OLDER, &after, &before);
    g_assert_null(after);
    g_assert_cmpstr(before, ==, "1095d");
    g_free(before);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/disk_scan/sizes", test_sizes);
    g_test_add_func("/disk_scan/entries", test_entries);
    g_test_add_func("/disk_scan/snapshot", test_snapshot);
    g_test_add_func("/disk_scan/other_files", test_other_files);
    g_test_add_func("/disk_scan/missing_root", test_missing_root);
    g_test_add_func("/disk_scan/cancelled", test_cancelled);
    g_test_add_func("/disk_scan/root_path", test_root_path);
    g_test_add_func("/disk_scan/exclude", test_exclude);
    g_test_add_func("/disk_scan/filter", test_filter);
    g_test_add_func("/disk_scan/remove", test_remove);
#ifdef G_OS_UNIX
    g_test_add_func("/disk_scan/links", test_links);
    g_test_add_func("/disk_scan/unreadable", test_unreadable);
#endif
    g_test_add_func("/disk_stats/ages", test_ages);
    g_test_add_func("/disk_stats/size_groups", test_size_groups);
    g_test_add_func("/disk_stats/kinds", test_kinds);
    g_test_add_func("/disk_stats/format", test_format);
    g_test_add_func("/disk_stats/criteria", test_criteria);
    return g_test_run();
}
