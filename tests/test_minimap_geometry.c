/* Unit tests for src/minimap_geometry.c: minimap scale, scroll and slider. */
#include "minimap_geometry.h"

#define EPS 1e-6

static void test_short_document_is_not_blown_up(void) {
    MinimapGeometry g;
    g_assert_true(minimap_geometry_compute(1000, 800, 0, 600, &g));
    g_assert_cmpfloat_with_epsilon(g.scale, MINIMAP_MAX_SCALE, EPS);
    g_assert_cmpfloat_with_epsilon(g.offset, 0, EPS);
    g_assert_cmpfloat_with_epsilon(g.slider_height, 200, EPS);
}

static void test_medium_document_fits(void) {
    MinimapGeometry g;
    /* 4000 px of content in a 600 px minimap: scale 0.15, no scrolling */
    g_assert_true(minimap_geometry_compute(4000, 800, 3200, 600, &g));
    g_assert_cmpfloat_with_epsilon(g.scale, 0.15, EPS);
    g_assert_cmpfloat_with_epsilon(g.offset, 0, EPS);
    g_assert_cmpfloat_with_epsilon(g.slider_top + g.slider_height, 600, EPS);
}

static void test_long_document_scrolls(void) {
    MinimapGeometry g;
    /* 100000 px at the minimum scale: 12500 px of minimap content */
    g_assert_true(minimap_geometry_compute(100000, 1000, 0, 600, &g));
    g_assert_cmpfloat_with_epsilon(g.scale, MINIMAP_MIN_SCALE, EPS);
    g_assert_cmpfloat_with_epsilon(g.offset, 0, EPS);
    g_assert_cmpfloat_with_epsilon(g.slider_top, 0, EPS);

    /* At the end: the last minimap pixel shows the end of the document */
    g_assert_true(minimap_geometry_compute(100000, 1000, 99000, 600, &g));
    g_assert_cmpfloat_with_epsilon(g.offset, 12500 - 600, EPS);
    g_assert_cmpfloat_with_epsilon(g.slider_top + g.slider_height, 600, EPS);

    /* Half way: slider half way along its track */
    g_assert_true(minimap_geometry_compute(100000, 1000, 49500, 600, &g));
    g_assert_cmpfloat_with_epsilon(g.slider_top, (600 - 125) / 2.0, EPS);
}

static void test_slider_drag_round_trip(void) {
    MinimapGeometry g;
    gdouble values[] = {0, 1234, 49500, 99000};
    for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
        minimap_geometry_compute(100000, 1000, values[i], 600, &g);
        gdouble v = minimap_geometry_value_for_slider(&g, 100000, 1000, 600, g.slider_top);
        g_assert_cmpfloat_with_epsilon(v, values[i], 1e-3);
    }
    minimap_geometry_compute(4000, 800, 1000, 600, &g);
    g_assert_cmpfloat_with_epsilon(
        minimap_geometry_value_for_slider(&g, 4000, 800, 600, g.slider_top), 1000, 1e-3);
    /* Dragged past the ends: clamped */
    g_assert_cmpfloat_with_epsilon(minimap_geometry_value_for_slider(&g, 4000, 800, 600, -50), 0, EPS);
    g_assert_cmpfloat_with_epsilon(minimap_geometry_value_for_slider(&g, 4000, 800, 600, 9999), 3200, EPS);
}

static void test_click_centers_line(void) {
    MinimapGeometry g;
    minimap_geometry_compute(100000, 1000, 49500, 600, &g);
    /* The line drawn at minimap y 300 is at document y (300 + offset) / scale */
    gdouble doc_y = (300 + g.offset) / g.scale;
    gdouble v = minimap_geometry_value_centered(&g, 100000, 1000, 300);
    g_assert_cmpfloat_with_epsilon(v, doc_y - 500, EPS);
    /* Near the top: clamped to 0 */
    minimap_geometry_compute(100000, 1000, 0, 600, &g);
    g_assert_cmpfloat_with_epsilon(minimap_geometry_value_centered(&g, 100000, 1000, 10), 0, EPS);
}

static void test_nothing_to_show(void) {
    MinimapGeometry g;
    g_assert_false(minimap_geometry_compute(0, 800, 0, 600, &g));
    g_assert_false(minimap_geometry_compute(1000, 800, 0, 0, &g));
    g_assert_cmpfloat_with_epsilon(minimap_geometry_value_centered(&g, 1000, 800, 10), 0, EPS);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/minimap_geometry/short", test_short_document_is_not_blown_up);
    g_test_add_func("/minimap_geometry/medium", test_medium_document_fits);
    g_test_add_func("/minimap_geometry/long_scrolls", test_long_document_scrolls);
    g_test_add_func("/minimap_geometry/slider_drag", test_slider_drag_round_trip);
    g_test_add_func("/minimap_geometry/click_centers", test_click_centers_line);
    g_test_add_func("/minimap_geometry/nothing", test_nothing_to_show);
    return g_test_run();
}
