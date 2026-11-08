/* Unit tests for src/preview/preview_cache.c: bounded most-recently-used cache of
 * previews, with file stamps. */
#include "preview/preview_cache.h"

#include <glib/gstdio.h>

static gint freed;

static void count_free(gpointer value) {
    freed++;
    g_free(value);
}

static FileStamp stamp_of(gint64 size) {
    FileStamp s = { size, size * 10, 1 };
    return s;
}

static void test_lookup_and_replace(void) {
    freed = 0;
    PreviewCache* cache = preview_cache_new(4, 1000, count_free);
    FileStamp s1 = stamp_of(1);
    preview_cache_insert(cache, "a", &s1, g_strdup("A"), 10);
    FileStamp got = { 0, 0, 0 };
    g_assert_cmpstr(preview_cache_lookup(cache, "a", &got), ==, "A");
    g_assert_true(file_stamp_equal(&got, &s1));
    g_assert_null(preview_cache_lookup(cache, "b", NULL));

    FileStamp s2 = stamp_of(2);
    preview_cache_insert(cache, "a", &s2, g_strdup("A2"), 20);
    g_assert_cmpint(freed, ==, 1);
    g_assert_cmpstr(preview_cache_lookup(cache, "a", &got), ==, "A2");
    g_assert_true(file_stamp_equal(&got, &s2));
    g_assert_cmpuint(preview_cache_count(cache), ==, 1);
    g_assert_cmpuint(preview_cache_bytes(cache), ==, 20);

    preview_cache_remove(cache, "a");
    g_assert_cmpint(freed, ==, 2);
    g_assert_cmpuint(preview_cache_count(cache), ==, 0);
    g_assert_cmpuint(preview_cache_bytes(cache), ==, 0);
    preview_cache_free(cache);
}

/* Over the entry count, the least recently used goes (a lookup counts as a use) */
static void test_evicts_least_recently_used(void) {
    freed = 0;
    PreviewCache* cache = preview_cache_new(2, 1000, count_free);
    FileStamp s = stamp_of(1);
    preview_cache_insert(cache, "a", &s, g_strdup("A"), 1);
    preview_cache_insert(cache, "b", &s, g_strdup("B"), 1);
    g_assert_nonnull(preview_cache_lookup(cache, "a", NULL));
    preview_cache_insert(cache, "c", &s, g_strdup("C"), 1);
    g_assert_cmpint(freed, ==, 1);
    g_assert_null(preview_cache_lookup(cache, "b", NULL));
    g_assert_nonnull(preview_cache_lookup(cache, "a", NULL));
    g_assert_nonnull(preview_cache_lookup(cache, "c", NULL));
    preview_cache_free(cache);
    g_assert_cmpint(freed, ==, 3);
}

/* Over the size budget, old entries go; a value larger than the budget is not kept */
static void test_size_budget(void) {
    freed = 0;
    PreviewCache* cache = preview_cache_new(10, 100, count_free);
    FileStamp s = stamp_of(1);
    preview_cache_insert(cache, "a", &s, g_strdup("A"), 60);
    preview_cache_insert(cache, "b", &s, g_strdup("B"), 30);
    preview_cache_insert(cache, "c", &s, g_strdup("C"), 30);
    g_assert_null(preview_cache_lookup(cache, "a", NULL));
    g_assert_cmpuint(preview_cache_bytes(cache), ==, 60);
    preview_cache_insert(cache, "big", &s, g_strdup("X"), 101);
    g_assert_null(preview_cache_lookup(cache, "big", NULL));
    g_assert_cmpuint(preview_cache_count(cache), ==, 2);
    g_assert_cmpint(freed, ==, 2);
    preview_cache_free(cache);
}

/* A rewrite changes the stamp; a missing file has none */
static void test_file_stamp(void) {
    gchar* dir = g_dir_make_tmp("preview-cache-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "f.txt", NULL);
    g_assert_true(g_file_set_contents(path, "one", -1, NULL));
    FileStamp a, b;
    g_assert_true(file_stamp_read(path, &a));
    g_assert_cmpint(a.size, ==, 3);
    g_assert_true(file_stamp_read(path, &b));
    g_assert_true(file_stamp_equal(&a, &b));
    g_assert_true(g_file_set_contents(path, "three", -1, NULL));
    g_assert_true(file_stamp_read(path, &b));
    g_assert_false(file_stamp_equal(&a, &b));
    g_unlink(path);
    g_assert_false(file_stamp_read(path, &b));
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_cache/lookup_and_replace", test_lookup_and_replace);
    g_test_add_func("/preview_cache/evicts_least_recently_used", test_evicts_least_recently_used);
    g_test_add_func("/preview_cache/size_budget", test_size_budget);
    g_test_add_func("/preview_cache/file_stamp", test_file_stamp);
    return g_test_run();
}
