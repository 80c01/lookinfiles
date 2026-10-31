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

static void test_search(void) {
    gchar* root = g_dir_make_tmp("text_search_XXXXXX", NULL);
    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(files, write_file(root, "b.txt", "one Total\n\ttotal two\n", -1));
    g_ptr_array_add(files, write_file(root, "a/x.c", "int total; /* TOTAL */\n", -1));
    g_ptr_array_add(files, write_file(root, "bin.dat", "total\0\1\2", 8));

    /* Lower case query: any case, files in path order, binary skipped */
    gboolean truncated = TRUE;
    GPtrArray* refs = text_search_files("total", files, NULL, &truncated);
    gchar* got = list_of(root, refs);
    g_assert_cmpstr(got, ==, "a/x.c:1:4 a/x.c:1:14 b.txt:1:4 b.txt:2:1");
    g_assert_false(truncated);
    /* The line as shown: the tab became a space, the column still points at the match */
    CodeReference* last = g_ptr_array_index(refs, 3);
    g_assert_cmpstr(last->text, ==, " total two");
    g_assert_cmpint(strncmp(last->text + last->column, "total", 5), ==, 0);
    g_free(got);
    g_ptr_array_unref(refs);

    /* A capital: exact case */
    g_assert_true(text_search_exact_case("Total"));
    refs = text_search_files("Total", files, NULL, NULL);
    got = list_of(root, refs);
    g_assert_cmpstr(got, ==, "b.txt:1:4");
    g_free(got);
    g_ptr_array_unref(refs);

    /* No overlapping matches */
    g_ptr_array_set_size(files, 0);
    g_ptr_array_add(files, write_file(root, "aaa.txt", "aaaa\n", -1));
    refs = text_search_files("aa", files, NULL, NULL);
    got = list_of(root, refs);
    g_assert_cmpstr(got, ==, "aaa.txt:1:0 aaa.txt:1:2");
    g_free(got);
    g_ptr_array_unref(refs);

    refs = text_search_files("", files, NULL, NULL);
    g_assert_cmpuint(refs->len, ==, 0);
    g_ptr_array_unref(refs);

    g_ptr_array_unref(files);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/text_search/search", test_search);
    return g_test_run();
}
