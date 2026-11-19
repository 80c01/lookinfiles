/* Unit tests for src/search/deep_search.c: a deep search below a folder. */
#include "search/deep_search.h"

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static void write_file(const gchar* root, const gchar* rel, const gchar* text) {
    gchar* path = g_build_filename(root, rel, NULL);
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(path);
}

static void set_mtime(const gchar* root, const gchar* rel, gint64 when) {
    gchar* path = g_build_filename(root, rel, NULL);
    GFile* f = g_file_new_for_path(path);
    GFileInfo* info = g_file_info_new();
    g_file_info_set_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED, (guint64)when);
    g_assert_true(g_file_set_attributes_from_info(f, info, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
    g_object_unref(info);
    g_object_unref(f);
    g_free(path);
}

/*  root/
 *    a.c          "int needle;"
 *    b.txt        "nothing"
 *    .hidden.c    "needle"
 *    src/
 *      main.c     "needle\nneedle"
 *      deep/x.c   "no"
 *    node_modules/m.c "needle"
 *    big.bin      NUL bytes then needle */
static gchar* make_tree(void) {
    gchar* root = g_dir_make_tmp("deep-search-XXXXXX", NULL);
    write_file(root, "a.c", "int needle;\n");
    write_file(root, "b.txt", "nothing\n");
    write_file(root, ".hidden.c", "needle\n");
    write_file(root, "src/main.c", "needle\nneedle\n");
    write_file(root, "src/deep/x.c", "no\n");
    write_file(root, "node_modules/m.c", "needle\n");
    gchar* bin = g_build_filename(root, "big.bin", NULL);
    g_assert_true(g_file_set_contents(bin, "\0\0needle", 8, NULL));
    g_free(bin);
    return root;
}

static void remove_tree(const gchar* path) {
    GDir* d = g_dir_open(path, 0, NULL);
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR) && !g_file_test(child, G_FILE_TEST_IS_SYMLINK)) {
                remove_tree(child);
            } else {
                g_unlink(child);
            }
            g_free(child);
        }
        g_dir_close(d);
    }
    g_rmdir(path);
}

/* The entries found, relative and in order, joined by spaces ("src/" for a folder) */
static gchar* run(const gchar* root, const DeepQuery* q, DeepSearchProgress* progress) {
    DeepSearch* s = deep_search_new(root, q);
    g_assert_nonnull(s);
    deep_search_run(s, NULL);
    GPtrArray* hits = deep_search_take(s, progress);
    GString* out = g_string_new(NULL);
    for (guint i = 0; hits != NULL && i < hits->len; i++) {
        DeepHit* h = g_ptr_array_index(hits, i);
        g_string_append_printf(out, "%s%s%s", i > 0 ? " " : "", h->path + strlen(root) + 1, h->is_dir ? "/" : "");
    }
    if (hits != NULL) g_ptr_array_unref(hits);
    g_assert_null(deep_search_take(s, NULL)); /* taken */
    deep_search_free(s);
    return g_string_free(out, FALSE);
}

static void test_all(void) {
    gchar* root = make_tree();
    DeepQuery q;
    deep_query_init(&q);
    DeepSearchProgress p;
    gchar* got = run(root, &q, &p);
    /* Each folder's entries in name order, then what is below them */
    g_assert_cmpstr(got, ==, "a.c b.txt big.bin node_modules/ src/ node_modules/m.c src/deep/ src/main.c src/deep/x.c");
    g_assert_cmpint(p.state, ==, DEEP_SEARCH_DONE);
    g_assert_cmpuint(p.found, ==, 9);
    g_assert_cmpuint(p.dirs, ==, 4);
    g_assert_null(p.current);
    deep_search_progress_clear(&p);
    g_free(got);
    /* Hidden names too */
    q.hidden = TRUE;
    got = run(root, &q, NULL);
    g_assert_true(g_str_has_prefix(got, ".hidden.c a.c"));
    g_free(got);
    deep_query_clear(&q);
    remove_tree(root);
    g_free(root);
}

static void test_criteria(void) {
    gchar* root = make_tree();
    DeepQuery q;
    deep_query_init(&q);
    q.extensions = g_strdup("c");
    q.exclude = g_strdup("node_modules");
    gchar* got = run(root, &q, NULL);
    g_assert_cmpstr(got, ==, "a.c src/main.c src/deep/x.c");
    g_free(got);
    /* Depth 1: the entries of the folder only; 2: one level below */
    q.max_depth = 1;
    got = run(root, &q, NULL);
    g_assert_cmpstr(got, ==, "a.c");
    g_free(got);
    q.max_depth = 2;
    got = run(root, &q, NULL);
    g_assert_cmpstr(got, ==, "a.c src/main.c");
    g_free(got);
    q.max_depth = 0;
    /* Folders only */
    deep_query_clear(&q);
    q.kind = DEEP_KIND_FOLDERS;
    got = run(root, &q, NULL);
    g_assert_cmpstr(got, ==, "node_modules/ src/ src/deep/");
    g_free(got);
    deep_query_clear(&q);
    /* Dates */
    set_mtime(root, "b.txt", 1000000000);
    q.before = 1000000001;
    got = run(root, &q, NULL);
    g_assert_cmpstr(got, ==, "b.txt");
    g_free(got);
    deep_query_clear(&q);
    remove_tree(root);
    g_free(root);
}

static void test_content(void) {
    gchar* root = make_tree();
    DeepQuery q;
    deep_query_init(&q);
    q.content = g_strdup("NEEDLE");
    DeepSearchProgress p;
    DeepSearch* s = deep_search_new(root, &q);
    deep_search_run(s, NULL);
    GPtrArray* hits = deep_search_take(s, &p);
    g_assert_nonnull(hits);
    g_assert_cmpuint(hits->len, ==, 3); /* a.c, node_modules/m.c, src/main.c: not the binary file */
    DeepHit* a = g_ptr_array_index(hits, 0);
    g_assert_true(g_str_has_suffix(a->path, "/a.c"));
    g_assert_cmpuint(a->matches, ==, 1);
    g_assert_cmpint(a->line, ==, 0);
    g_assert_cmpint(a->column, ==, 4);
    g_assert_cmpstr(a->text, ==, "int needle;");
    DeepHit* m = g_ptr_array_index(hits, 2);
    g_assert_true(g_str_has_suffix(m->path, "/src/main.c"));
    g_assert_cmpuint(m->matches, ==, 2);
    g_assert_cmpuint(p.binary, ==, 1);
    g_assert_cmpuint(p.searched, ==, 5); /* the other files */
    g_assert_cmpuint(p.found, ==, 3);
    g_assert_cmpuint(p.bytes, >, 0);
    deep_search_progress_clear(&p);
    g_ptr_array_unref(hits);
    deep_search_free(s);
    /* Match the case */
    q.content_opts.match_case = TRUE;
    gchar* got = run(root, &q, NULL);
    g_assert_cmpstr(got, ==, "");
    g_free(got);
    deep_query_clear(&q);
    remove_tree(root);
    g_free(root);
}

static void test_links_and_errors(void) {
    gchar* root = make_tree();
    DeepQuery q;
    deep_query_init(&q);
    q.name = g_strdup("loop");
#ifdef G_OS_UNIX
    /* A link to a folder above: an entry, never entered */
    gchar* link = g_build_filename(root, "src", "loop", NULL);
    g_assert_cmpint(symlink(root, link), ==, 0);
    gchar* got = run(root, &q, NULL);
    g_assert_cmpstr(got, ==, "src/loop");
    g_free(got);
    g_unlink(link);
    g_free(link);
#endif
    deep_query_clear(&q);
    /* A folder that is not there */
    gchar* missing = g_build_filename(root, "missing", NULL);
    DeepSearch* s = deep_search_new(missing, &q);
    deep_search_run(s, NULL);
    DeepSearchProgress p;
    g_assert_null(deep_search_take(s, &p));
    g_assert_cmpint(p.state, ==, DEEP_SEARCH_NO_FOLDER);
    deep_search_progress_clear(&p);
    deep_search_free(s);
    g_free(missing);
    /* A query that cannot run */
    q.content = g_strdup("(");
    q.content_opts.regex = TRUE;
    g_assert_null(deep_search_new(root, &q));
    deep_query_clear(&q);
    remove_tree(root);
    g_free(root);
}

static void test_cancel(void) {
    gchar* root = make_tree();
    DeepQuery q;
    deep_query_init(&q);
    GCancellable* cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    DeepSearch* s = deep_search_new(root, &q);
    deep_search_run(s, cancel);
    DeepSearchProgress p;
    GPtrArray* hits = deep_search_take(s, &p);
    g_assert_null(hits);
    g_assert_cmpint(p.state, ==, DEEP_SEARCH_STOPPED);
    deep_search_progress_clear(&p);
    deep_search_free(s);
    g_object_unref(cancel);
    deep_query_clear(&q);
    remove_tree(root);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/deep_search/all", test_all);
    g_test_add_func("/deep_search/criteria", test_criteria);
    g_test_add_func("/deep_search/content", test_content);
    g_test_add_func("/deep_search/links_and_errors", test_links_and_errors);
    g_test_add_func("/deep_search/cancel", test_cancel);
    return g_test_run();
}
