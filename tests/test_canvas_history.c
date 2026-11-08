/* Unit tests for src/canvas/canvas_history.c: undo / redo of page states. */
#include "canvas/canvas_history.h"

static void test_undo_redo(void) {
    CanvasHistory* h = canvas_history_new(10);
    g_assert_false(canvas_history_can_undo(h));
    g_assert_null(canvas_history_undo(h, "a"));

    canvas_history_push(h, "a"); /* a -> b */
    canvas_history_push(h, "b"); /* b -> c */
    gchar* s = canvas_history_undo(h, "c");
    g_assert_cmpstr(s, ==, "b");
    g_free(s);
    s = canvas_history_undo(h, "b");
    g_assert_cmpstr(s, ==, "a");
    g_free(s);
    g_assert_false(canvas_history_can_undo(h));
    g_assert_true(canvas_history_can_redo(h));

    s = canvas_history_redo(h, "a");
    g_assert_cmpstr(s, ==, "b");
    g_free(s);
    s = canvas_history_redo(h, "b");
    g_assert_cmpstr(s, ==, "c");
    g_free(s);
    g_assert_false(canvas_history_can_redo(h));
    g_assert_null(canvas_history_redo(h, "c"));
    canvas_history_free(h);
}

static void test_push_clears_redo(void) {
    CanvasHistory* h = canvas_history_new(10);
    canvas_history_push(h, "a");
    g_free(canvas_history_undo(h, "b"));
    g_assert_true(canvas_history_can_redo(h));
    canvas_history_push(h, "a");
    g_assert_false(canvas_history_can_redo(h));

    g_free(canvas_history_undo(h, "c"));
    canvas_history_clear_redo(h);
    g_assert_false(canvas_history_can_redo(h));
    canvas_history_free(h);
}

static void test_no_duplicate(void) {
    CanvasHistory* h = canvas_history_new(10);
    canvas_history_push(h, "a");
    canvas_history_push(h, "a");
    g_free(canvas_history_undo(h, "b"));
    g_assert_false(canvas_history_can_undo(h));
    canvas_history_free(h);
}

static void test_limit_and_clear(void) {
    CanvasHistory* h = canvas_history_new(2);
    canvas_history_push(h, "a");
    canvas_history_push(h, "b");
    canvas_history_push(h, "c");
    gchar* s = canvas_history_undo(h, "d");
    g_assert_cmpstr(s, ==, "c");
    g_free(s);
    s = canvas_history_undo(h, "c");
    g_assert_cmpstr(s, ==, "b");
    g_free(s);
    g_assert_false(canvas_history_can_undo(h)); /* "a" was dropped */

    canvas_history_clear(h);
    g_assert_false(canvas_history_can_undo(h));
    g_assert_false(canvas_history_can_redo(h));
    canvas_history_free(h);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/canvas_history/undo_redo", test_undo_redo);
    g_test_add_func("/canvas_history/push_clears_redo", test_push_clears_redo);
    g_test_add_func("/canvas_history/no_duplicate", test_no_duplicate);
    g_test_add_func("/canvas_history/limit_and_clear", test_limit_and_clear);
    return g_test_run();
}
