/* Unit tests for src/widgets/pane_geometry.c: choosing the pane next to another one. */
#include "widgets/pane_geometry.h"

/* 2x2 grid of 100x100 panes: 0 top-left, 1 top-right, 2 bottom-left,
 * 3 bottom-right */
static const PaneRect GRID[] = {
    {0, 0, 100, 100}, {100, 0, 100, 100},
    {0, 100, 100, 100}, {100, 100, 100, 100},
};

static void test_grid_stays_in_row_or_column(void) {
    g_assert_cmpint(pane_find_neighbor(&GRID[2], GRID, 4, PANE_DIR_RIGHT), ==, 3);
    g_assert_cmpint(pane_find_neighbor(&GRID[0], GRID, 4, PANE_DIR_RIGHT), ==, 1);
    g_assert_cmpint(pane_find_neighbor(&GRID[3], GRID, 4, PANE_DIR_LEFT), ==, 2);
    g_assert_cmpint(pane_find_neighbor(&GRID[1], GRID, 4, PANE_DIR_DOWN), ==, 3);
    g_assert_cmpint(pane_find_neighbor(&GRID[2], GRID, 4, PANE_DIR_UP), ==, 0);
}

static void test_grid_edges(void) {
    g_assert_cmpint(pane_find_neighbor(&GRID[3], GRID, 4, PANE_DIR_RIGHT), ==, -1);
    g_assert_cmpint(pane_find_neighbor(&GRID[0], GRID, 4, PANE_DIR_UP), ==, -1);
    g_assert_cmpint(pane_find_neighbor(&GRID[0], GRID, 4, PANE_DIR_LEFT), ==, -1);
}

static void test_prefers_same_row_over_nearer_center(void) {
    /* Top row: narrow left pane, wide right pane; bottom row: wide left
     * pane, narrow right pane. From the bottom left, the top right pane's
     * center is nearer horizontally but the bottom right one is in the row. */
    const PaneRect panes[] = {
        {0, 0, 50, 100}, {50, 0, 150, 100},
        {0, 100, 150, 100}, {150, 100, 50, 100},
    };
    g_assert_cmpint(pane_find_neighbor(&panes[2], panes, 4, PANE_DIR_RIGHT), ==, 3);
}

static void test_ignores_other_rows(void) {
    /* Bottom row has a single pane; only the top row has a right pane. */
    const PaneRect panes[] = {
        {0, 0, 100, 100}, {100, 0, 100, 100}, {0, 100, 200, 100},
        {0, 200, 100, 100},
    };
    g_assert_cmpint(pane_find_neighbor(&panes[3], panes, 4, PANE_DIR_RIGHT), ==, -1);
    /* A full-width pane below both top panes is below each of them. */
    g_assert_cmpint(pane_find_neighbor(&panes[1], panes, 4, PANE_DIR_DOWN), ==, 2);
}

static void test_touching_edges_with_handle(void) {
    /* A 1 px paned handle sits between the panes. */
    const PaneRect panes[] = {{0, 0, 100, 100}, {101, 0, 100, 100}};
    g_assert_cmpint(pane_find_neighbor(&panes[0], panes, 2, PANE_DIR_RIGHT), ==, 1);
    g_assert_cmpint(pane_find_neighbor(&panes[1], panes, 2, PANE_DIR_LEFT), ==, 0);
}

static void test_drop_zone(void) {
    const PaneRect area = {100, 50, 400, 200};
    g_assert_cmpint(pane_drop_zone(&area, 300, 150), ==, PANE_ZONE_CENTER);
    g_assert_cmpint(pane_drop_zone(&area, 110, 150), ==, PANE_ZONE_LEFT);
    g_assert_cmpint(pane_drop_zone(&area, 490, 150), ==, PANE_ZONE_RIGHT);
    g_assert_cmpint(pane_drop_zone(&area, 300, 55), ==, PANE_ZONE_TOP);
    g_assert_cmpint(pane_drop_zone(&area, 300, 245), ==, PANE_ZONE_BOTTOM);
    /* Near a corner the nearest edge wins (relative to the pane size). */
    g_assert_cmpint(pane_drop_zone(&area, 104, 60), ==, PANE_ZONE_LEFT);
    g_assert_cmpint(pane_drop_zone(&area, 140, 52), ==, PANE_ZONE_TOP);
    /* Just inside the edge band, then just outside it. */
    g_assert_cmpint(pane_drop_zone(&area, 199, 150), ==, PANE_ZONE_LEFT);
    g_assert_cmpint(pane_drop_zone(&area, 201, 150), ==, PANE_ZONE_CENTER);
    const PaneRect empty = {0, 0, 0, 0};
    g_assert_cmpint(pane_drop_zone(&empty, 0, 0), ==, PANE_ZONE_CENTER);
}

static void test_zone_rect(void) {
    const PaneRect area = {100, 50, 400, 200};
    PaneRect r = pane_zone_rect(&area, PANE_ZONE_RIGHT);
    g_assert_cmpfloat(r.x, ==, 300);
    g_assert_cmpfloat(r.width, ==, 200);
    g_assert_cmpfloat(r.height, ==, 200);
    r = pane_zone_rect(&area, PANE_ZONE_BOTTOM);
    g_assert_cmpfloat(r.y, ==, 150);
    g_assert_cmpfloat(r.height, ==, 100);
    r = pane_zone_rect(&area, PANE_ZONE_LEFT);
    g_assert_cmpfloat(r.x, ==, 100);
    g_assert_cmpfloat(r.width, ==, 200);
    r = pane_zone_rect(&area, PANE_ZONE_CENTER);
    g_assert_cmpfloat(r.width, ==, 400);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pane_geometry/grid_row_column", test_grid_stays_in_row_or_column);
    g_test_add_func("/pane_geometry/grid_edges", test_grid_edges);
    g_test_add_func("/pane_geometry/same_row_first", test_prefers_same_row_over_nearer_center);
    g_test_add_func("/pane_geometry/other_rows_ignored", test_ignores_other_rows);
    g_test_add_func("/pane_geometry/touching_edges", test_touching_edges_with_handle);
    g_test_add_func("/pane_geometry/drop_zone", test_drop_zone);
    g_test_add_func("/pane_geometry/zone_rect", test_zone_rect);
    return g_test_run();
}
