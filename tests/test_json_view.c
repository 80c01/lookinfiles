/* Unit tests for src/formats/json_view.c (tolerant JSON pretty-printing). */
#include "formats/json_view.h"

#include <string.h>

static gchar* pretty(const gchar* json, JsonViewInfo* info) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = json_view_append(doc, json, strlen(json), info, &error);
    g_assert_no_error(error);
    g_assert_true(ok);
    return g_strdup(doc->text->str);
}

static void test_pretty(void) {
    JsonViewInfo info;
    g_autofree gchar* out = pretty("{\"a\":1,\"b\":[1,2,3],\"c\":{\"d\":null,\"e\":\"x\\\"y\"},\"f\":[],\"g\":{}}", &info);
    g_assert_cmpstr(out, ==,
                    "{\n"
                    "  \"a\": 1,\n"
                    "  \"b\": [1, 2, 3],\n"
                    "  \"c\": {\n"
                    "    \"d\": null,\n"
                    "    \"e\": \"x\\\"y\"\n"
                    "  },\n"
                    "  \"f\": [],\n"
                    "  \"g\": {}\n"
                    "}\n");
    g_assert_cmpint(info.root, ==, '{');
    g_assert_cmpuint(info.n_root_items, ==, 5);
    g_assert_false(info.lines);
}

static void test_styles(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(json_view_append(doc, "{\"k\": true}", 11, NULL, NULL));
    gboolean key = FALSE, keyword = FALSE;
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, i);
        key |= (s->style == PREVIEW_STYLE_KEY && s->end - s->start == 3);
        keyword |= (s->style == PREVIEW_STYLE_KEYWORD && s->end - s->start == 4);
    }
    g_assert_true(key);
    g_assert_true(keyword);
}

static void test_tolerant(void) {
    /* Comments, trailing commas, unquoted keys, single quotes (JSONC / JSON5) */
    g_autofree gchar* out = pretty("// settings\n{\n  /* c */ name: 'x', \"n\": -1.5e3, \"list\": [1,],\n}", NULL);
    g_assert_cmpstr(out, ==, "{\n  name: 'x',\n  \"n\": -1.5e3,\n  \"list\": [1]\n}\n");
}

static void test_lines(void) {
    JsonViewInfo info;
    g_autofree gchar* out = pretty("{\"a\":1}\n{\"a\":2}\n\n[3]\n", &info);
    g_assert_true(info.lines);
    g_assert_cmpuint(info.n_values, ==, 3);
    g_assert_cmpstr(out, ==, "{\n  \"a\": 1\n}\n\n{\n  \"a\": 2\n}\n\n[3]\n");
}

static void test_errors(void) {
    const gchar* bad[] = { "{\"a\" 1}", "[1, 2", "{\"a\": tru}", "", "{} x", "[\"open]" };
    for (gsize i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_autoptr(PreviewDoc) doc = preview_doc_new();
        GError* error = NULL;
        g_assert_false(json_view_append(doc, bad[i], strlen(bad[i]), NULL, &error));
        g_assert_nonnull(error);
        g_assert_cmpuint(doc->text->len, ==, 0);
        g_error_free(error);
    }
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    json_view_append(doc, "{\n  \"a\": 1\n  \"b\": 2\n}", 21, NULL, &error);
    g_assert_cmpstr(error->message, ==, "line 3, column 3: expected , or }");
    g_error_free(error);
}

static void test_deep_and_long(void) {
    GString* deep = g_string_new(NULL);
    for (gint i = 0; i < 1000; i++) {
        g_string_append_c(deep, '[');
    }
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_false(json_view_append(doc, deep->str, deep->len, NULL, NULL));
    g_string_free(deep, TRUE);

    GString* big = g_string_new("[");
    for (gint i = 0; i < JSON_VIEW_MAX_LINES * 2; i++) {
        g_string_append_printf(big, "%s{\"i\": %d}", i > 0 ? "," : "", i);
    }
    g_string_append_c(big, ']');
    g_autoptr(PreviewDoc) out = preview_doc_new();
    g_assert_true(json_view_append(out, big->str, big->len, NULL, NULL));
    g_assert_nonnull(strstr(out->text->str, "stopped after"));
    g_string_free(big, TRUE);
}

static void test_values(void) {
    const gchar* text = "{\"cells\": [{\"source\": [\"a\\n\", \"b \\u00e9\\ud83d\\ude00\"]}], n: 2.5, 'q': true}";
    GError* error = NULL;
    g_autoptr(JsonValue) root = json_value_parse(text, strlen(text), &error);
    g_assert_no_error(error);
    g_assert_cmpint(json_value_get_type(root), ==, JSON_VALUE_OBJECT);
    g_assert_cmpuint(json_value_get_length(root), ==, 3);
    const JsonValue* cells = json_value_get(root, "cells");
    g_assert_cmpint(json_value_get_type(cells), ==, JSON_VALUE_ARRAY);
    const JsonValue* cell = json_value_get_item(cells, 0);
    gchar* source = json_value_dup_text(json_value_get(cell, "source"));
    g_assert_cmpstr(source, ==, "a\nb é😀");
    g_free(source);
    g_assert_cmpfloat(json_value_get_number(json_value_get(root, "n")), ==, 2.5);
    gchar* q = json_value_dup_string(json_value_get(root, "q"));
    g_assert_cmpstr(q, ==, "true");
    g_free(q);
    g_assert_null(json_value_get(root, "missing"));
    g_assert_null(json_value_get_item(cells, 5));
    g_assert_null(json_value_parse("[1,", 3, NULL));
}

/* Text of a fold point */
static gchar* fold_text(const PreviewDoc* doc, const PreviewFoldPoint* point) {
    const gchar* a = g_utf8_offset_to_pointer(doc->text->str, point->start);
    const gchar* b = g_utf8_offset_to_pointer(doc->text->str, point->end);
    return g_strndup(a, (gsize)(b - a));
}

static void test_fold_points(void) {
    const gchar* json = "{\"a\": {\"b\": [{\"c\": 1}, 2]}, \"odd key\": {\"x\": 1}, \"s\": [1, 2]}";
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(json_view_append_folded(doc, json, strlen(json), NULL, NULL, NULL));
    /* Blocks only: not the one-line array "s" */
    const gchar* ids[] = { "$.a.b[0]", "$.a.b", "$.a", "$[\"odd key\"]", "$" };
    g_assert_cmpuint(doc->folds->len, ==, G_N_ELEMENTS(ids));
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++) {
        const PreviewFoldPoint* point = &g_array_index(doc->folds, PreviewFoldPoint, i);
        g_assert_cmpstr(point->id, ==, ids[i]);
        g_assert_false(point->folded);
    }
    g_autofree gchar* b = fold_text(doc, preview_doc_fold_find(doc, "$.a.b"));
    g_assert_cmpstr(b, ==, "    \"b\": [\n      {\n        \"c\": 1\n      },\n      2\n    ]");
    /* A line of values belongs to its block */
    const gchar* two = strstr(doc->text->str, "      2");
    const PreviewFoldPoint* at = preview_doc_fold_at(doc, (gint)g_utf8_pointer_to_offset(doc->text->str, two));
    g_assert_cmpstr(at->id, ==, "$.a.b");
    /* The members of the outer object go to the tagbar, without quotes */
    guint outline = 0;
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* span = &g_array_index(doc->spans, PreviewSpan, i);
        if (span->style == PREVIEW_STYLE_OUTLINE1) {
            outline++;
            g_assert_cmpint(doc->text->str[span->start - 1], ==, '"');
        }
    }
    g_assert_cmpuint(outline, ==, 3);
}

static void test_folded(void) {
    const gchar* json = "{\"a\": {\"b\": {\"c\": 1}}, \"l\": [{\"x\": 1}, {\"y\": 2}]}";
    g_autoptr(PreviewFold) fold = preview_fold_new(1);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(json_view_append_folded(doc, json, strlen(json), fold, NULL, NULL));
    g_assert_cmpstr(doc->text->str, ==, "{\n  \"a\": {…}  1 key,\n  \"l\": […]  2 items\n}\n");
    g_assert_true(preview_doc_fold_find(doc, "$.a")->folded);
    g_assert_null(preview_doc_fold_find(doc, "$.a.b"));

    /* Opened by hand against the level, then a block in it folded */
    preview_fold_toggle(fold, "$.l");
    g_autoptr(PreviewDoc) open = preview_doc_new();
    g_assert_true(json_view_append_folded(open, json, strlen(json), fold, NULL, NULL));
    g_assert_cmpstr(open->text->str, ==,
                    "{\n  \"a\": {…}  1 key,\n  \"l\": [\n    {…}  1 key,\n    {…}  1 key\n  ]\n}\n");
    g_assert_false(preview_doc_fold_find(open, "$.l")->folded);
    g_assert_true(preview_doc_fold_find(open, "$.l[1]")->folded);

    /* JSON Lines: one block per record */
    g_autoptr(PreviewFold) none = preview_fold_new(PREVIEW_FOLD_OPEN);
    preview_fold_toggle(none, "$[1]");
    g_autoptr(PreviewDoc) lines = preview_doc_new();
    g_assert_true(json_view_append_folded(lines, "{\"a\":1}\n{\"a\":2}\n", 16, none, NULL, NULL));
    g_assert_cmpstr(lines->text->str, ==, "{\n  \"a\": 1\n}\n\n{…}  1 key\n");
}

static void test_auto_level(void) {
    /* Too long open: folded below the deepest level that fits, with a note */
    GString* big = g_string_new("{\"items\": [");
    for (gint i = 0; i < JSON_VIEW_MAX_LINES / 4; i++) { /* 3 levels: 15,000 lines, 2: 2,500 */
        g_string_append_printf(big, "%s{\"i\": %d, \"j\": {\"k\": 1}}", i > 0 ? "," : "", i);
    }
    g_string_append(big, "]}");
    JsonViewInfo info;
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(json_view_append_folded(doc, big->str, big->len, NULL, &info, NULL));
    g_assert_cmpint(info.level, ==, 2);
    g_assert_true(g_str_has_prefix(doc->text->str, "Long document"));
    g_assert_null(strstr(doc->text->str, "stopped after"));
    g_assert_true(preview_doc_fold_find(doc, "$.items[5]")->folded);
    g_string_free(big, TRUE);

    g_autoptr(PreviewDoc) small = preview_doc_new();
    g_assert_true(json_view_append_folded(small, "[{\"a\": 1}]", 10, NULL, &info, NULL));
    g_assert_cmpint(info.level, ==, PREVIEW_FOLD_OPEN);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/json_view/pretty", test_pretty);
    g_test_add_func("/json_view/styles", test_styles);
    g_test_add_func("/json_view/tolerant", test_tolerant);
    g_test_add_func("/json_view/lines", test_lines);
    g_test_add_func("/json_view/errors", test_errors);
    g_test_add_func("/json_view/deep_and_long", test_deep_and_long);
    g_test_add_func("/json_view/values", test_values);
    g_test_add_func("/json_view/fold_points", test_fold_points);
    g_test_add_func("/json_view/folded", test_folded);
    g_test_add_func("/json_view/auto_level", test_auto_level);
    return g_test_run();
}
