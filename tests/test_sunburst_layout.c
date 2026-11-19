/* Unit tests for src/core/sunburst_layout.c: the frame, the arcs
 * proportional to the weights, a point found in its ring and angle. */
#include "core/sunburst_layout.h"

#include <math.h>

#define EPS 1e-9

static void test_frame(void) {
    SunburstFrame f = sunburst_frame(400, 300, 4, 10);
    g_assert_cmpfloat(f.cx, ==, 200);
    g_assert_cmpfloat(f.cy, ==, 150);
    g_assert_cmpuint(f.rings, ==, 4);
    /* hole + rings = the radius (140) */
    g_assert_cmpfloat(fabs(f.hole + 4 * f.ring - 140), <, EPS);
    g_assert_cmpfloat(f.hole, >, f.ring);
    gdouble in, out;
    sunburst_ring_radii(&f, 0, &in, &out);
    g_assert_cmpfloat(fabs(in - f.hole), <, EPS);
    g_assert_cmpfloat(fabs(out - in - f.ring), <, EPS);
}

static void test_split(void) {
    gdouble w[] = { 1, 0, 3, -2 };
    gdouble s[5];
    sunburst_split(w, 4, 0, 2 * G_PI, s);
    g_assert_cmpfloat(s[0], ==, 0);
    g_assert_cmpfloat(fabs(s[1] - G_PI / 2), <, EPS);
    g_assert_cmpfloat(s[2], ==, s[1]); /* 0: nothing */
    g_assert_cmpfloat(fabs(s[3] - 2 * G_PI), <, EPS);
    g_assert_cmpfloat(s[4], ==, 2 * G_PI);
    gdouble z[] = { 0, 0 };
    sunburst_split(z, 2, 1, 2, s);
    g_assert_cmpfloat(s[2], ==, 1); /* no weight: empty arcs */
}

static void test_locate(void) {
    SunburstFrame f = sunburst_frame(200, 200, 2, 0); /* radius 100, ring 100/3.5 */
    gint ring;
    gdouble a;
    g_assert_true(sunburst_locate(&f, 100, 100, &ring, &a));
    g_assert_cmpint(ring, ==, -1);
    g_assert_true(sunburst_locate(&f, 100, 100 - f.hole - 1, &ring, &a)); /* above the centre */
    g_assert_cmpint(ring, ==, 0);
    g_assert_cmpfloat(fabs(a), <, 1e-6);
    g_assert_true(sunburst_locate(&f, 100 + f.hole + f.ring + 1, 100, &ring, &a)); /* right */
    g_assert_cmpint(ring, ==, 1);
    g_assert_cmpfloat(fabs(a - G_PI / 2), <, 1e-6);
    g_assert_true(sunburst_locate(&f, 100 - f.hole - 1, 100, &ring, &a)); /* left */
    g_assert_cmpfloat(fabs(a - 3 * G_PI / 2), <, 1e-6);
    g_assert_false(sunburst_locate(&f, 199.9, 199.9, &ring, &a)); /* a corner, outside */
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sunburst_layout/frame", test_frame);
    g_test_add_func("/sunburst_layout/split", test_split);
    g_test_add_func("/sunburst_layout/locate", test_locate);
    return g_test_run();
}
