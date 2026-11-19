#include "workspace/layout_history_stack.h"

static void test_empty_and_single(void) {
    LayoutHistoryStack* s = layout_history_stack_new(10, g_free);
    g_assert_false(layout_history_stack_can_back(s));
    g_assert_false(layout_history_stack_can_forward(s));
    g_assert_null(layout_history_stack_current(s));
    g_assert_null(layout_history_stack_back(s));
    g_assert_null(layout_history_stack_forward(s));
    g_assert_cmpuint(layout_history_stack_length(s), ==, 0);

    layout_history_stack_push(s, g_strdup("a"));
    g_assert_false(layout_history_stack_can_back(s));
    g_assert_false(layout_history_stack_can_forward(s));
    g_assert_cmpstr(layout_history_stack_current(s), ==, "a");
    g_assert_cmpuint(layout_history_stack_length(s), ==, 1);
    g_assert_cmpuint(layout_history_stack_position(s), ==, 0);

    layout_history_stack_free(s);
}

static void test_back_forward_and_branch(void) {
    LayoutHistoryStack* s = layout_history_stack_new(10, g_free);
    layout_history_stack_push(s, g_strdup("a"));
    layout_history_stack_push(s, g_strdup("b"));
    layout_history_stack_push(s, g_strdup("c"));
    g_assert_true(layout_history_stack_can_back(s));
    g_assert_false(layout_history_stack_can_forward(s));
    g_assert_cmpstr(layout_history_stack_current(s), ==, "c");

    g_assert_cmpstr(layout_history_stack_back(s), ==, "b");
    g_assert_true(layout_history_stack_can_back(s));
    g_assert_true(layout_history_stack_can_forward(s));

    g_assert_cmpstr(layout_history_stack_back(s), ==, "a");
    g_assert_false(layout_history_stack_can_back(s));
    g_assert_true(layout_history_stack_can_forward(s));
    g_assert_null(layout_history_stack_back(s));

    g_assert_cmpstr(layout_history_stack_forward(s), ==, "b");
    /* New change after stepping back drops the forward branch ("c") */
    layout_history_stack_push(s, g_strdup("d"));
    g_assert_false(layout_history_stack_can_forward(s));
    g_assert_cmpuint(layout_history_stack_length(s), ==, 3);
    g_assert_cmpstr(layout_history_stack_current(s), ==, "d");
    g_assert_cmpstr(layout_history_stack_back(s), ==, "b");
    g_assert_cmpstr(layout_history_stack_back(s), ==, "a");
    g_assert_cmpstr(layout_history_stack_forward(s), ==, "b");
    g_assert_cmpstr(layout_history_stack_forward(s), ==, "d");
    g_assert_null(layout_history_stack_forward(s));

    layout_history_stack_free(s);
}

static void test_limit_and_clear(void) {
    LayoutHistoryStack* s = layout_history_stack_new(3, g_free);
    layout_history_stack_push(s, g_strdup("1"));
    layout_history_stack_push(s, g_strdup("2"));
    layout_history_stack_push(s, g_strdup("3"));
    layout_history_stack_push(s, g_strdup("4"));
    g_assert_cmpuint(layout_history_stack_length(s), ==, 3);
    g_assert_cmpstr(layout_history_stack_current(s), ==, "4");
    g_assert_cmpstr(layout_history_stack_back(s), ==, "3");
    g_assert_cmpstr(layout_history_stack_back(s), ==, "2");
    g_assert_null(layout_history_stack_back(s));

    layout_history_stack_clear(s);
    g_assert_cmpuint(layout_history_stack_length(s), ==, 0);
    g_assert_false(layout_history_stack_can_back(s));
    g_assert_false(layout_history_stack_can_forward(s));

    layout_history_stack_free(s);
    layout_history_stack_free(NULL);
}

static void test_same_panels(void) {
    SavedLayout* a = layout_store_builtin_default();
    SavedLayout* b = saved_layout_copy(a);
    g_assert_true(layout_history_same_panels(a, b));
    g_assert_false(layout_history_same_panels(a, NULL));

    /* Tiny allocation jitter on a visible divider is ignored */
    b->main_frac = a->main_frac + 0.002;
    g_assert_true(layout_history_same_panels(a, b));

    /* A real resize counts as a change */
    b->main_frac = a->main_frac + 0.02;
    g_assert_false(layout_history_same_panels(a, b));
    b->main_frac = a->main_frac;

    /* Hidden divider fractions are ignored */
    b->right_frac = 0.30;
    b->editor_frac = 0.25;
    b->editor_left = TRUE;
    g_assert_true(layout_history_same_panels(a, b));

    /* Showing the terminal counts */
    b->term_vis = TRUE;
    g_assert_false(layout_history_same_panels(a, b));
    a->term_vis = TRUE;
    b->right_frac = a->right_frac;
    g_assert_true(layout_history_same_panels(a, b));

    /* Stacking the terminal counts */
    b->stacked = TRUE;
    g_assert_false(layout_history_same_panels(a, b));
    b->stacked = FALSE;

    /* Adding a terminal tab in the same pane is ignored, splitting counts */
    g_free(a->terminals);
    g_free(b->terminals);
    a->terminals = g_strdup("T");
    b->terminals = g_strdup("T2");
    g_assert_true(layout_history_same_panels(a, b));
    g_free(b->terminals);
    b->terminals = g_strdup("V:0.500(T,T)");
    g_assert_false(layout_history_same_panels(a, b));

    /* Terminal split jitter vs real resize */
    g_free(a->terminals);
    a->terminals = g_strdup("V:0.502(T,T)");
    g_assert_true(layout_history_same_panels(a, b));
    g_free(a->terminals);
    a->terminals = g_strdup("V:0.650(T,T)");
    g_assert_false(layout_history_same_panels(a, b));

    /* Editor dedicated panel, swap and splits */
    g_free(a->terminals);
    a->terminals = g_strdup("V:0.500(T,T)");
    a->editor_dedicated = TRUE;
    b->editor_dedicated = TRUE;
    a->editor_left = FALSE;
    b->editor_left = FALSE;
    a->editor_frac = 0.50;
    b->editor_frac = 0.50;
    g_free(a->editor_splits);
    g_free(b->editor_splits);
    a->editor_splits = g_strdup("H:0.500([0],[0])");
    b->editor_splits = g_strdup("H:0.501([0],[0])");
    g_assert_true(layout_history_same_panels(a, b));
    b->editor_left = TRUE;
    g_assert_false(layout_history_same_panels(a, b));
    b->editor_left = FALSE;
    g_free(b->editor_splits);
    b->editor_splits = g_strdup("V:0.500([0],[0])");
    g_assert_false(layout_history_same_panels(a, b));

    /* Tagbars */
    g_free(b->editor_splits);
    b->editor_splits = g_strdup("H:0.500([0],[0])");
    a->tagbars = TRUE;
    b->tagbars = TRUE;
    a->preview_tagbar = FALSE;
    b->preview_tagbar = TRUE;
    g_assert_false(layout_history_same_panels(a, b));

    saved_layout_free(a);
    saved_layout_free(b);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/layout_history_stack/empty_and_single", test_empty_and_single);
    g_test_add_func("/layout_history_stack/back_forward_and_branch", test_back_forward_and_branch);
    g_test_add_func("/layout_history_stack/limit_and_clear", test_limit_and_clear);
    g_test_add_func("/layout_history_stack/same_panels", test_same_panels);
    return g_test_run();
}
