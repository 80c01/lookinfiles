/* Unit tests for src/palette.c: application colours by role. */
#include "palette.h"

#include <string.h>

static void test_defaults(void) {
    palette_reset();
    g_assert_cmpstr(palette_color(PAL_BG_WINDOW), ==, "#191a1b");
    g_assert_cmpstr(palette_name(PAL_BG_WINDOW), ==, "bg_window");
    g_assert_cmpstr(palette_default(PAL_ACCENT), ==, "#0078d4");
    /* Every role has a unique CSS name, a #rrggbb default and a description */
    for (gint i = 0; i < PAL_COUNT; i++) {
        const gchar* v = palette_default((PaletteRole)i);
        g_assert_cmpuint(strlen(v), ==, 7);
        g_assert_true(v[0] == '#');
        g_assert_nonnull(palette_description((PaletteRole)i));
        PaletteRole found;
        g_assert_true(palette_lookup(palette_name((PaletteRole)i), &found));
        g_assert_cmpint(found, ==, i);
    }
    g_assert_false(palette_lookup("no_such_role", NULL));
    /* The terminal takes the 16 ANSI colours as a run of roles */
    g_assert_cmpint(PAL_ANSI_BRIGHT_WHITE - PAL_ANSI_BLACK, ==, 15);
}

static void test_set_and_reset(void) {
    palette_reset();
    g_assert_true(palette_set(PAL_FG, "#ABC"));
    g_assert_cmpstr(palette_color(PAL_FG), ==, "#aabbcc");
    g_assert_true(palette_set(PAL_FG, "#102030"));
    g_assert_cmpstr(palette_color(PAL_FG), ==, "#102030");
    /* Invalid values change nothing */
    g_assert_false(palette_set(PAL_FG, "red"));
    g_assert_false(palette_set(PAL_FG, "#12345"));
    g_assert_false(palette_set(PAL_FG, "#12345g"));
    g_assert_false(palette_set(PAL_FG, NULL));
    g_assert_cmpstr(palette_color(PAL_FG), ==, "#102030");
    palette_reset();
    g_assert_cmpstr(palette_color(PAL_FG), ==, "#cccccc");
}

static void test_rgb(void) {
    palette_reset();
    palette_set(PAL_FG, "#ff8000");
    double r, g, b;
    palette_rgb(PAL_FG, &r, &g, &b);
    g_assert_cmpfloat_with_epsilon(r, 1.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(g, 128.0 / 255.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(b, 0.0, 1e-9);
    palette_reset();
}

static void test_css_defines(void) {
    palette_reset();
    gchar* css = palette_css_defines();
    g_assert_nonnull(strstr(css, "@define-color bg_window #191a1b;\n"));
    g_assert_nonnull(strstr(css, "@define-color ansi_bright_white #e5e5e5;\n"));
    g_free(css);
}

static void test_expand(void) {
    palette_reset();
    gchar* s = palette_expand("<style fg=\"@syn_comment\" bg=\"@bg_view\"/> mail@example @nope @");
    g_assert_cmpstr(s, ==, "<style fg=\"#6a9955\" bg=\"#121314\"/> mail@example @nope @");
    g_free(s);
    /* The longest name is taken: @fg_text is not @fg followed by "_text" */
    s = palette_expand("@fg_text;@fg;");
    g_assert_cmpstr(s, ==, "#d4d4d4;#cccccc;");
    g_free(s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/palette/defaults", test_defaults);
    g_test_add_func("/palette/set_and_reset", test_set_and_reset);
    g_test_add_func("/palette/rgb", test_rgb);
    g_test_add_func("/palette/css_defines", test_css_defines);
    g_test_add_func("/palette/expand", test_expand);
    return g_test_run();
}
