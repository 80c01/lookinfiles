/* Unit tests for remote_search: the commands, and the reading of their output */
#include "search/remote_search.h"

#include <string.h>

static gboolean keep_no_txt(const gchar* rel, gpointer data) {
    (void)data;
    return !g_str_has_suffix(rel, ".txt");
}

static void test_list(void) {
    static const gchar data[] = "./a.c\0src/b.h\0\0notes.txt\0cut";
    gboolean truncated = TRUE;
    GPtrArray* files = remote_search_parse_list("/m/root", data, sizeof(data) - 1, NULL, NULL, 100, &truncated);
    g_assert_false(truncated);
    g_assert_cmpuint(files->len, ==, 3); /* the last name, without its NUL, is cut short */
    g_assert_cmpstr(files->pdata[0], ==, "/m/root/a.c");
    g_assert_cmpstr(files->pdata[1], ==, "/m/root/src/b.h");
    g_assert_cmpstr(files->pdata[2], ==, "/m/root/notes.txt");
    g_ptr_array_unref(files);

    files = remote_search_parse_list("/m/root", data, sizeof(data) - 1, keep_no_txt, NULL, 1, &truncated);
    g_assert_cmpuint(files->len, ==, 1);
    g_assert_true(truncated);
    g_ptr_array_unref(files);
}

static void test_lines(void) {
    /* grep -nH --null: path NUL line ':' text */
    static const gchar data[] = "./src/a.c\0" "3:int Foo = foo();\n"
                                "src/a.c\0" "10:food\n"
                                "notes.txt\0" "1:foo\n"
                                "b.c\0" "2:x foo\n"
                                "b.c\0" "7:cut sho";
    TextSearchOptions opts = { 0 };
    TextSearchStats stats;
    GPtrArray* refs = remote_search_parse_lines("/r", data, sizeof(data) - 1, "foo", &opts, keep_no_txt, NULL, &stats);
    g_assert_cmpuint(refs->len, ==, 4);
    CodeReference* r = refs->pdata[0];
    g_assert_cmpstr(r->path, ==, "/r/b.c");
    g_assert_cmpint(r->line, ==, 1);
    g_assert_cmpint(r->column, ==, 2);
    r = refs->pdata[1];
    g_assert_cmpstr(r->path, ==, "/r/src/a.c");
    g_assert_cmpint(r->line, ==, 2);
    g_assert_cmpint(r->column, ==, 4); /* any case: "Foo" */
    g_assert_cmpint(r->length, ==, 3);
    g_assert_cmpstr(r->text, ==, "int Foo = foo();");
    r = refs->pdata[2];
    g_assert_cmpint(r->column, ==, 10);
    r = refs->pdata[3];
    g_assert_cmpint(r->line, ==, 9);
    g_assert_cmpuint(stats.searched, ==, 2);
    g_ptr_array_unref(refs);

    /* Whole words and case: the lines grep gave are checked again */
    opts.whole_word = TRUE;
    opts.match_case = TRUE;
    refs = remote_search_parse_lines("/r", data, sizeof(data) - 1, "foo", &opts, NULL, NULL, NULL);
    g_assert_cmpuint(refs->len, ==, 3); /* not "Foo", not "food" */
    g_ptr_array_unref(refs);

    /* Fuzzy: the letters in order */
    TextSearchOptions fuzzy = { .fuzzy = TRUE };
    refs = remote_search_parse_lines("/r", data, sizeof(data) - 1, "fd", &fuzzy, NULL, NULL, NULL);
    g_assert_cmpuint(refs->len, ==, 1);
    g_ptr_array_unref(refs);
}

static void test_commands(void) {
    gchar* list = remote_search_list_command(FALSE);
    g_assert_nonnull(strstr(list, "git ls-files -z -co --exclude-standard"));
    g_assert_nonnull(strstr(list, "-name node_modules"));
    g_free(list);
    list = remote_search_list_command(TRUE);
    g_assert_null(strstr(list, "--exclude-standard"));
    g_assert_null(strstr(list, "node_modules"));
    g_free(list);

    TextSearchOptions opts = { 0 };
    gchar* cmd = remote_search_grep_command("it's", &opts, FALSE);
    g_assert_nonnull(strstr(cmd, "grep -nHI --null -F -i -e 'it'\\''s' --"));
    g_free(cmd);
    opts.match_case = TRUE;
    opts.regex = TRUE;
    cmd = remote_search_grep_command("a+b", &opts, FALSE);
    g_assert_nonnull(strstr(cmd, "grep -qP"));
    g_assert_nonnull(strstr(cmd, "--null $R -e 'a+b'"));
    g_free(cmd);
    TextSearchOptions fuzzy = { .fuzzy = TRUE };
    cmd = remote_search_grep_command("a. b", &fuzzy, FALSE);
    g_assert_nonnull(strstr(cmd, "-E -i -e 'a.*\\..*b'"));
    g_free(cmd);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/remote_search/list", test_list);
    g_test_add_func("/remote_search/lines", test_lines);
    g_test_add_func("/remote_search/commands", test_commands);
    return g_test_run();
}
