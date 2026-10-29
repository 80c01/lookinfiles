/* Unit tests for src/dir_summary.c: directory overview totals, sorting,
 * relative dates and column padding. */
#include "dir_summary.h"

#include <string.h>

static const gchar* name_at(DirSummary* s, guint i) {
    return g_array_index(s->entries, DirSummaryEntry, i).name;
}

static void test_totals(void) {
    DirSummary* s = dir_summary_new();
    dir_summary_add(s, "src", TRUE, 4096, 100);
    dir_summary_add(s, "a.c", FALSE, 1000, 300);
    dir_summary_add(s, "b.h", FALSE, 24, 200);
    g_assert_cmpuint(s->n_dirs, ==, 1);
    g_assert_cmpuint(s->n_files, ==, 2);
    g_assert_cmpuint(s->total_size, ==, 1024); /* directory sizes are not counted */
    g_assert_cmpint(s->newest_mtime, ==, 300);
    g_assert_false(s->truncated);
    g_assert_cmpuint(g_array_index(s->entries, DirSummaryEntry, 0).size, ==, 0);
    dir_summary_free(s);
}

static void test_limit(void) {
    DirSummary* s = dir_summary_new();
    for (guint i = 0; i < DIR_SUMMARY_SCAN_LIMIT; i++) {
        g_assert_true(dir_summary_add(s, "f", FALSE, 1, 1));
    }
    g_assert_false(s->truncated);
    g_assert_false(dir_summary_add(s, "extra", FALSE, 1, 1));
    g_assert_true(s->truncated);
    g_assert_cmpuint(s->entries->len, ==, DIR_SUMMARY_SCAN_LIMIT);
    dir_summary_free(s);
}

static DirSummary* sample(void) {
    DirSummary* s = dir_summary_new();
    dir_summary_add(s, "b.txt", FALSE, 10, 50);
    dir_summary_add(s, "zdir", TRUE, 0, 10);
    dir_summary_add(s, "a.md", FALSE, 300, 20);
    dir_summary_add(s, "adir", TRUE, 0, 90);
    dir_summary_add(s, "c.c", FALSE, 20, 80);
    return s;
}

static void test_sort(void) {
    DirSummary* s = sample();
    dir_summary_sort(s, DIR_SUMMARY_SORT_NAME);
    g_assert_cmpstr(name_at(s, 0), ==, "adir");
    g_assert_cmpstr(name_at(s, 1), ==, "zdir");
    g_assert_cmpstr(name_at(s, 2), ==, "a.md");
    g_assert_cmpstr(name_at(s, 4), ==, "c.c");

    dir_summary_sort(s, DIR_SUMMARY_SORT_DATE);
    g_assert_cmpstr(name_at(s, 0), ==, "adir"); /* directories stay first */
    g_assert_cmpstr(name_at(s, 2), ==, "c.c");
    g_assert_cmpstr(name_at(s, 4), ==, "a.md");

    dir_summary_sort(s, DIR_SUMMARY_SORT_SIZE);
    g_assert_cmpstr(name_at(s, 2), ==, "a.md");
    g_assert_cmpstr(name_at(s, 4), ==, "b.txt");

    dir_summary_sort(s, DIR_SUMMARY_SORT_EXT);
    g_assert_cmpstr(name_at(s, 2), ==, "c.c");
    g_assert_cmpstr(name_at(s, 3), ==, "a.md");
    g_assert_cmpstr(name_at(s, 4), ==, "b.txt");
    dir_summary_free(s);
}

static void check_age(gint64 delta, const gchar* expected) {
    gint64 now = 2000000000;
    gchar* text = dir_summary_format_age(now - delta, now);
    g_assert_cmpstr(text, ==, expected);
    g_free(text);
}

static void test_age(void) {
    check_age(5, "just now");
    check_age(-30, "just now");
    check_age(5 * 60, "5 min ago");
    check_age(3 * 3600 + 10, "3 h ago");
    check_age(30 * 3600, "yesterday");
    check_age(4 * 86400, "4 days ago");
    gchar* empty = dir_summary_format_age(0, 1000);
    g_assert_cmpstr(empty, ==, "");
    g_free(empty);
    gchar* date = dir_summary_format_age(1000000000, 2000000000);
    g_assert_cmpint(strlen(date), ==, 10); /* YYYY-MM-DD */
    g_assert_true(g_str_has_prefix(date, "2001-09-"));
    g_free(date);
}

static void test_pad(void) {
    gchar* t = dir_summary_pad("ab", 4, FALSE);
    g_assert_cmpstr(t, ==, "ab  ");
    g_free(t);
    t = dir_summary_pad("ab", 4, TRUE);
    g_assert_cmpstr(t, ==, "  ab");
    g_free(t);
    t = dir_summary_pad("abcdef", 4, FALSE);
    g_assert_cmpstr(t, ==, "abc…");
    g_free(t);
    t = dir_summary_pad("éèàù", 4, FALSE); /* counts characters, not bytes */
    g_assert_cmpstr(t, ==, "éèàù");
    g_free(t);
    t = dir_summary_pad(NULL, 2, FALSE);
    g_assert_cmpstr(t, ==, "  ");
    g_free(t);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dir_summary/totals", test_totals);
    g_test_add_func("/dir_summary/limit", test_limit);
    g_test_add_func("/dir_summary/sort", test_sort);
    g_test_add_func("/dir_summary/age", test_age);
    g_test_add_func("/dir_summary/pad", test_pad);
    return g_test_run();
}
