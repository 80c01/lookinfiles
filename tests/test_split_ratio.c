/* Unit tests for src/widgets/split_ratio.c: split shares and collapsed sides. */
#include "widgets/split_ratio.h"

static void test_position_of_shares(void) {
    g_assert_cmpint(split_ratio_position(900, 1.0 / 3.0, 0, 0), ==, 300);
    g_assert_cmpint(split_ratio_position(900, 0.5, 0, 0), ==, 450);
    g_assert_cmpint(split_ratio_position(900, 2.0 / 3.0, 0, 0), ==, 600);
    g_assert_cmpint(split_ratio_position(900, 0.0, 0, 0), ==, 0);
    g_assert_cmpint(split_ratio_position(900, 1.0, 0, 0), ==, 900);
    g_assert_cmpint(split_ratio_position(900, 1.5, 0, 0), ==, 900);
    g_assert_cmpint(split_ratio_position(0, 0.5, 0, 0), ==, 0);
}

static void test_position_keeps_minimum_sizes(void) {
    /* 1/3 of 300 is 100, but the start side needs 150 */
    g_assert_cmpint(split_ratio_position(300, 1.0 / 3.0, 150, 0), ==, 150);
    /* 2/3 of 300 is 200, but the end side needs 150 */
    g_assert_cmpint(split_ratio_position(300, 2.0 / 3.0, 0, 150), ==, 150);
    /* Not enough room for both: the share as is */
    g_assert_cmpint(split_ratio_position(200, 1.0 / 3.0, 150, 150), ==, 67);
}

static void test_reachable_and_matches(void) {
    g_assert_true(split_ratio_reachable(900, 0.5, 150, 150));
    g_assert_false(split_ratio_reachable(300, 1.0 / 3.0, 150, 0));
    g_assert_false(split_ratio_reachable(0, 0.5, 0, 0));
    g_assert_true(split_ratio_matches(452, 900, 0.5, 0, 0));
    g_assert_false(split_ratio_matches(460, 900, 0.5, 0, 0));
    g_assert_true(split_ratio_matches(150, 300, 1.0 / 3.0, 150, 0));
}

static void test_collapsed_sides(void) {
    g_assert_cmpint(split_ratio_collapsed(0, 900), ==, SPLIT_SIDE_START);
    g_assert_cmpint(split_ratio_collapsed(2, 900), ==, SPLIT_SIDE_START);
    g_assert_cmpint(split_ratio_collapsed(900, 900), ==, SPLIT_SIDE_END);
    g_assert_cmpint(split_ratio_collapsed(898, 900), ==, SPLIT_SIDE_END);
    g_assert_cmpint(split_ratio_collapsed(450, 900), ==, SPLIT_SIDE_NONE);
    g_assert_cmpint(split_ratio_collapsed(0, 4), ==, SPLIT_SIDE_NONE);
}

static void test_expand(void) {
    g_assert_cmpint(split_ratio_expand(900, 0.25, 0, 0), ==, 225);
    g_assert_cmpint(split_ratio_expand(900, -1.0, 0, 0), ==, 450);
    /* The share before was collapsed too: a half */
    g_assert_cmpint(split_ratio_expand(900, 0.0, 0, 0), ==, 450);
    g_assert_cmpint(split_ratio_expand(900, 1.0, 0, 0), ==, 450);
    g_assert_cmpint(split_ratio_expand(900, 0.05, 150, 0), ==, 150);
}

static void test_share_sets(void) {
    const gdouble* even = split_ratio_shares(SPLIT_SHARES_EVEN);
    const gdouble* side = split_ratio_shares(SPLIT_SHARES_SIDE);
    g_assert_cmpfloat_with_epsilon(even[1], 0.5, 1e-9);
    g_assert_cmpfloat_with_epsilon(side[0], 0.2, 1e-9);
    g_assert_cmpfloat_with_epsilon(side[1], 0.25, 1e-9);
    g_assert_cmpfloat_with_epsilon(side[2], 1.0 / 3.0, 1e-9);
    for (gint i = 1; i < SPLIT_RATIO_COUNT; i++) {
        g_assert_cmpfloat(even[i - 1], <, even[i]);
        g_assert_cmpfloat(side[i - 1], <, side[i]);
    }
}

static void test_fit(void) {
    g_assert_cmpint(split_ratio_fit(260, 1920, 0.4), ==, 260);
    /* A very long name: at most 40 % of the window */
    g_assert_cmpint(split_ratio_fit(1500, 1920, 0.4), ==, 768);
    g_assert_cmpint(split_ratio_fit(0, 1920, 0.4), ==, -1);
    g_assert_cmpint(split_ratio_fit(260, 0, 0.4), ==, -1);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/split_ratio/position", test_position_of_shares);
    g_test_add_func("/split_ratio/minimum_sizes", test_position_keeps_minimum_sizes);
    g_test_add_func("/split_ratio/reachable", test_reachable_and_matches);
    g_test_add_func("/split_ratio/collapsed", test_collapsed_sides);
    g_test_add_func("/split_ratio/expand", test_expand);
    g_test_add_func("/split_ratio/share_sets", test_share_sets);
    g_test_add_func("/split_ratio/fit", test_fit);
    return g_test_run();
}
