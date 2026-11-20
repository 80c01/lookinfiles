/* Unit tests for src/code/lsp_edit.c */
#include "code/lsp_edit.h"

#include <string.h>

static LspTextEdit* make_edit(gint sl, gint sc, gint el, gint ec, const gchar* text) {
    LspTextEdit* e = g_new0(LspTextEdit, 1);
    e->start_line = sl;
    e->start_col = sc;
    e->end_line = el;
    e->end_col = ec;
    e->new_text = g_strdup(text);
    return e;
}

static void test_apply_multiple(void) {
    const gchar* src = "int foo(int x) {\n    return foo(x) + foo(1);\n}\n";
    GPtrArray* edits = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_text_edit_free);
    /* Deliberately unsorted order */
    g_ptr_array_add(edits, make_edit(1, 20, 1, 23, "bar_fn"));
    g_ptr_array_add(edits, make_edit(0, 4, 0, 7, "bar_fn"));
    g_ptr_array_add(edits, make_edit(1, 11, 1, 14, "bar_fn"));

    GError* err = NULL;
    gchar* out = lsp_edit_apply(src, edits, TRUE, &err);
    g_assert_no_error(err);
    g_assert_cmpstr(out, ==, "int bar_fn(int x) {\n    return bar_fn(x) + bar_fn(1);\n}\n");
    g_free(out);
    g_ptr_array_unref(edits);
}

static void test_apply_utf16_emoji(void) {
    /* "a😀b" => 'a'(1) + '😀'(2 UTF-16 units, 4 UTF-8 bytes) + 'b'(1) */
    const gchar* src = "a\xf0\x9f\x98\x80" "b\n";
    GPtrArray* edits = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_text_edit_free);
    g_ptr_array_add(edits, make_edit(0, 3, 0, 4, "Z"));

    GError* err = NULL;
    gchar* out = lsp_edit_apply(src, edits, FALSE, &err);
    g_assert_no_error(err);
    g_assert_cmpstr(out, ==, "a\xf0\x9f\x98\x80" "Z\n");
    g_free(out);
    g_ptr_array_unref(edits);
}

static void test_apply_overlap_rejected(void) {
    const gchar* src = "abcdef\n";
    GPtrArray* edits = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_text_edit_free);
    g_ptr_array_add(edits, make_edit(0, 1, 0, 4, "X"));
    g_ptr_array_add(edits, make_edit(0, 3, 0, 5, "Y"));

    GError* err = NULL;
    gchar* out = lsp_edit_apply(src, edits, TRUE, &err);
    g_assert_null(out);
    g_assert_nonnull(err);
    g_error_free(err);
    g_ptr_array_unref(edits);
}

static void test_buffer_identifiers(void) {
    const gchar* src = "static int calc_total(int Calc_Sum, int my_calc_val, int calc) {\n"
                       "    int calc_total = Calc_Sum + my_calc_val;\n"
                       "    return calc_total + 42;\n"
                       "}\n";
    GPtrArray* hits = lsp_edit_buffer_identifiers(src, strlen(src), CODE_LANG_C, "calc", 10);
    /* Exact match "calc" excluded; exact-case prefix first, then ci-prefix, then substring */
    g_assert_cmpuint(hits->len, ==, 3);
    g_assert_cmpstr(hits->pdata[0], ==, "calc_total");
    g_assert_cmpstr(hits->pdata[1], ==, "Calc_Sum");
    g_assert_cmpstr(hits->pdata[2], ==, "my_calc_val");
    g_ptr_array_unref(hits);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/lsp_edit/apply_multiple", test_apply_multiple);
    g_test_add_func("/lsp_edit/apply_utf16_emoji", test_apply_utf16_emoji);
    g_test_add_func("/lsp_edit/apply_overlap_rejected", test_apply_overlap_rejected);
    g_test_add_func("/lsp_edit/buffer_identifiers", test_buffer_identifiers);
    return g_test_run();
}
