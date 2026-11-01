/* Unit tests for src/text_search.c: search the files of a project. */
#include "text_search.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar* write_file(const gchar* root, const gchar* rel, const gchar* text, gssize len) {
    gchar* path = g_build_filename(root, rel, NULL);
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_assert_true(g_file_set_contents(path, text, len, NULL));
    return path;
}

/* Results as "rel:line:column" joined by spaces */
static gchar* list_of(const gchar* root, GPtrArray* refs) {
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < refs->len; i++) {
        CodeReference* r = g_ptr_array_index(refs, i);
        g_string_append_printf(s, "%s%s:%d:%d", i > 0 ? " " : "", r->path + strlen(root) + 1, r->line + 1,
                               r->column);
    }
    return g_string_free(s, FALSE);
}

static const TextSearchOptions ANY = {FALSE, FALSE, FALSE, FALSE};

/* Results of query in files as "rel:line:column+length" */
static gchar* search(const gchar* root, GPtrArray* files, const gchar* query, const TextSearchOptions* opts) {
    GPtrArray* refs = text_search_files(query, opts, files, NULL, NULL);
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < refs->len; i++) {
        CodeReference* r = g_ptr_array_index(refs, i);
        g_string_append_printf(s, "%s%s:%d:%d+%d", i > 0 ? " " : "", r->path + strlen(root) + 1, r->line + 1,
                               r->column, r->length);
    }
    g_ptr_array_unref(refs);
    return g_string_free(s, FALSE);
}

#define ASSERT_SEARCH(query, opts, expected)                       \
    do {                                                           \
        gchar* got_ = search(root, files, (query), (opts));        \
        g_assert_cmpstr(got_, ==, (expected));                     \
        g_free(got_);                                              \
    } while (0)

static void test_search(void) {
    gchar* root = g_dir_make_tmp("text_search_XXXXXX", NULL);
    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(files, write_file(root, "b.txt", "one Total\n\ttotal two\n", -1));
    g_ptr_array_add(files, write_file(root, "a/x.c", "int total; /* TOTAL */\n", -1));
    g_ptr_array_add(files, write_file(root, "bin.dat", "total\0\1\2", 8));

    /* Any case by default, even with a capital; files in path order, binary skipped */
    TextSearchStats stats = {TEXT_SEARCH_MAX_BYTES_HIT, 99, 99, 99};
    GPtrArray* refs = text_search_files("total", &ANY, files, NULL, &stats);
    gchar* got = list_of(root, refs);
    g_assert_cmpstr(got, ==, "a/x.c:1:4 a/x.c:1:14 b.txt:1:4 b.txt:2:1");
    g_assert_cmpint(stats.stop, ==, TEXT_SEARCH_DONE);
    g_assert_cmpuint(stats.searched, ==, 2); /* not the binary file */
    g_assert_cmpuint(stats.skipped_large, ==, 0);
    g_assert_cmpuint(stats.too_complex, ==, 0);
    /* The line as shown: the tab became a space, the column still points at the match */
    CodeReference* last = g_ptr_array_index(refs, 3);
    g_assert_cmpstr(last->text, ==, " total two");
    g_assert_cmpint(strncmp(last->text + last->column, "total", 5), ==, 0);
    g_assert_cmpint(last->length, ==, 5);
    g_free(got);
    g_ptr_array_unref(refs);
    ASSERT_SEARCH("Total", &ANY, "a/x.c:1:4+5 a/x.c:1:14+5 b.txt:1:4+5 b.txt:2:1+5");

    /* Match case */
    TextSearchOptions cased = {TRUE, FALSE, FALSE, FALSE};
    ASSERT_SEARCH("Total", &cased, "b.txt:1:4+5");

    /* No overlapping matches */
    g_ptr_array_set_size(files, 0);
    g_ptr_array_add(files, write_file(root, "aaa.txt", "aaaa\n", -1));
    ASSERT_SEARCH("aa", &ANY, "aaa.txt:1:0+2 aaa.txt:1:2+2");
    ASSERT_SEARCH("", &ANY, "");

    /* A file over TEXT_SEARCH_MAX_FILE_BYTES is skipped and counted */
    gsize big_len = TEXT_SEARCH_MAX_FILE_BYTES + 1;
    gchar* big = g_malloc(big_len);
    memset(big, 'a', big_len);
    g_ptr_array_add(files, write_file(root, "big.txt", big, (gssize)big_len));
    g_free(big);
    refs = text_search_files("aa", &ANY, files, NULL, &stats);
    g_assert_cmpuint(refs->len, ==, 2);
    g_assert_cmpuint(stats.searched, ==, 1);
    g_assert_cmpuint(stats.skipped_large, ==, 1);
    g_ptr_array_unref(refs);

    g_ptr_array_unref(files);
    g_free(root);
}

static void test_max_results(void) {
    gchar* root = g_dir_make_tmp("text_search_XXXXXX", NULL);
    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    GString* many = g_string_new(NULL);
    for (guint i = 0; i < TEXT_SEARCH_MAX_RESULTS + 10; i++) g_string_append(many, "x\n");
    g_ptr_array_add(files, write_file(root, "a.txt", many->str, (gssize)many->len));
    g_ptr_array_add(files, write_file(root, "b.txt", "x\n", -1));
    g_string_free(many, TRUE);
    TextSearchStats stats;
    GPtrArray* refs = text_search_files("x", &ANY, files, NULL, &stats);
    g_assert_cmpuint(refs->len, ==, TEXT_SEARCH_MAX_RESULTS);
    g_assert_cmpint(stats.stop, ==, TEXT_SEARCH_MAX_RESULTS_HIT);
    g_ptr_array_unref(refs);
    g_ptr_array_unref(files);
    g_free(root);
}

static void test_options(void) {
    gchar* root = g_dir_make_tmp("text_search_XXXXXX", NULL);
    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(files, write_file(root, "w.txt", "foo foobar barfoo foo_x Foo\nÉTÉ été\r\n", -1));

    /* Whole word, with and without the case */
    TextSearchOptions word = {FALSE, TRUE, FALSE, FALSE};
    ASSERT_SEARCH("foo", &word, "w.txt:1:0+3 w.txt:1:24+3");
    TextSearchOptions word_cased = {TRUE, TRUE, FALSE, FALSE};
    ASSERT_SEARCH("foo", &word_cased, "w.txt:1:0+3");

    /* Any case beyond ASCII */
    ASSERT_SEARCH("été", &ANY, "w.txt:2:0+5 w.txt:2:6+5");

    /* Regular expression: the matched length, ^ and $ by line (CRLF too) */
    TextSearchOptions re = {FALSE, FALSE, TRUE, FALSE};
    ASSERT_SEARCH("bar\\w*", &re, "w.txt:1:7+3 w.txt:1:11+6");
    ASSERT_SEARCH("^été|été$", &re, "w.txt:2:0+5 w.txt:2:6+5");
    ASSERT_SEARCH("x*", &re, "w.txt:1:22+1"); /* empty matches dropped */
    /* A regex special character is literal without the option */
    ASSERT_SEARCH("o_.", &ANY, "");
    ASSERT_SEARCH("o_.", &re, "w.txt:1:20+3");

    /* Invalid regular expression: a reason, no results */
    gchar* why = text_search_check("foo(", &re);
    g_assert_nonnull(why);
    g_free(why);
    g_assert_null(text_search_check("foo(", &ANY));
    ASSERT_SEARCH("foo(", &re, "");
    ASSERT_SEARCH("foo(", &ANY, "");

    gchar* how = text_search_describe(&word_cased);
    g_assert_cmpstr(how, ==, "case sensitive, whole word");
    g_free(how);
    how = text_search_describe(&ANY);
    g_assert_cmpstr(how, ==, "any case");
    g_free(how);

    g_ptr_array_unref(files);
    g_free(root);
}

static void test_fuzzy(void) {
    gchar* root = g_dir_make_tmp("text_search_XXXXXX", NULL);
    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(files, write_file(root, "f.c", "parse(path, size)\nno match\n#include \"editor_vim.h\"\n", -1));

    /* The shortest span of each line, spaces in the query ignored */
    TextSearchOptions fuzzy = {FALSE, FALSE, FALSE, TRUE};
    ASSERT_SEARCH("ps", &fuzzy, "f.c:1:0+4");
    ASSERT_SEARCH("ed vim", &fuzzy, "f.c:3:10+10");
    ASSERT_SEARCH("EDVIM", &fuzzy, "f.c:3:10+10");
    /* Letters on one line only */
    ASSERT_SEARCH("zm", &fuzzy, "");
    /* Match case */
    TextSearchOptions fuzzy_cased = {TRUE, FALSE, FALSE, TRUE};
    ASSERT_SEARCH("EDVIM", &fuzzy_cased, "");
    /* Whole word and regex are ignored */
    TextSearchOptions all = {FALSE, TRUE, TRUE, TRUE};
    ASSERT_SEARCH("ps", &all, "f.c:1:0+4");
    ASSERT_SEARCH("   ", &fuzzy, "");
    gchar* how = text_search_describe(&all);
    g_assert_cmpstr(how, ==, "any case, letters in order");
    g_free(how);

    g_ptr_array_unref(files);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/text_search/search", test_search);
    g_test_add_func("/text_search/options", test_options);
    g_test_add_func("/text_search/max_results", test_max_results);
    g_test_add_func("/text_search/fuzzy", test_fuzzy);
    return g_test_run();
}
