/* Unit tests for deep_search_remote: the find command, its records, the matches of grep's lines */
#include "search/deep_search_remote.h"

#include <string.h>

static void test_command(void) {
    DeepQuery q;
    deep_query_init(&q);
    gchar* cmd = deep_remote_find_command(&q);
    g_assert_true(g_str_has_prefix(cmd, "printf 'r 0 0 .\\0'; find \"$PWD\" -mindepth 1 \\("));
    g_assert_nonnull(strstr(cmd, "-o -name '.*'"));
    g_assert_null(strstr(cmd, "-maxdepth"));
    g_free(cmd);
    q.hidden = TRUE;
    q.max_depth = 2;
    q.one_filesystem = TRUE;
    cmd = deep_remote_find_command(&q);
    g_assert_nonnull(strstr(cmd, "-mindepth 1 -maxdepth 2 -xdev"));
    g_assert_null(strstr(cmd, "-name '.*'"));
    g_free(cmd);
    deep_query_clear(&q);
}

static void test_entry(void) {
    DeepRemoteEntry e;
    g_assert_true(deep_remote_parse_entry("f 14 1795343863.2188237180 src/a b.c", &e));
    g_assert_cmpint(e.type, ==, 'f');
    g_assert_cmpuint(e.size, ==, 14);
    g_assert_cmpint(e.mtime, ==, 1795343863);
    g_assert_cmpstr(e.rel, ==, "src/a b.c"); /* spaces in names kept */
    g_assert_true(deep_remote_parse_entry("r 0 0 .", &e));
    g_assert_cmpint(e.type, ==, 'r');
    g_assert_false(deep_remote_parse_entry("", &e));
    g_assert_false(deep_remote_parse_entry("f x", &e));
    g_assert_false(deep_remote_parse_entry("f 1 2", &e));
}

static void test_matches(void) {
    static const gchar out[] = "src/a.c\0" "2:foo and Foo\n"
                               "src/a.c\0" "9:food\n"
                               "b.txt\0" "1:nothing here\n"
                               "c.txt\0" "4:x foo";
    TextSearchOptions opts = { .whole_word = TRUE };
    GHashTable* m = deep_remote_parse_matches(out, sizeof(out) - 1, "foo", &opts, 9999);
    g_assert_cmpuint(g_hash_table_size(m), ==, 1); /* b.txt: no match; c.txt: cut short */
    DeepRemoteMatch* a = g_hash_table_lookup(m, "src/a.c");
    g_assert_cmpuint(a->count, ==, 2); /* foo, Foo; not food */
    g_assert_false(a->more);
    g_assert_cmpint(a->line, ==, 1);
    g_assert_cmpint(a->column, ==, 0);
    g_assert_cmpstr(a->text, ==, "foo and Foo");
    g_hash_table_destroy(m);

    m = deep_remote_parse_matches(out, sizeof(out) - 1, "foo", &opts, 1);
    a = g_hash_table_lookup(m, "src/a.c");
    g_assert_cmpuint(a->count, ==, 1);
    g_assert_true(a->more);
    g_hash_table_destroy(m);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/deep_search_remote/command", test_command);
    g_test_add_func("/deep_search_remote/entry", test_entry);
    g_test_add_func("/deep_search_remote/matches", test_matches);
    return g_test_run();
}
