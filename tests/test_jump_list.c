/* Unit tests for src/jump_list.c: back / forward history of code
 * navigation jumps. */
#include "jump_list.h"

#define ASSERT_LOC(loc, p, l)                \
    do {                                     \
        const JumpLocation* loc_ = (loc);    \
        g_assert_nonnull(loc_);              \
        g_assert_cmpstr(loc_->path, ==, (p)); \
        g_assert_cmpint(loc_->line, ==, (l)); \
    } while (0)

static void test_back_forward(void) {
    JumpList* j = jump_list_new(100);
    g_assert_null(jump_list_back(j, "a.c", 1, 0));
    /* a.c:10 -> b.c:20 -> c.c:30 (current) */
    jump_list_push(j, "a.c", 10, 4);
    jump_list_push(j, "b.c", 20, 0);
    ASSERT_LOC(jump_list_back(j, "c.c", 30, 2), "b.c", 20);
    ASSERT_LOC(jump_list_back(j, "b.c", 20, 0), "a.c", 10);
    g_assert_null(jump_list_back(j, "a.c", 10, 4));
    ASSERT_LOC(jump_list_forward(j), "b.c", 20);
    ASSERT_LOC(jump_list_forward(j), "c.c", 30);
    g_assert_null(jump_list_forward(j));
    jump_list_free(j);
}

static void test_push_truncates(void) {
    JumpList* j = jump_list_new(100);
    jump_list_push(j, "a.c", 1, 0);
    jump_list_push(j, "b.c", 2, 0);
    ASSERT_LOC(jump_list_back(j, "c.c", 3, 0), "b.c", 2);
    ASSERT_LOC(jump_list_back(j, "b.c", 2, 0), "a.c", 1);
    /* A new jump from a.c drops b.c and c.c */
    jump_list_push(j, "a.c", 1, 0);
    g_assert_cmpuint(jump_list_length(j), ==, 1);
    g_assert_null(jump_list_forward(j));
    ASSERT_LOC(jump_list_back(j, "d.c", 4, 0), "a.c", 1);
    jump_list_free(j);
}

static void test_same_line_replaced(void) {
    JumpList* j = jump_list_new(100);
    jump_list_push(j, "a.c", 5, 0);
    jump_list_push(j, "a.c", 5, 9);
    g_assert_cmpuint(jump_list_length(j), ==, 1);
    /* Going back from where the last jump started: nothing new is added */
    g_assert_null(jump_list_back(j, "a.c", 5, 9));
    g_assert_cmpuint(jump_list_length(j), ==, 1);
    jump_list_free(j);
}

static void test_max(void) {
    JumpList* j = jump_list_new(3);
    for (gint i = 0; i < 5; i++) jump_list_push(j, "a.c", i, 0);
    g_assert_cmpuint(jump_list_length(j), ==, 3);
    ASSERT_LOC(jump_list_back(j, "a.c", 9, 0), "a.c", 4);
    g_assert_cmpuint(jump_list_length(j), ==, 3);
    ASSERT_LOC(jump_list_back(j, "a.c", 4, 0), "a.c", 3);
    g_assert_null(jump_list_back(j, "a.c", 3, 0));
    jump_list_free(j);
}

static void test_forget_file(void) {
    JumpList* j = jump_list_new(100);
    jump_list_push(j, "a.c", 1, 0);
    jump_list_push(j, "gone.c", 2, 0);
    jump_list_push(j, "b.c", 3, 0);
    jump_list_forget_file(j, "gone.c");
    g_assert_cmpuint(jump_list_length(j), ==, 2);
    ASSERT_LOC(jump_list_back(j, "c.c", 4, 0), "b.c", 3);
    ASSERT_LOC(jump_list_back(j, "b.c", 3, 0), "a.c", 1);
    jump_list_forget_file(j, "a.c");
    g_assert_cmpuint(jump_list_position(j), <=, jump_list_length(j));
    jump_list_free(j);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/jump_list/back_forward", test_back_forward);
    g_test_add_func("/jump_list/push_truncates", test_push_truncates);
    g_test_add_func("/jump_list/same_line_replaced", test_same_line_replaced);
    g_test_add_func("/jump_list/max", test_max);
    g_test_add_func("/jump_list/forget_file", test_forget_file);
    return g_test_run();
}
