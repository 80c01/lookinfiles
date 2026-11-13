/* Unit tests for src/jupyter/ipynb_outline.c (the tagbar of a notebook tab). */
#include "jupyter/ipynb_outline.h"

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

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ipynb_outline/headings", test_headings);
    g_test_add_func("/ipynb_outline/same", test_same);
    g_test_add_func("/ipynb_outline/empty", test_empty);
    return g_test_run();
}
