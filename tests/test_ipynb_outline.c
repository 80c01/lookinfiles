/* Unit tests for src/jupyter/ipynb_outline.c (the tagbar of a notebook tab). */
#include "jupyter/ipynb_outline.h"

#include <string.h>

static IpynbDoc* sample(void) {
    IpynbDoc* doc = ipynb_doc_new(); /* one empty code cell */
    ipynb_doc_set_source(doc, 0, "# Not a heading: code");
    guint md = ipynb_doc_insert_cell(doc, 1, IPYNB_CELL_MARKDOWN);
    ipynb_doc_set_source(doc, md, "# Intro\nSome text\n\n## Data\n");
    guint code = ipynb_doc_insert_cell(doc, 2, IPYNB_CELL_CODE);
    ipynb_doc_set_source(doc, code, "x = 1");
    guint setext = ipynb_doc_insert_cell(doc, 3, IPYNB_CELL_MARKDOWN);
    ipynb_doc_set_source(doc, setext, "Results\n=======\n```\n# fenced\n```");
    ipynb_doc_insert_cell(doc, 4, IPYNB_CELL_RAW);
    ipynb_doc_set_source(doc, 4, "# raw");
    return doc;
}

static void test_headings(void) {
    IpynbDoc* doc = sample();
    GPtrArray* h = ipynb_outline_headings(doc);
    g_assert_cmpuint(h->len, ==, 3);
    const CodeOutlineEntry* e = g_ptr_array_index(h, 0);
    g_assert_cmpstr(e->name, ==, "Intro");
    g_assert_cmpint(e->level, ==, 1);
    g_assert_cmpint(e->line, ==, 1);
    g_assert_cmpint(e->column, ==, 0);
    e = g_ptr_array_index(h, 1);
    g_assert_cmpstr(e->name, ==, "Data");
    g_assert_cmpint(e->level, ==, 2);
    g_assert_cmpint(e->line, ==, 1);
    g_assert_cmpint(e->column, ==, 3);
    e = g_ptr_array_index(h, 2);
    g_assert_cmpstr(e->name, ==, "Results");
    g_assert_cmpint(e->level, ==, 1);
    g_assert_cmpint(e->line, ==, 3);
    /* The current cell finds its heading */
    g_assert_cmpint(code_outline_entry_at_line(h, 0), ==, -1);
    g_assert_cmpint(code_outline_entry_at_line(h, 2), ==, 1);
    g_assert_cmpint(code_outline_entry_at_line(h, 4), ==, 2);
    g_ptr_array_unref(h);
    ipynb_doc_free(doc);
}

static void test_same(void) {
    IpynbDoc* doc = sample();
    GPtrArray* a = ipynb_outline_headings(doc);
    GPtrArray* b = ipynb_outline_headings(doc);
    g_assert_true(ipynb_outline_same(a, b));
    g_ptr_array_unref(b);
    /* Moving a cell moves its headings */
    ipynb_doc_move_cell(doc, 2, 0);
    b = ipynb_outline_headings(doc);
    g_assert_false(ipynb_outline_same(a, b));
    g_ptr_array_unref(b);
    ipynb_doc_set_source(doc, 1, "plain text");
    ipynb_doc_set_source(doc, 2, "plain text");
    b = ipynb_outline_headings(doc);
    g_assert_cmpuint(b->len, ==, 1);
    g_assert_false(ipynb_outline_same(a, b));
    g_ptr_array_unref(b);
    g_assert_true(ipynb_outline_same(NULL, NULL));
    g_ptr_array_unref(a);
    ipynb_doc_free(doc);
}

static void test_empty(void) {
    GPtrArray* h = ipynb_outline_headings(NULL);
    g_assert_cmpuint(h->len, ==, 0);
    g_ptr_array_unref(h);
}

static void test_levels(void) {
    IpynbDoc* doc = sample();
    g_assert_cmpuint(ipynb_outline_cell_level(doc, 0), ==, 0); /* code */
    g_assert_cmpuint(ipynb_outline_cell_level(doc, 1), ==, 1); /* # and ##: the highest */
    g_assert_cmpuint(ipynb_outline_cell_level(doc, 2), ==, 0);
    g_assert_cmpuint(ipynb_outline_cell_level(doc, 3), ==, 1); /* Setext */
    g_assert_cmpuint(ipynb_outline_cell_level(doc, 4), ==, 0); /* raw */
    ipynb_doc_set_source(doc, 1, "Plain text");
    g_assert_cmpuint(ipynb_outline_cell_level(doc, 1), ==, 0);
    ipynb_doc_set_source(doc, 1, "### Deep\n## Less deep");
    g_assert_cmpuint(ipynb_outline_cell_level(doc, 1), ==, 2);
    ipynb_doc_free(doc);
}

static void test_sections(void) {
    /* # A, code, ## B, code, ### C, code, # D, code */
    const guint levels[] = { 1, 0, 2, 0, 3, 0, 1, 0 };
    guint n = G_N_ELEMENTS(levels);
    g_assert_cmpuint(ipynb_outline_section_end(levels, n, 0), ==, 6);
    g_assert_cmpuint(ipynb_outline_section_end(levels, n, 2), ==, 6);
    g_assert_cmpuint(ipynb_outline_section_end(levels, n, 4), ==, 6);
    g_assert_cmpuint(ipynb_outline_section_end(levels, n, 6), ==, 8);
    g_assert_cmpuint(ipynb_outline_section_end(levels, n, 1), ==, 2); /* no section */
    g_assert_cmpuint(ipynb_outline_section_end(levels, n, 9), ==, 8);

    gboolean collapsed[8] = { FALSE };
    gboolean hidden[8];
    ipynb_outline_hidden(levels, collapsed, n, hidden);
    for (guint i = 0; i < n; i++) g_assert_false(hidden[i]);

    collapsed[2] = TRUE; /* ## B: hides code, ### C, code */
    ipynb_outline_hidden(levels, collapsed, n, hidden);
    const gboolean b[] = { FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE };
    for (guint i = 0; i < n; i++) g_assert_cmpint(hidden[i], ==, b[i]);

    collapsed[0] = TRUE; /* # A as well: everything up to # D */
    collapsed[1] = TRUE; /* a flag on a cell without headings does nothing */
    ipynb_outline_hidden(levels, collapsed, n, hidden);
    const gboolean a[] = { FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE };
    for (guint i = 0; i < n; i++) g_assert_cmpint(hidden[i], ==, a[i]);
}

static void test_flags(void) {
    IpynbDoc* doc = sample();
    gchar* before = ipynb_doc_to_json(doc);
    g_assert_false(ipynb_doc_cell_flag(doc, 1, IPYNB_FLAG_HEADING_COLLAPSED));
    /* Each flag goes on its type of cell only */
    g_assert_false(ipynb_doc_set_cell_flag(doc, 0, IPYNB_FLAG_HEADING_COLLAPSED, TRUE));
    g_assert_false(ipynb_doc_set_cell_flag(doc, 1, IPYNB_FLAG_OUTPUTS_HIDDEN, TRUE));
    g_assert_true(ipynb_doc_set_cell_flag(doc, 1, IPYNB_FLAG_HEADING_COLLAPSED, TRUE));
    g_assert_false(ipynb_doc_set_cell_flag(doc, 1, IPYNB_FLAG_HEADING_COLLAPSED, TRUE));
    g_assert_true(ipynb_doc_cell_flag(doc, 1, IPYNB_FLAG_HEADING_COLLAPSED));
    g_assert_true(ipynb_doc_set_cell_flag(doc, 2, IPYNB_FLAG_OUTPUTS_HIDDEN, TRUE));
    g_assert_true(ipynb_doc_cell_flag(doc, 2, IPYNB_FLAG_OUTPUTS_HIDDEN));
    gchar* json = ipynb_doc_to_json(doc);
    g_assert_nonnull(strstr(json, "\"jp-MarkdownHeadingCollapsed\": true"));
    g_assert_nonnull(strstr(json, "\"outputs_hidden\": true"));
    g_assert_nonnull(strstr(json, "\"collapsed\": true"));
    /* Read back as saved */
    IpynbDoc* again = ipynb_doc_parse(json, strlen(json), NULL);
    g_assert_true(ipynb_doc_cell_flag(again, 1, IPYNB_FLAG_HEADING_COLLAPSED));
    g_assert_true(ipynb_doc_cell_flag(again, 2, IPYNB_FLAG_OUTPUTS_HIDDEN));
    ipynb_doc_free(again);
    g_free(json);
    /* Cleared: the notebook as it was */
    g_assert_true(ipynb_doc_set_cell_flag(doc, 1, IPYNB_FLAG_HEADING_COLLAPSED, FALSE));
    g_assert_true(ipynb_doc_set_cell_flag(doc, 2, IPYNB_FLAG_OUTPUTS_HIDDEN, FALSE));
    g_assert_false(ipynb_doc_set_cell_flag(doc, 2, IPYNB_FLAG_OUTPUTS_HIDDEN, FALSE));
    json = ipynb_doc_to_json(doc);
    g_assert_cmpstr(json, ==, before);
    g_free(json);
    g_free(before);
    ipynb_doc_free(doc);
}

static void test_older_collapsed(void) {
    const gchar* text = "{\"cells\": [{\"cell_type\": \"code\", \"metadata\": {\"collapsed\": true},"
                        " \"source\": [], \"outputs\": [], \"execution_count\": null}],"
                        " \"metadata\": {}, \"nbformat\": 4, \"nbformat_minor\": 2}";
    IpynbDoc* doc = ipynb_doc_parse(text, strlen(text), NULL);
    g_assert_nonnull(doc);
    g_assert_true(ipynb_doc_cell_flag(doc, 0, IPYNB_FLAG_OUTPUTS_HIDDEN));
    g_assert_true(ipynb_doc_set_cell_flag(doc, 0, IPYNB_FLAG_OUTPUTS_HIDDEN, FALSE));
    g_assert_false(ipynb_doc_cell_flag(doc, 0, IPYNB_FLAG_OUTPUTS_HIDDEN));
    ipynb_doc_free(doc);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ipynb_outline/headings", test_headings);
    g_test_add_func("/ipynb_outline/same", test_same);
    g_test_add_func("/ipynb_outline/empty", test_empty);
    g_test_add_func("/ipynb_outline/levels", test_levels);
    g_test_add_func("/ipynb_outline/sections", test_sections);
    g_test_add_func("/ipynb_outline/flags", test_flags);
    g_test_add_func("/ipynb_outline/older_collapsed", test_older_collapsed);
    return g_test_run();
}
