/* Unit tests for src/canvas_geometry.c: grid snap and object alignment. */
#include "canvas_geometry.h"

static void test_snap_coord(void) {
    g_assert_cmpint(canvas_snap_coord(0, 16), ==, 0);
    g_assert_cmpint(canvas_snap_coord(7, 16), ==, 0);
    g_assert_cmpint(canvas_snap_coord(8, 16), ==, 16);
    g_assert_cmpint(canvas_snap_coord(15, 16), ==, 16);
    g_assert_cmpint(canvas_snap_coord(16, 16), ==, 16);
    g_assert_cmpint(canvas_snap_coord(25, 16), ==, 32);
    /* Disabled grid */
    g_assert_cmpint(canvas_snap_coord(25, 0), ==, 25);
    g_assert_cmpint(canvas_snap_coord(25, 1), ==, 25);
}

static void test_snap_box(void) {
    CanvasBox targets[] = {
        {100, 100, 50, 50},
        {300, 200, 100, 100},
    };
    gint out_x = 0, out_y = 0;
    gboolean guide_x = FALSE, guide_y = FALSE;

    /* Near target 0's left edge (100) */
    canvas_snap_box(102, 10, 50, 50, targets, 2, 0, &out_x, &out_y, &guide_x, &guide_y);
    g_assert_cmpint(out_x, ==, 100);
    g_assert_true(guide_x);

    /* Grid snap fallback when no guide */
    canvas_snap_box(17, 33, 50, 50, targets, 2, 16, &out_x, &out_y, &guide_x, &guide_y);
    g_assert_cmpint(out_x, ==, 16);
    g_assert_cmpint(out_y, ==, 32);
    g_assert_false(guide_x);
    g_assert_false(guide_y);
}

static void test_align_boxes(void) {
    CanvasBox boxes[] = {
        {10, 20, 100, 50},
        {60, 80, 80, 40},
        {30, 150, 50, 30},
    };

    /* Left align: min x is 10 */
    canvas_align_boxes(boxes, 3, CANVAS_ALIGN_LEFT);
    g_assert_cmpint(boxes[0].x, ==, 10);
    g_assert_cmpint(boxes[1].x, ==, 10);
    g_assert_cmpint(boxes[2].x, ==, 10);

    /* Top align: min y is 20 */
    canvas_align_boxes(boxes, 3, CANVAS_ALIGN_TOP);
    g_assert_cmpint(boxes[0].y, ==, 20);
    g_assert_cmpint(boxes[1].y, ==, 20);
    g_assert_cmpint(boxes[2].y, ==, 20);

    /* Reset positions for distribute */
    boxes[0].x = 0;   boxes[0].w = 20;
    boxes[1].x = 100; boxes[1].w = 20;
    boxes[2].x = 40;  boxes[2].w = 20;
    /* Sorted by x: box 0 (0..20), box 2 (40..60), box 1 (100..120).
     * Total span = 120 - 0 = 120. Total width = 60. Remaining space = 60.
     * Between 3 boxes, 2 gaps of 30.
     * Box 0 at 0, box 2 at 50, box 1 at 100. */
    canvas_align_boxes(boxes, 3, CANVAS_ALIGN_DISTRIBUTE_H);
    g_assert_cmpint(boxes[0].x, ==, 0);
    g_assert_cmpint(boxes[2].x, ==, 50);
    g_assert_cmpint(boxes[1].x, ==, 100);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/canvas_geometry/snap_coord", test_snap_coord);
    g_test_add_func("/canvas_geometry/snap_box", test_snap_box);
    g_test_add_func("/canvas_geometry/align_boxes", test_align_boxes);
    return g_test_run();
}
