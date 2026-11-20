/* Unit tests for src/formats/side_diff.c: side-by-side alignment, fillers,
 * UTF-8 intra-line spans, hunks and budget fallback. */
#include "formats/side_diff.h"

#include <string.h>

static void test_identical_and_empty(void) {
    SideDiffDoc* d = side_diff_compute("", 0, "", 0);
    g_assert_cmpuint(d->rows->len, ==, 0);
    g_assert_cmpuint(d->hunks->len, ==, 0);
    g_assert_cmpuint(d->added, ==, 0);
    g_assert_cmpuint(d->removed, ==, 0);
    side_diff_free(d);

    const gchar* s = "alpha\nbeta\ngamma\n";
    d = side_diff_compute(s, strlen(s), s, strlen(s));
    g_assert_cmpuint(d->rows->len, ==, 3);
    g_assert_cmpuint(d->hunks->len, ==, 0);
    for (guint i = 0; i < 3; i++) {
        SideDiffRow* r = g_ptr_array_index(d->rows, i);
        g_assert_cmpint(r->kind, ==, SIDE_DIFF_EQUAL);
        g_assert_cmpint(r->left_line, ==, (gint)(i + 1));
        g_assert_cmpint(r->right_line, ==, (gint)(i + 1));
    }
    side_diff_free(d);
}

static void test_add_remove_and_fillers(void) {
    const gchar* a = "one\ntwo\nthree\n";
    const gchar* b = "zero\none\nthree\nfour\n";
    SideDiffDoc* d = side_diff_compute(a, strlen(a), b, strlen(b));
    g_assert_cmpuint(d->hunks->len, ==, 3);
    g_assert_cmpuint(d->added, ==, 2);
    g_assert_cmpuint(d->removed, ==, 1);
    g_assert_cmpuint(d->rows->len, ==, 5);

    SideDiffRow* r0 = g_ptr_array_index(d->rows, 0);
    g_assert_cmpint(r0->kind, ==, SIDE_DIFF_ADDED);
    g_assert_cmpint(r0->left_line, ==, 0);
    g_assert_cmpint(r0->right_line, ==, 1);
    g_assert_cmpstr(r0->right_text, ==, "zero");

    SideDiffRow* r2 = g_ptr_array_index(d->rows, 2);
    g_assert_cmpint(r2->kind, ==, SIDE_DIFF_REMOVED);
    g_assert_cmpint(r2->left_line, ==, 2);
    g_assert_cmpint(r2->right_line, ==, 0);
    g_assert_cmpstr(r2->left_text, ==, "two");

    SideDiffRow* r4 = g_ptr_array_index(d->rows, 4);
    g_assert_cmpint(r4->kind, ==, SIDE_DIFF_ADDED);
    g_assert_cmpint(r4->left_line, ==, 0);
    g_assert_cmpint(r4->right_line, ==, 4);
    g_assert_cmpstr(r4->right_text, ==, "four");
    side_diff_free(d);
}

static void test_modified_inline_utf8(void) {
    const gchar* a = "prefix café old suffix\n";
    const gchar* b = "prefix café nouveau suffix\n";
    SideDiffDoc* d = side_diff_compute(a, strlen(a), b, strlen(b));
    g_assert_cmpuint(d->rows->len, ==, 1);
    g_assert_cmpuint(d->hunks->len, ==, 1);
    g_assert_cmpuint(d->modified, ==, 1);

    SideDiffRow* r = g_ptr_array_index(d->rows, 0);
    g_assert_cmpint(r->kind, ==, SIDE_DIFF_MODIFIED);
    g_assert_nonnull(r->left_spans);
    g_assert_nonnull(r->right_spans);
    SideDiffSpan ls = g_array_index(r->left_spans, SideDiffSpan, 0);
    SideDiffSpan rs = g_array_index(r->right_spans, SideDiffSpan, 0);
    gchar* lpart = g_strndup(r->left_text + ls.start, ls.end - ls.start);
    gchar* rpart = g_strndup(r->right_text + rs.start, rs.end - rs.start);
    g_assert_cmpstr(lpart, ==, "old");
    g_assert_cmpstr(rpart, ==, "nouveau");
    g_free(lpart);
    g_free(rpart);
    side_diff_free(d);
}

static void test_crlf_equivalence(void) {
    const gchar* a = "line1\r\nline2\r\n";
    const gchar* b = "line1\nline2\n";
    SideDiffDoc* d = side_diff_compute(a, strlen(a), b, strlen(b));
    g_assert_cmpuint(d->hunks->len, ==, 0);
    g_assert_cmpuint(d->rows->len, ==, 2);
    side_diff_free(d);
}

static void test_large_edit_fallback(void) {
    GString* a = g_string_new(NULL);
    GString* b = g_string_new(NULL);
    for (gint i = 0; i < 2500; i++) {
        g_string_append_printf(a, "old-%d\n", i);
        g_string_append_printf(b, "new-%d\n", i);
    }
    SideDiffDoc* d = side_diff_compute(a->str, a->len, b->str, b->len);
    g_assert_cmpuint(d->hunks->len, ==, 1);
    g_assert_cmpuint(d->rows->len, ==, 2500);
    g_assert_cmpuint(d->modified, ==, 2500);
    side_diff_free(d);
    g_string_free(a, TRUE);
    g_string_free(b, TRUE);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/side_diff/identical_and_empty", test_identical_and_empty);
    g_test_add_func("/side_diff/add_remove_and_fillers", test_add_remove_and_fillers);
    g_test_add_func("/side_diff/modified_inline_utf8", test_modified_inline_utf8);
    g_test_add_func("/side_diff/crlf_equivalence", test_crlf_equivalence);
    g_test_add_func("/side_diff/large_edit_fallback", test_large_edit_fallback);
    return g_test_run();
}
