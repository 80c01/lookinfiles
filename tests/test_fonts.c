/* Unit tests for src/fonts.c: application fonts by role. */
#include "fonts.h"

static void test_defaults(void) {
    fonts_reset();
    g_assert_cmpint(fonts_size(FONT_UI), ==, 13);
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 13);
    g_assert_true(g_str_has_suffix(fonts_family(FONT_UI), "sans-serif"));
    g_assert_true(g_str_has_suffix(fonts_family(FONT_CODE), "monospace"));
}

static void test_set_and_reset(void) {
    fonts_reset();
    g_assert_true(fonts_set(FONT_CODE, "Fira Code, monospace", 15));
    g_assert_cmpstr(fonts_family(FONT_CODE), ==, "Fira Code, monospace");
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 15);
    /* NULL / 0 keep the current value */
    g_assert_true(fonts_set(FONT_CODE, NULL, 14));
    g_assert_cmpstr(fonts_family(FONT_CODE), ==, "Fira Code, monospace");
    g_assert_true(fonts_set(FONT_CODE, "Iosevka", 0));
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 14);
    /* Invalid values change nothing */
    g_assert_false(fonts_set(FONT_CODE, "Evil'; }", 0));
    g_assert_false(fonts_set(FONT_CODE, "A,,B", 0));
    g_assert_false(fonts_set(FONT_CODE, "", 0));
    g_assert_false(fonts_set(FONT_CODE, NULL, 200));
    g_assert_cmpstr(fonts_family(FONT_CODE), ==, "Iosevka");
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 14);
    fonts_reset();
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 13);
}

static void test_css_family(void) {
    fonts_reset();
    fonts_set(FONT_UI, "Segoe UI,  Noto Sans , sans-serif", 0);
    gchar* css = fonts_css_family(FONT_UI);
    g_assert_cmpstr(css, ==, "'Segoe UI', 'Noto Sans', sans-serif");
    g_free(css);
    fonts_reset();
}

static void test_expand(void) {
    fonts_reset();
    fonts_set(FONT_UI, "Ubuntu, sans-serif", 15);
    fonts_set(FONT_CODE, "Hack", 12);
    gchar* s = fonts_expand("a { font-family: @font_ui; font-size: @size_small; color: @fg; }"
                            " b { font: @size_bar @font_code; } c { font-size: @size_title; } d { font-size: @size_code; }"
                            " e { font-size: @size_normal; }");
    g_assert_cmpstr(s, ==,
                    "a { font-family: 'Ubuntu', sans-serif; font-size: 13px; color: @fg; }"
                    " b { font: 14px 'Hack'; } c { font-size: 18px; } d { font-size: 12px; }"
                    " e { font-size: 15px; }");
    g_free(s);
    fonts_reset();
}

static void test_zoom(void) {
    fonts_reset();
    fonts_mark_base();
    g_assert_cmpint(fonts_zoom_percent(), ==, 100);
    fonts_set(FONT_CODE, NULL, 16);
    g_assert_cmpint(fonts_zoom_percent(), ==, 123);
    fonts_mark_base();
    g_assert_cmpint(fonts_zoom_percent(), ==, 100);
    fonts_reset();
    fonts_mark_base();
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/fonts/defaults", test_defaults);
    g_test_add_func("/fonts/set_and_reset", test_set_and_reset);
    g_test_add_func("/fonts/css_family", test_css_family);
    g_test_add_func("/fonts/expand", test_expand);
    g_test_add_func("/fonts/zoom", test_zoom);
    return g_test_run();
}
