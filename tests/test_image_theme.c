/* Unit tests for src/theme/image_theme.c: theme colours from an image. */
#include "theme/image_theme.h"

#include <math.h>

/* A width x height RGB image: the left part (split columns) in colour a,
 * the rest in colour b */
static guint8* two_colours(gint width, gint height, gint split, const guint8 a[3], const guint8 b[3]) {
    guint8* px = g_new(guint8, (gsize)width * height * 3);
    for (gint y = 0; y < height; y++) {
        for (gint x = 0; x < width; x++) {
            const guint8* c = (x < split) ? a : b;
            guint8* p = px + ((gsize)y * width + x) * 3;
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
        }
    }
    return px;
}

static void rgb_of(const gchar* hex, gint rgb[3]) {
    guint v = (guint)strtoul(hex + 1, NULL, 16);
    rgb[0] = (gint)(v >> 16) & 0xff;
    rgb[1] = (gint)(v >> 8) & 0xff;
    rgb[2] = (gint)v & 0xff;
}

/* Every role is a colour and the main text pairs are readable */
static void check_theme(const ImageTheme* t) {
    for (gint i = 0; i < PAL_COUNT; i++) {
        gchar norm[8];
        g_assert_true(palette_parse_color(t->colors[i], norm));
        g_assert_cmpstr(norm, ==, t->colors[i]);
    }
    const PaletteRole TEXT[] = {PAL_FG, PAL_FG_TEXT, PAL_FG_STRONG};
    for (gsize i = 0; i < G_N_ELEMENTS(TEXT); i++) {
        g_assert_cmpfloat(image_theme_contrast(t->colors[TEXT[i]], t->colors[PAL_BG_VIEW]), >=, 7.0);
    }
    g_assert_cmpfloat(image_theme_contrast(t->colors[PAL_FG_MUTED], t->colors[PAL_BG_VIEW]), >=, 4.5);
    g_assert_cmpfloat(image_theme_contrast(t->colors[PAL_SYN_KEYWORD], t->colors[PAL_BG_VIEW]), >=, 4.5);
    g_assert_cmpfloat(image_theme_contrast(t->colors[PAL_SYN_STRING], t->colors[PAL_BG_VIEW]), >=, 4.5);
    g_assert_cmpfloat(image_theme_contrast(t->colors[PAL_ACCENT_FG], t->colors[PAL_BG_VIEW]), >=, 4.5);
    g_assert_cmpfloat(image_theme_contrast(t->colors[PAL_ACCENT_TEXT], t->colors[PAL_ACCENT]), >=, 3.0);
}

static void test_contrast(void) {
    g_assert_cmpfloat(fabs(image_theme_contrast("#000", "#fff") - 21.0), <, 0.01);
    g_assert_cmpfloat(fabs(image_theme_contrast("#777777", "#777777") - 1.0), <, 0.001);
    g_assert_cmpfloat(image_theme_contrast("red", "#fff"), ==, 0.0);
}

/* A dark blue image with an orange spot: dark theme, blue-tinted
 * surfaces, orange accent */
static void test_dark_image(void) {
    const guint8 navy[3] = {16, 24, 48};
    const guint8 orange[3] = {240, 120, 20};
    guint8* px = two_colours(100, 50, 90, navy, orange);
    ImageTheme t;
    g_assert_true(image_theme_derive(px, 100, 50, 300, 3, &t));
    g_free(px);
    g_assert_true(t.dark);
    check_theme(&t);
    gint bg[3], acc[3];
    rgb_of(t.colors[PAL_BG_VIEW], bg);
    rgb_of(t.colors[PAL_ACCENT], acc);
    g_assert_cmpint(bg[2], >, bg[0]); /* bluish background */
    g_assert_cmpint(acc[0], >, acc[2]); /* orange accent */
    g_assert_cmpint(acc[0], >, acc[1]);
}

/* A light image gives a light theme */
static void test_light_image(void) {
    const guint8 paper[3] = {250, 246, 230};
    const guint8 teal[3] = {0, 140, 140};
    guint8* px = two_colours(64, 64, 56, paper, teal);
    ImageTheme t;
    g_assert_true(image_theme_derive(px, 64, 64, 64 * 3, 3, &t));
    g_free(px);
    g_assert_false(t.dark);
    check_theme(&t);
    gint acc[3];
    rgb_of(t.colors[PAL_ACCENT], acc);
    g_assert_cmpint(acc[1], >, acc[0]); /* teal accent */
}

/* A grey image still gives a usable theme with a coloured accent */
static void test_grey_image(void) {
    const guint8 grey[3] = {40, 40, 40};
    guint8* px = two_colours(10, 10, 10, grey, grey);
    ImageTheme t;
    g_assert_true(image_theme_derive(px, 10, 10, 30, 3, &t));
    g_free(px);
    g_assert_true(t.dark);
    check_theme(&t);
    gint acc[3];
    rgb_of(t.colors[PAL_ACCENT], acc);
    g_assert_cmpint(acc[2], >, acc[0] + 40);
}

/* Transparent pixels are ignored; a fully transparent image gives nothing;
 * the result does not change between runs */
static void test_alpha_and_errors(void) {
    guint8 px[4 * 4 * 4];
    for (gsize i = 0; i < sizeof px; i += 4) {
        px[i] = 255;
        px[i + 1] = 255;
        px[i + 2] = 255;
        px[i + 3] = 0;
    }
    ImageTheme t;
    g_assert_false(image_theme_derive(px, 4, 4, 16, 4, &t));
    /* One opaque dark pixel among transparent white ones */
    px[0] = px[1] = px[2] = 10;
    px[3] = 255;
    g_assert_true(image_theme_derive(px, 4, 4, 16, 4, &t));
    g_assert_true(t.dark);
    ImageTheme again;
    g_assert_true(image_theme_derive(px, 4, 4, 16, 4, &again));
    for (gint i = 0; i < PAL_COUNT; i++) {
        g_assert_cmpstr(t.colors[i], ==, again.colors[i]);
    }
    g_assert_false(image_theme_derive(NULL, 4, 4, 16, 4, &t));
    g_assert_false(image_theme_derive(px, 4, 4, 8, 4, &t));
    g_assert_false(image_theme_derive(px, 4, 4, 16, 2, &t));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/image_theme/contrast", test_contrast);
    g_test_add_func("/image_theme/dark", test_dark_image);
    g_test_add_func("/image_theme/light", test_light_image);
    g_test_add_func("/image_theme/grey", test_grey_image);
    g_test_add_func("/image_theme/alpha", test_alpha_and_errors);
    return g_test_run();
}
