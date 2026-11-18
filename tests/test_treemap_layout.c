/* Unit tests for src/core/treemap_layout.c: areas proportional to the
 * weights, the area tiled without overlap, squares where they can be. */
#include "core/treemap_layout.h"

#include <math.h>

#define EPS 1e-6

static void check_tiling(const gdouble* weights, guint n, TreemapRect area) {
    TreemapRect* out = g_new(TreemapRect, n);
    treemap_squarify(weights, n, area, out);
    gdouble total = 0.0, covered = 0.0;
    for (guint i = 0; i < n; i++) total += weights[i] > 0.0 ? weights[i] : 0.0;
    for (guint i = 0; i < n; i++) {
        const TreemapRect* r = &out[i];
        g_assert_cmpfloat(r->w, >=, -EPS);
        g_assert_cmpfloat(r->h, >=, -EPS);
        g_assert_cmpfloat(r->x, >=, area.x - EPS);
        g_assert_cmpfloat(r->y, >=, area.y - EPS);
        g_assert_cmpfloat(r->x + r->w, <=, area.x + area.w + EPS);
        g_assert_cmpfloat(r->y + r->h, <=, area.y + area.h + EPS);
        gdouble want = weights[i] > 0.0 ? weights[i] / total * area.w * area.h : 0.0;
        g_assert_cmpfloat(fabs(r->w * r->h - want), <, 1e-3 * area.w * area.h + EPS);
        covered += r->w * r->h;
        for (guint j = i + 1; j < n; j++) {
            const TreemapRect* q = &out[j];
            gdouble ox = MIN(r->x + r->w, q->x + q->w) - MAX(r->x, q->x);
            gdouble oy = MIN(r->y + r->h, q->y + q->h) - MAX(r->y, q->y);
            g_assert_true(ox <= EPS || oy <= EPS); /* no overlap */
        }
    }
    g_assert_cmpfloat(fabs(covered - area.w * area.h), <, 1e-3 * area.w * area.h);
    g_free(out);
}

static void test_single(void) {
    gdouble w[] = { 5.0 };
    TreemapRect out[1];
    treemap_squarify(w, 1, (TreemapRect){ 10, 20, 300, 100 }, out);
    g_assert_cmpfloat(out[0].x, ==, 10);
    g_assert_cmpfloat(out[0].y, ==, 20);
    g_assert_cmpfloat(out[0].w, ==, 300);
    g_assert_cmpfloat(out[0].h, ==, 100);
}

/* Four equal weights in a square: four squares */
static void test_equal_squares(void) {
    gdouble w[] = { 1, 1, 1, 1 };
    TreemapRect out[4];
    treemap_squarify(w, 4, (TreemapRect){ 0, 0, 200, 200 }, out);
    for (guint i = 0; i < 4; i++) {
        g_assert_cmpfloat(fabs(out[i].w - 100), <, EPS);
        g_assert_cmpfloat(fabs(out[i].h - 100), <, EPS);
    }
}

static void test_tiling(void) {
    gdouble a[] = { 6, 6, 4, 3, 2, 2, 1 }; /* the example of the paper */
    check_tiling(a, G_N_ELEMENTS(a), (TreemapRect){ 0, 0, 600, 400 });
    check_tiling(a, G_N_ELEMENTS(a), (TreemapRect){ 5, 7, 90, 700 });
    gdouble b[] = { 1000, 1, 1, 1, 0.5 };
    check_tiling(b, G_N_ELEMENTS(b), (TreemapRect){ 0, 0, 640, 480 });
    gdouble c[50];
    for (guint i = 0; i < 50; i++) c[i] = 50 - i;
    check_tiling(c, 50, (TreemapRect){ 0, 0, 1000, 300 });
}

/* The paper's example in 6x4: the two largest side by side on the left */
static void test_squarified(void) {
    gdouble w[] = { 6, 6, 4, 3, 2, 2, 1 };
    TreemapRect out[7];
    treemap_squarify(w, 7, (TreemapRect){ 0, 0, 6, 4 }, out);
    g_assert_cmpfloat(fabs(out[0].w - 3), <, EPS);
    g_assert_cmpfloat(fabs(out[0].h - 2), <, EPS);
    g_assert_cmpfloat(fabs(out[1].y - 2), <, EPS);
    for (guint i = 0; i < 7; i++) {
        gdouble ratio = MAX(out[i].w / out[i].h, out[i].h / out[i].w);
        g_assert_cmpfloat(ratio, <, 3.0);
    }
}

static void test_empty(void) {
    gdouble w[] = { 3, 0, 1 };
    TreemapRect out[3];
    treemap_squarify(w, 3, (TreemapRect){ 0, 0, 40, 10 }, out);
    g_assert_cmpfloat(out[1].w * out[1].h, ==, 0);
    g_assert_cmpfloat(fabs(out[0].w * out[0].h - 300), <, EPS);
    g_assert_cmpfloat(fabs(out[2].w * out[2].h - 100), <, EPS);
    treemap_squarify(w, 3, (TreemapRect){ 0, 0, 0, 10 }, out);
    for (guint i = 0; i < 3; i++) g_assert_cmpfloat(out[i].w * out[i].h, ==, 0);
    gdouble zero[] = { 0, 0 };
    treemap_squarify(zero, 2, (TreemapRect){ 0, 0, 10, 10 }, out);
    g_assert_cmpfloat(out[0].w, ==, 0);
}

static void test_contains(void) {
    TreemapRect r = { 10, 10, 5, 5 };
    g_assert_true(treemap_rect_contains(&r, 10, 10));
    g_assert_true(treemap_rect_contains(&r, 14.9, 14.9));
    g_assert_false(treemap_rect_contains(&r, 15, 12));
    g_assert_false(treemap_rect_contains(&r, 9.9, 12));
    g_assert_false(treemap_rect_contains(NULL, 0, 0));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/treemap_layout/single", test_single);
    g_test_add_func("/treemap_layout/equal_squares", test_equal_squares);
    g_test_add_func("/treemap_layout/tiling", test_tiling);
    g_test_add_func("/treemap_layout/squarified", test_squarified);
    g_test_add_func("/treemap_layout/empty", test_empty);
    g_test_add_func("/treemap_layout/contains", test_contains);
    return g_test_run();
}
