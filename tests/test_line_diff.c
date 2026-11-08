/* Unit tests for src/core/line_diff.c: per-line change marks of a text against
 * its previous version. */
#include "core/line_diff.h"

#include <string.h>

/* Marks of new_text against old_text as a string, one character per line:
 * '.' unchanged, 'A' added, 'M' modified, then '^' / 'v' for a deletion
 * above / below the line. */
static gchar* marks_of(const gchar* old_text, const gchar* new_text) {
    guint n = 0;
    guint8* m = line_diff_marks(old_text, strlen(old_text), new_text, strlen(new_text), &n);
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < n; i++) {
        if (m[i] & LINE_DIFF_ADDED) {
            g_string_append_c(s, 'A');
        } else if (m[i] & LINE_DIFF_MODIFIED) {
            g_string_append_c(s, 'M');
        } else {
            g_string_append_c(s, '.');
        }
        if (m[i] & LINE_DIFF_DELETED_ABOVE) {
            g_string_append_c(s, '^');
        }
        if (m[i] & LINE_DIFF_DELETED_BELOW) {
            g_string_append_c(s, 'v');
        }
    }
    g_free(m);
    return g_string_free(s, FALSE);
}

#define ASSERT_MARKS(old_text, new_text, expected) \
    do {                                           \
        gchar* got = marks_of(old_text, new_text); \
        g_assert_cmpstr(got, ==, expected);        \
        g_free(got);                               \
    } while (0)

static void test_count_lines(void) {
    g_assert_cmpuint(line_diff_count_lines("", 0), ==, 0);
    g_assert_cmpuint(line_diff_count_lines("a", 1), ==, 1);
    g_assert_cmpuint(line_diff_count_lines("a\n", 2), ==, 1);
    g_assert_cmpuint(line_diff_count_lines("a\nb", 3), ==, 2);
    g_assert_cmpuint(line_diff_count_lines("\n\n", 2), ==, 2);
}

static void test_unchanged(void) {
    ASSERT_MARKS("a\nb\nc\n", "a\nb\nc\n", "...");
    ASSERT_MARKS("a\nb\nc\n", "a\nb\nc", "..."); /* end of line not compared */
}

static void test_added(void) {
    ASSERT_MARKS("a\nc\n", "a\nb\nc\n", ".A.");
    ASSERT_MARKS("a\n", "x\ny\na\n", "AA.");
    ASSERT_MARKS("a\n", "a\nx\n", ".A");
    ASSERT_MARKS("", "x\ny\n", "AA");
}

static void test_modified(void) {
    ASSERT_MARKS("a\nb\nc\n", "a\nB\nc\n", ".M.");
    ASSERT_MARKS("a\nb\nc\n", "a\nB\nB2\nc\n", ".MM.");
}

static void test_deleted(void) {
    ASSERT_MARKS("a\nb\nc\n", "a\nc\n", "..^");
    ASSERT_MARKS("a\nb\nc\n", "b\nc\n", ".^.");
    ASSERT_MARKS("a\nb\nc\n", "a\nb\n", "..v");
    ASSERT_MARKS("a\nb\n", "", "");
}

static void test_several_hunks(void) {
    ASSERT_MARKS("1\n2\n3\n4\n5\n6\n7\n", "1\nX\n3\n4\n6\n7\nY\n", ".M...^.A");
}

static void test_large_change_budget(void) {
    /* Far beyond the edit budget: the changed region is one modified block */
    GString* old_text = g_string_new("head\n");
    GString* new_text = g_string_new("head\n");
    for (gint i = 0; i < 3000; i++) {
        g_string_append_printf(old_text, "old %d\n", i);
        g_string_append_printf(new_text, "new %d\n", i);
    }
    g_string_append(old_text, "tail\n");
    g_string_append(new_text, "tail\n");
    guint n = 0;
    guint8* m = line_diff_marks(old_text->str, old_text->len, new_text->str, new_text->len, &n);
    g_assert_cmpuint(n, ==, 3002);
    g_assert_cmpuint(m[0], ==, 0);
    g_assert_cmpuint(m[1], ==, LINE_DIFF_MODIFIED);
    g_assert_cmpuint(m[3000], ==, LINE_DIFF_MODIFIED);
    g_assert_cmpuint(m[3001], ==, 0);
    g_free(m);
    g_string_free(old_text, TRUE);
    g_string_free(new_text, TRUE);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/line_diff/count_lines", test_count_lines);
    g_test_add_func("/line_diff/unchanged", test_unchanged);
    g_test_add_func("/line_diff/added", test_added);
    g_test_add_func("/line_diff/modified", test_modified);
    g_test_add_func("/line_diff/deleted", test_deleted);
    g_test_add_func("/line_diff/several_hunks", test_several_hunks);
    g_test_add_func("/line_diff/large_change_budget", test_large_change_budget);
    return g_test_run();
}
