/* Unit tests for src/tab_tree.c: the editor tabs of a workspace as text. */
#include "tab_tree.h"

static void test_round_trip(void) {
    const gchar* texts[] = {
        "[0]",
        "[0*,2,5!]",
        "H:0.500([0*,2],V:0.400([1!],[0]))",
        "H:0.500([0,t*],[t])",
        "V:0.300(H:0.700([3],[4*!]),[1])",
    };
    for (guint i = 0; i < G_N_ELEMENTS(texts); i++) {
        TabTreeNode* n = tab_tree_parse(texts[i]);
        g_assert_nonnull(n);
        gchar* back = tab_tree_to_string(n);
        g_assert_cmpstr(back, ==, texts[i]);
        g_free(back);
        tab_tree_free(n);
    }
}

static void test_fields(void) {
    TabTreeNode* n = tab_tree_parse("H:0.250([0*,2],[1!])");
    g_assert_nonnull(n);
    g_assert_false(n->is_notebook);
    g_assert_false(n->vertical);
    g_assert_cmpfloat_with_epsilon(n->frac, 0.25, 1e-9);
    g_assert_true(n->first->is_notebook);
    g_assert_cmpuint(n->first->tabs->len, ==, 2);
    g_assert_cmpuint(g_array_index(n->first->tabs, guint, 1), ==, 2);
    g_assert_cmpint(n->first->current, ==, 0);
    g_assert_cmpint(n->first->focused, ==, -1);
    g_assert_cmpint(n->second->current, ==, -1);
    g_assert_cmpint(n->second->focused, ==, 0);
    tab_tree_free(n);

    /* A terminal tab */
    n = tab_tree_parse("[0,t*]");
    g_assert_cmpuint(g_array_index(n->tabs, guint, 1), ==, TAB_TREE_TERMINAL);
    g_assert_cmpint(n->current, ==, 1);
    tab_tree_free(n);

    /* The fraction is clamped */
    n = tab_tree_parse("V:1.5([0],[1])");
    g_assert_cmpfloat_with_epsilon(n->frac, 0.95, 1e-9);
    tab_tree_free(n);
}

static void test_build(void) {
    TabTreeNode* a = tab_tree_notebook_new();
    tab_tree_notebook_add(a, 3);
    a->current = (gint)tab_tree_notebook_add(a, 1);
    TabTreeNode* b = tab_tree_notebook_new();
    b->focused = (gint)tab_tree_notebook_add(b, 0);
    TabTreeNode* n = tab_tree_split_new(TRUE, 0.6, a, b);
    gchar* text = tab_tree_to_string(n);
    g_assert_cmpstr(text, ==, "V:0.600([3,1*],[0!])");
    g_free(text);
    tab_tree_free(n);

    /* A split with one side missing is that side */
    a = tab_tree_notebook_new();
    tab_tree_notebook_add(a, 0);
    n = tab_tree_split_new(FALSE, 0.5, NULL, a);
    g_assert_true(n == a);
    tab_tree_free(n);
    g_assert_null(tab_tree_split_new(FALSE, 0.5, NULL, NULL));

    text = tab_tree_to_string(NULL);
    g_assert_cmpstr(text, ==, "");
    g_free(text);
}

static void test_invalid(void) {
    const gchar* bad[] = {
        "", "[]", "[", "[0,]", "[,0]", "[a]", "[0]x", "H:0.5([0])", "H:([0],[1])",
        "X:0.5([0],[1])", "H:0.5([0],[1]", "[99999999999]", "T", "[tt]", "[t1]",
    };
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_assert_null(tab_tree_parse(bad[i]));
    }
    g_assert_null(tab_tree_parse(NULL));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tab_tree/round_trip", test_round_trip);
    g_test_add_func("/tab_tree/fields", test_fields);
    g_test_add_func("/tab_tree/build", test_build);
    g_test_add_func("/tab_tree/invalid", test_invalid);
    return g_test_run();
}
