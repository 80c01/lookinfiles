/* Unit tests for src/code/code_outline_tree.c: Markdown headings, the outline
 * tree (nesting, groups) and the entry at a line. */
#include "code/code_outline_tree.h"

#include <string.h>

/* Headings as "level:name@line" (1-based line), separated by spaces */
static gchar* headings(const gchar* text) {
    GPtrArray* entries = code_outline_markdown(text, strlen(text));
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < entries->len; i++) {
        CodeOutlineEntry* e = g_ptr_array_index(entries, i);
        if (s->len > 0) g_string_append_c(s, ' ');
        g_string_append_printf(s, "%d:%s@%d", e->level, e->name, e->line + 1);
    }
    g_ptr_array_unref(entries);
    return g_string_free(s, FALSE);
}

#define ASSERT_HEADINGS(text, expected)           \
    do {                                          \
        gchar* got_ = headings(text);             \
        g_assert_cmpstr(got_, ==, (expected));    \
        g_free(got_);                             \
    } while (0)

static void test_markdown_atx(void) {
    ASSERT_HEADINGS("# Title\ntext\n## Part one ##\n### Deep\n", "1:Title@1 2:Part one@3 3:Deep@4");
    ASSERT_HEADINGS("#NoSpace\n####### seven\n    # indented code\n", "");
    ASSERT_HEADINGS("  ## Indented a little\n", "2:Indented a little@1");
    ASSERT_HEADINGS("# C# notes\n", "1:C# notes@1");
    ASSERT_HEADINGS("#\n# \n", "");
    ASSERT_HEADINGS("# Windows\r\n## Line ends\r\n", "1:Windows@1 2:Line ends@2");
}

static void test_markdown_setext(void) {
    ASSERT_HEADINGS("Title\n=====\n\nPart\n----\n", "1:Title@1 2:Part@4");
    /* A rule after a blank line, a list item or a heading is no underline */
    ASSERT_HEADINGS("text\n\n---\n- item\n---\n# H\n---\n", "1:H@6");
}

static void test_markdown_skips(void) {
    ASSERT_HEADINGS("```c\n# include\n```\n# After\n", "1:After@4");
    ASSERT_HEADINGS("~~~~\n```\n# still code\n~~~~\n# Out\n", "1:Out@5");
    ASSERT_HEADINGS("---\ntitle: x\n# not a heading\n---\n# Real\n", "1:Real@5");
}

static void test_markdown_column(void) {
    const gchar* text = "intro\n  ## Two\n";
    GPtrArray* entries = code_outline_markdown(text, strlen(text));
    g_assert_cmpuint(entries->len, ==, 1);
    CodeOutlineEntry* e = g_ptr_array_index(entries, 0);
    g_assert_cmpint(e->line, ==, 1);
    g_assert_cmpint(e->column, ==, 5);
    g_ptr_array_unref(entries);
}

/* The tree as text: "name(child child)" or "[Group](...)" */
static void print_nodes(GString* s, const GPtrArray* nodes, const GPtrArray* entries) {
    for (guint i = 0; i < nodes->len; i++) {
        const CodeOutlineNode* n = g_ptr_array_index(nodes, i);
        if (i > 0) g_string_append_c(s, ' ');
        if (n->entry < 0) {
            g_string_append_printf(s, "[%s]", code_outline_group_label(n->group));
        } else {
            g_string_append(s, ((CodeOutlineEntry*)g_ptr_array_index(entries, n->entry))->name);
        }
        if (n->children->len > 0) {
            g_string_append_c(s, '(');
            print_nodes(s, n->children, entries);
            g_string_append_c(s, ')');
        }
    }
}

static gchar* tree_text(const GPtrArray* entries, CodeOutlineSort sort) {
    GPtrArray* roots = code_outline_tree(entries, sort);
    GString* s = g_string_new(NULL);
    print_nodes(s, roots, entries);
    g_ptr_array_unref(roots);
    return g_string_free(s, FALSE);
}

static GPtrArray* sample_symbols(void) {
    GPtrArray* e = code_outline_entries_new();
    g_ptr_array_add(e, code_outline_entry_new("main", "int main()", NULL, CODE_SYM_FUNCTION, 40, 4));
    g_ptr_array_add(e, code_outline_entry_new("Point", NULL, NULL, CODE_SYM_TYPE, 2, 7));
    g_ptr_array_add(e, code_outline_entry_new("x", "int", "Point", CODE_SYM_FIELD, 3, 8));
    g_ptr_array_add(e, code_outline_entry_new("y", "int", "Point", CODE_SYM_FIELD, 4, 8));
    g_ptr_array_add(e, code_outline_entry_new("MAX", NULL, NULL, CODE_SYM_MACRO, 0, 8));
    g_ptr_array_add(e, code_outline_entry_new("helper", NULL, NULL, CODE_SYM_FUNCTION, 10, 5));
    g_ptr_array_add(e, code_outline_entry_new("orphan", NULL, "Missing", CODE_SYM_FIELD, 12, 0));
    code_outline_sort(e);
    return e;
}

static void test_tree_symbols(void) {
    GPtrArray* e = sample_symbols();
    gchar* by_line = tree_text(e, CODE_OUTLINE_BY_LINE);
    g_assert_cmpstr(by_line, ==, "MAX Point(x y) helper orphan main");
    gchar* by_kind = tree_text(e, CODE_OUTLINE_BY_KIND);
    g_assert_cmpstr(by_kind, ==, "[Types](Point(x y)) [Functions](helper main) [Macros](MAX) [Fields](orphan)");
    g_free(by_line);
    g_free(by_kind);
    g_ptr_array_unref(e);
}

static void test_tree_headings(void) {
    const gchar* text = "# A\n### A1\n## A2\n# B\n## B1\n";
    GPtrArray* e = code_outline_markdown(text, strlen(text));
    gchar* t = tree_text(e, CODE_OUTLINE_BY_KIND);
    g_assert_cmpstr(t, ==, "A(A1 A2) B(B1)");
    g_free(t);
    g_ptr_array_unref(e);
    GPtrArray* none = code_outline_entries_new();
    t = tree_text(none, CODE_OUTLINE_BY_KIND);
    g_assert_cmpstr(t, ==, "");
    g_free(t);
    g_ptr_array_unref(none);
}

static void test_entry_at_line(void) {
    GPtrArray* e = sample_symbols(); /* lines 0 2 3 4 10 12 40 */
    g_assert_cmpint(code_outline_entry_at_line(e, 0), ==, 0);
    g_assert_cmpint(code_outline_entry_at_line(e, 3), ==, 2);
    g_assert_cmpint(code_outline_entry_at_line(e, 20), ==, 5);
    g_assert_cmpint(code_outline_entry_at_line(e, 99), ==, 6);
    g_ptr_array_unref(e);
    GPtrArray* late = code_outline_entries_new();
    g_ptr_array_add(late, code_outline_entry_new("f", NULL, NULL, CODE_SYM_FUNCTION, 5, 0));
    g_assert_cmpint(code_outline_entry_at_line(late, 2), ==, -1);
    g_ptr_array_unref(late);
}

static void test_heading_tags(void) {
    g_assert_cmpint(code_outline_heading_tag_level("md_h1"), ==, 1);
    g_assert_cmpint(code_outline_heading_tag_level("html_h6"), ==, 6);
    g_assert_cmpint(code_outline_heading_tag_level("org_h3"), ==, 3);
    g_assert_cmpint(code_outline_heading_tag_level("pd_h2"), ==, 2);
    g_assert_cmpint(code_outline_heading_tag_level("pd_outline_h4"), ==, 4);
    g_assert_cmpint(code_outline_heading_tag_level("md_h7"), ==, 0);
    g_assert_cmpint(code_outline_heading_tag_level("md_h1x"), ==, 0);
    g_assert_cmpint(code_outline_heading_tag_level("md_hr"), ==, 0);
    g_assert_cmpint(code_outline_heading_tag_level("md_hl_ffffff"), ==, 0);
    g_assert_cmpint(code_outline_heading_tag_level(NULL), ==, 0);
    CodeOutlineEntry* e = code_outline_heading_new("  Two\n  lines\t here \n", 2, 7);
    g_assert_nonnull(e);
    g_assert_cmpstr(e->name, ==, "Two lines here");
    g_assert_cmpint(e->level, ==, 2);
    g_assert_cmpint(e->line, ==, 7);
    code_outline_entry_free(e);
    g_assert_null(code_outline_heading_new(" \n\t", 1, 0));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/code_outline_tree/markdown_atx", test_markdown_atx);
    g_test_add_func("/code_outline_tree/markdown_setext", test_markdown_setext);
    g_test_add_func("/code_outline_tree/markdown_skips", test_markdown_skips);
    g_test_add_func("/code_outline_tree/markdown_column", test_markdown_column);
    g_test_add_func("/code_outline_tree/tree_symbols", test_tree_symbols);
    g_test_add_func("/code_outline_tree/tree_headings", test_tree_headings);
    g_test_add_func("/code_outline_tree/entry_at_line", test_entry_at_line);
    g_test_add_func("/code_outline_tree/heading_tags", test_heading_tags);
    return g_test_run();
}
