/* Unit tests for src/explorer/dir_scan.c: explorer listings (sorting, hidden entries,
 * expanded folders, unreadable folders, expand all). */
#include "explorer/dir_scan.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar* fixture_dir(void) {
    gchar* dir = g_dir_make_tmp("dir-scan-XXXXXX", NULL);
    g_assert_nonnull(dir);
    return dir;
}

static void put(const gchar* dir, const gchar* rel, const gchar* contents) {
    gchar* full = g_build_filename(dir, rel, NULL);
    if (contents == NULL) {
        g_assert_cmpint(g_mkdir_with_parents(full, 0700), ==, 0);
    } else {
        g_assert_true(g_file_set_contents(full, contents, -1, NULL));
    }
    g_free(full);
}

static void remove_tree(const gchar* path) {
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
    g_remove(path);
}

static const gchar* name_at(const DirScanNode* node, guint i) {
    return g_array_index(node->entries, DirScanEntry, i).name;
}

static void test_sorted_folders_first(void) {
    gchar* dir = fixture_dir();
    put(dir, "b.txt", "bb");
    put(dir, "zdir", NULL);
    put(dir, "a.md", "aaaa");
    put(dir, "adir", NULL);
    put(dir, ".hidden", "h");

    DirScanNode* node = dir_scan_read(dir, FALSE, DIR_SCAN_SORT_NAME, NULL, NULL);
    g_assert_false(node->unreadable);
    g_assert_cmpuint(node->entries->len, ==, 4);
    g_assert_cmpstr(name_at(node, 0), ==, "adir");
    g_assert_cmpstr(name_at(node, 1), ==, "zdir");
    g_assert_cmpstr(name_at(node, 2), ==, "a.md");
    g_assert_cmpstr(name_at(node, 3), ==, "b.txt");
    g_assert_true(g_array_index(node->entries, DirScanEntry, 0).is_dir);
    g_assert_cmpint(g_array_index(node->entries, DirScanEntry, 2).size, ==, 4);
    g_assert_cmpint(g_array_index(node->entries, DirScanEntry, 0).size, ==, 0);
    g_assert_cmpuint(node->children->len, ==, 4);
    g_assert_null(g_ptr_array_index(node->children, 0)); /* not expanded */
    dir_scan_node_free(node);

    node = dir_scan_read(dir, TRUE, DIR_SCAN_SORT_SIZE, NULL, NULL);
    g_assert_cmpuint(node->entries->len, ==, 5);
    g_assert_cmpstr(name_at(node, 2), ==, "a.md"); /* largest file first, after the folders */
    g_assert_cmpstr(name_at(node, 3), ==, "b.txt");
    dir_scan_node_free(node);

    remove_tree(dir);
    g_free(dir);
}

static void test_compare_modes(void) {
    DirScanEntry a = {.name = "a.zip", .mtime = 10, .size = 5};
    DirScanEntry b = {.name = "b.c", .mtime = 20, .size = 1};
    DirScanEntry d = {.name = "z", .is_dir = TRUE};
    g_assert_cmpint(dir_scan_compare(&a, &b, DIR_SCAN_SORT_NAME), <, 0);
    g_assert_cmpint(dir_scan_compare(&a, &b, DIR_SCAN_SORT_DATE), >, 0); /* newest first */
    g_assert_cmpint(dir_scan_compare(&a, &b, DIR_SCAN_SORT_SIZE), <, 0); /* largest first */
    g_assert_cmpint(dir_scan_compare(&a, &b, DIR_SCAN_SORT_EXT), >, 0);  /* "c" before "zip" */
    g_assert_cmpint(dir_scan_compare(&d, &a, DIR_SCAN_SORT_SIZE), <, 0); /* folders first */
}

/* A listing sorted by extension: the keys made once give the order of
 * dir_scan_compare() without keys */
static void test_sort_keys(void) {
    gchar* dir = fixture_dir();
    put(dir, "b.TXT", "x");
    put(dir, "a.zip", "x");
    put(dir, "c.c", "x");
    put(dir, "A.c", "x");
    DirScanNode* node = dir_scan_read(dir, FALSE, DIR_SCAN_SORT_EXT, NULL, NULL);
    g_assert_cmpuint(node->entries->len, ==, 4);
    g_assert_cmpstr(name_at(node, 0), ==, "A.c");
    g_assert_cmpstr(name_at(node, 1), ==, "c.c");
    g_assert_cmpstr(name_at(node, 2), ==, "b.TXT");
    g_assert_cmpstr(name_at(node, 3), ==, "a.zip");
    for (guint i = 0; i + 1 < node->entries->len; i++) {
        DirScanEntry x = g_array_index(node->entries, DirScanEntry, i);
        DirScanEntry y = g_array_index(node->entries, DirScanEntry, i + 1);
        g_assert_nonnull(x.name_key);
        x.name_key = x.ext_key = y.name_key = y.ext_key = NULL;
        g_assert_cmpint(dir_scan_compare(&x, &y, DIR_SCAN_SORT_EXT), <, 0);
    }
    dir_scan_node_free(node);
    remove_tree(dir);
    g_free(dir);
}

static void on_read(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    *(DirScanNode**)data = dir_scan_read_finish(res);
}

/* dir_scan_read() on a worker thread, the result back in the main context */
static void test_read_async(void) {
    gchar* dir = fixture_dir();
    put(dir, "sub", NULL);
    put(dir, "sub/in.txt", "x");
    put(dir, "b.txt", "x");
    GHashTable* expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_hash_table_add(expanded, g_build_filename(dir, "sub", NULL));
    DirScanNode* node = NULL;
    dir_scan_read_async(dir, FALSE, DIR_SCAN_SORT_NAME, expanded, on_read, &node);
    g_hash_table_destroy(expanded); /* copied */
    while (node == NULL) {
        g_main_context_iteration(NULL, TRUE);
    }
    g_assert_cmpuint(node->entries->len, ==, 2);
    g_assert_cmpstr(name_at(node, 0), ==, "sub");
    g_assert_cmpstr(name_at(node, 1), ==, "b.txt");
    const DirScanNode* sub = g_ptr_array_index(node->children, 0);
    g_assert_nonnull(sub);
    g_assert_cmpstr(name_at(sub, 0), ==, "in.txt");
    dir_scan_node_free(node);
    remove_tree(dir);
    g_free(dir);
}

static void test_expanded_and_unreadable(void) {
    gchar* dir = fixture_dir();
    put(dir, "open/inner", NULL);
    put(dir, "open/inner/deep.txt", "x");
    put(dir, "closed", NULL);
    put(dir, "closed/file.txt", "f");
    put(dir, "top.txt", "t");

    GHashTable* expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_hash_table_add(expanded, g_build_filename(dir, "open", NULL));
    g_hash_table_add(expanded, g_build_filename(dir, "open", "inner", NULL));
    DirScanNode* node = dir_scan_read(dir, FALSE, DIR_SCAN_SORT_NAME, expanded, NULL);
    g_assert_cmpstr(name_at(node, 0), ==, "closed");
    g_assert_cmpstr(name_at(node, 1), ==, "open");
    g_assert_null(g_ptr_array_index(node->children, 0));
    const DirScanNode* open = g_ptr_array_index(node->children, 1);
    g_assert_nonnull(open);
    g_assert_cmpstr(name_at(open, 0), ==, "inner");
    const DirScanNode* inner = g_ptr_array_index(open->children, 0);
    g_assert_nonnull(inner);
    g_assert_cmpstr(name_at(inner, 0), ==, "deep.txt");
    dir_scan_node_free(node);
    g_hash_table_destroy(expanded);

    gchar* missing = g_build_filename(dir, "missing", NULL);
    node = dir_scan_read(missing, FALSE, DIR_SCAN_SORT_NAME, NULL, NULL);
    g_assert_true(node->unreadable);
    g_assert_null(node->entries);
    dir_scan_node_free(node);
    g_free(missing);

    remove_tree(dir);
    g_free(dir);
}

static void test_cancelled(void) {
    gchar* dir = fixture_dir();
    put(dir, "a.txt", "a");
    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    DirScanNode* node = dir_scan_read(dir, FALSE, DIR_SCAN_SORT_NAME, NULL, cancel);
    g_assert_cmpuint(node->entries->len, ==, 0);
    dir_scan_node_free(node);
    g_object_unref(cancel);
    remove_tree(dir);
    g_free(dir);
}

static void test_expand_all(void) {
    gchar* dir = fixture_dir();
    put(dir, "a/b/c", NULL);
    put(dir, "d", NULL);
    put(dir, ".h/x", NULL);
    GHashTable* out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    dir_scan_collect_expand_all(dir, FALSE, 1000, out, NULL);
    gchar* a = g_build_filename(dir, "a", NULL);
    gchar* c = g_build_filename(dir, "a", "b", "c", NULL);
    gchar* h = g_build_filename(dir, ".h", NULL);
    g_assert_true(g_hash_table_contains(out, a));
    g_assert_true(g_hash_table_contains(out, c));
    g_assert_false(g_hash_table_contains(out, h));
    g_assert_false(g_hash_table_contains(out, dir));
    g_assert_cmpuint(g_hash_table_size(out), ==, 4); /* a, a/b, a/b/c, d */
    g_hash_table_remove_all(out);

    /* The row budget stops at the first level */
    dir_scan_collect_expand_all(dir, FALSE, 1, out, NULL);
    g_assert_cmpuint(g_hash_table_size(out), ==, 0);

    g_free(a);
    g_free(c);
    g_free(h);
    g_hash_table_destroy(out);
    remove_tree(dir);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dir_scan/sorted_folders_first", test_sorted_folders_first);
    g_test_add_func("/dir_scan/compare_modes", test_compare_modes);
    g_test_add_func("/dir_scan/sort_keys", test_sort_keys);
    g_test_add_func("/dir_scan/read_async", test_read_async);
    g_test_add_func("/dir_scan/expanded_and_unreadable", test_expanded_and_unreadable);
    g_test_add_func("/dir_scan/cancelled", test_cancelled);
    g_test_add_func("/dir_scan/expand_all", test_expand_all);
    return g_test_run();
}
