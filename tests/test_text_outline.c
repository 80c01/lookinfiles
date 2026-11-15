/* Unit tests for src/formats/text_outline.c (text shown as written, with folds). */
#include "formats/text_outline.h"

#include <string.h>

static const gchar* TEXT = "a:\n  b: 1\n  c: [\n    2 ]\nd: é\n";

/* Lines 0-3 a block "a", 2-3 a block "a.c"; paths and a heading */
static TextOutline* outline_new(void) {
    TextOutline* o = text_outline_new(TEXT, strlen(TEXT));
    text_outline_add_token(o, 0, 1, PREVIEW_STYLE_KEY);
    text_outline_add_token(o, 17, 26, PREVIEW_STYLE_NUMBER); /* crosses a line */
    text_outline_add_heading(o, 0, 1, 1);
    text_outline_set_path(o, 0, "a");
    text_outline_set_path(o, 5, "a.b");
    text_outline_set_path(o, 5, "not kept");
    text_outline_set_path(o, 14, "a.c");
    text_outline_set_path(o, 28, "d");
    guint a = text_outline_add_block(o, 0, 0, "a", 1, TEXT_OUTLINE_KEYS, 2, FALSE);
    text_outline_block(o, a)->last = 3;
    text_outline_add_block(o, 2, 3, "a.c", 2, TEXT_OUTLINE_ITEMS, 1, TRUE);
    return o;
}

static void test_lines(void) {
    g_autoptr(TextOutline) o = outline_new();
    g_assert_cmpuint(text_outline_n_lines(o), ==, 5);
    g_assert_cmpuint(text_outline_line_at(o, 0), ==, 0);
    g_assert_cmpuint(text_outline_line_at(o, 12), ==, 2);
    g_assert_cmpuint(text_outline_line_start(o, 4), ==, 25);
    g_autoptr(TextOutline) empty = text_outline_new("", 0);
    g_assert_cmpuint(text_outline_n_lines(empty), ==, 1);
    g_autoptr(TextOutline) open = text_outline_new("x\ny", 3);
    g_assert_cmpuint(text_outline_n_lines(open), ==, 2);
}

static void test_print(void) {
    g_autoptr(TextOutline) o = outline_new();
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_cmpint(text_outline_level(o, NULL), ==, PREVIEW_FOLD_OPEN);
    g_assert_false(text_outline_print(o, doc, NULL, PREVIEW_FOLD_OPEN));
    g_assert_cmpstr(doc->text->str, ==, TEXT);
    g_assert_cmpuint(doc->folds->len, ==, 2);
    g_assert_cmpint(preview_doc_fold_find(doc, "a")->end, ==, 24);
    g_assert_cmpstr(preview_doc_path_at(doc, 7), ==, "a.b");
    g_assert_cmpstr(preview_doc_path_at(doc, 30), ==, "d");
    guint numbers = 0, headings = 0;
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* span = &g_array_index(doc->spans, PreviewSpan, i);
        numbers += span->style == PREVIEW_STYLE_NUMBER;
        headings += span->style == PREVIEW_STYLE_OUTLINE1;
    }
    g_assert_cmpuint(numbers, ==, 2); /* one span per line */
    g_assert_cmpuint(headings, ==, 1);
}

static void test_folded(void) {
    g_autoptr(TextOutline) o = outline_new();
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    text_outline_print(o, doc, NULL, 2);
    g_assert_cmpstr(doc->text->str, ==, "a:\n  b: 1\n  c: […  1 item\nd: é\n");
    g_assert_true(preview_doc_fold_find(doc, "a.c")->folded);
    g_autoptr(TextOutline) top = outline_new();
    g_autoptr(PreviewDoc) all = preview_doc_new();
    text_outline_print(top, all, NULL, 1);
    g_assert_cmpstr(all->text->str, ==, "a:  2 keys\nd: é\n");
    g_assert_null(preview_doc_fold_find(all, "a.c"));
}

static void test_long(void) {
    GString* text = g_string_new(NULL);
    for (guint i = 0; i < TEXT_OUTLINE_MAX_LINES + 5; i++) {
        g_string_append(text, "x\n");
    }
    g_autoptr(TextOutline) o = text_outline_new(text->str, text->len);
    text_outline_add_block(o, 1, 6, "b", 1, TEXT_OUTLINE_KEYS, 5, FALSE);
    g_assert_cmpint(text_outline_level(o, NULL), ==, 1);
    g_autoptr(PreviewFold) open = preview_fold_new(PREVIEW_FOLD_OPEN);
    g_assert_cmpint(text_outline_level(o, open), ==, PREVIEW_FOLD_OPEN);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(text_outline_print(o, doc, open, PREVIEW_FOLD_OPEN));
    g_assert_nonnull(strstr(doc->text->str, "stopped after"));
    g_string_free(text, TRUE);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/text_outline/lines", test_lines);
    g_test_add_func("/text_outline/print", test_print);
    g_test_add_func("/text_outline/folded", test_folded);
    g_test_add_func("/text_outline/long", test_long);
    return g_test_run();
}
