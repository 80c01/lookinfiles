/* Unit tests for src/canvas_geometry.c: grid snap and object alignment. */
#include "canvas_geometry.h"

#include <math.h>

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
    CanvasRect targets[] = {
        {100, 100, 50, 50},
        {300, 200, 100, 100},
    };
    gint x = 102, y = 10;
    CanvasGuides guides;

    /* Near target 0's left edge (100) */
    canvas_snap_box(&x, &y, 50, 50, targets, 2, FALSE, 0, &guides);
    g_assert_cmpint(x, ==, 100);
    g_assert_true(guides.active_x);

    /* Grid snap fallback when no guide */
    x = 17; y = 33;
    canvas_snap_box(&x, &y, 50, 50, targets, 2, TRUE, 16, &guides);
    g_assert_cmpint(x, ==, 16);
    g_assert_cmpint(y, ==, 32);
    g_assert_false(guides.active_x);
    g_assert_false(guides.active_y);
}

static void test_align_boxes(void) {
    CanvasRect boxes[] = {
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
    canvas_align_boxes(boxes, 3, CANVAS_DISTRIBUTE_H);
    g_assert_cmpint(boxes[0].x, ==, 0);
    g_assert_cmpint(boxes[1].x, ==, 50);
    g_assert_cmpint(boxes[2].x, ==, 100);
}

static void test_drag_box(void) {
    CanvasRect r;
    canvas_drag_box(100, 100, 40, 160, FALSE, &r);
    g_assert_cmpint(r.x, ==, 40);
    g_assert_cmpint(r.y, ==, 100);
    g_assert_cmpint(r.w, ==, 60);
    g_assert_cmpint(r.h, ==, 60);
    /* Square: the shorter side grows, towards the drag */
    canvas_drag_box(100, 100, 70, 180, TRUE, &r);
    g_assert_cmpint(r.x, ==, 20);
    g_assert_cmpint(r.y, ==, 100);
    g_assert_cmpint(r.w, ==, 80);
    g_assert_cmpint(r.h, ==, 80);
}

static void test_snap_angle(void) {
    gdouble x = 100, y = 7;
    canvas_snap_angle(0, 0, &x, &y);
    g_assert_cmpfloat_with_epsilon(y, 0.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(x, sqrt(100.0 * 100.0 + 49.0), 1e-9);
    x = -50;
    y = -45;
    canvas_snap_angle(0, 0, &x, &y);
    g_assert_cmpfloat_with_epsilon(x, y, 1e-9);
    g_assert_true(x < 0);
    /* No length: unchanged */
    x = 3;
    y = 4;
    canvas_snap_angle(3, 4, &x, &y);
    g_assert_cmpfloat(x, ==, 3);
    g_assert_cmpfloat(y, ==, 4);
}

static void test_map_coord(void) {
    g_assert_cmpfloat(canvas_map_coord(15, 10, 10, 100, 40), ==, 120);
    g_assert_cmpfloat(canvas_map_coord(5, 5, 0, 100, 40), ==, 120);
}

static void test_edge_point(void) {
    CanvasRect box = {0, 0, 100, 50};
    gdouble x = 0, y = 0;
    /* Right: the middle of the right side, for every outline, gap away */
    for (gint o = CANVAS_OUTLINE_RECT; o <= CANVAS_OUTLINE_DIAMOND; o++) {
        g_assert_true(canvas_edge_point((CanvasOutline)o, &box, 300, 25, 4, &x, &y));
        g_assert_cmpfloat_with_epsilon(x, 104, 1e-6);
        g_assert_cmpfloat_with_epsilon(y, 25, 1e-6);
    }
    /* Rectangle: towards the bottom-right corner, the corner */
    g_assert_true(canvas_edge_point(CANVAS_OUTLINE_RECT, &box, 150, 75, 0, &x, &y));
    g_assert_cmpfloat_with_epsilon(x, 100, 1e-6);
    g_assert_cmpfloat_with_epsilon(y, 50, 1e-6);
    /* Diamond: towards the same corner, half-way along the side */
    g_assert_true(canvas_edge_point(CANVAS_OUTLINE_DIAMOND, &box, 150, 75, 0, &x, &y));
    g_assert_cmpfloat_with_epsilon(x, 75, 1e-6);
    g_assert_cmpfloat_with_epsilon(y, 37.5, 1e-6);
    /* Ellipse: on the curve */
    g_assert_true(canvas_edge_point(CANVAS_OUTLINE_ELLIPSE, &box, 150, 75, 0, &x, &y));
    gdouble ex = (x - 50) / 50, ey = (y - 25) / 25;
    g_assert_cmpfloat_with_epsilon(ex * ex + ey * ey, 1.0, 1e-6);
    /* Above: the top side */
    g_assert_true(canvas_edge_point(CANVAS_OUTLINE_RECT, &box, 50, -100, 0, &x, &y));
    g_assert_cmpfloat_with_epsilon(x, 50, 1e-6);
    g_assert_cmpfloat_with_epsilon(y, 0, 1e-6);
    /* The centre itself: no direction */
    x = y = -1;
    g_assert_false(canvas_edge_point(CANVAS_OUTLINE_RECT, &box, 50, 25, 4, &x, &y));
    g_assert_cmpfloat(x, ==, -1);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/canvas_geometry/snap_coord", test_snap_coord);
    g_test_add_func("/canvas_geometry/snap_box", test_snap_box);
    g_test_add_func("/canvas_geometry/align_boxes", test_align_boxes);
    g_test_add_func("/canvas_geometry/drag_box", test_drag_box);
    g_test_add_func("/canvas_geometry/snap_angle", test_snap_angle);
    g_test_add_func("/canvas_geometry/map_coord", test_map_coord);
    g_test_add_func("/canvas_geometry/edge_point", test_edge_point);
    return g_test_run();
}
