/* Unit tests for src/text_tags.c: text tags whose colours and fonts are
 * palette / font roles, set again after a theme change. Headless. */
#include "text_tags.h"
#include "fonts.h"
#include "palette.h"

static gchar* tag_string(GtkTextTag* tag, const gchar* property) {
    gchar* value = NULL;
    g_object_get(tag, property, &value, NULL);
    return value;
}

static gchar* rgba_hex(const GdkRGBA* c) {
    return g_strdup_printf("#%02x%02x%02x", (guint)(c->red * 255 + 0.5), (guint)(c->green * 255 + 0.5),
                           (guint)(c->blue * 255 + 0.5));
}

static gchar* tag_color(GtkTextTag* tag, const gchar* property) {
    GdkRGBA* c = NULL;
    g_object_get(tag, property, &c, NULL);
    g_assert_nonnull(c);
    gchar* hex = rgba_hex(c);
    gdk_rgba_free(c);
    return hex;
}

static void test_resolve(void) {
    palette_reset();
    fonts_reset();
    g_assert_cmpstr(text_tags_resolve("@accent"), ==, palette_default(PAL_ACCENT));
    g_assert_cmpstr(text_tags_resolve("@font_code"), ==, fonts_family(FONT_CODE));
    g_assert_null(text_tags_resolve("@nope"));
    g_assert_null(text_tags_resolve("#123456"));
    g_assert_null(text_tags_resolve(NULL));
}

static void test_create_and_refresh(void) {
    palette_reset();
    fonts_reset();
    GtkTextBuffer* buf = gtk_text_buffer_new(NULL);
    GtkTextTag* tag = text_tag_create(buf, "t", "foreground", "@accent", "weight", 700, "family", "@font_code",
                                      "underline-rgba", "@error", "paragraph-background", "#102030", NULL);
    gint weight = 0;
    g_object_get(tag, "weight", &weight, NULL);
    g_assert_cmpint(weight, ==, 700);
    gchar* s = tag_color(tag, "foreground-rgba");
    g_assert_cmpstr(s, ==, palette_default(PAL_ACCENT));
    g_free(s);
    s = tag_string(tag, "family");
    g_assert_cmpstr(s, ==, fonts_family(FONT_CODE));
    g_free(s);

    /* Theme change: roles follow, literal values stay */
    palette_set(PAL_ACCENT, "#ff0000");
    palette_set(PAL_ERROR, "#00ff00");
    fonts_set(FONT_CODE, "Hack", 0);
    text_tags_refresh_all();
    s = tag_color(tag, "foreground-rgba");
    g_assert_cmpstr(s, ==, "#ff0000");
    g_free(s);
    s = tag_color(tag, "underline-rgba");
    g_assert_cmpstr(s, ==, "#00ff00");
    g_free(s);
    s = tag_string(tag, "family");
    g_assert_cmpstr(s, ==, "Hack");
    g_free(s);
    s = tag_color(tag, "paragraph-background-rgba");
    g_assert_cmpstr(s, ==, "#102030");
    g_free(s);

    /* ensure: an existing tag is returned as is */
    g_assert_true(text_tag_ensure(buf, "t", "foreground", "@fg", NULL) == tag);
    GtkTextTag* other = text_tag_ensure(buf, "u", "background", "@bg_view", NULL);
    g_assert_true(other != tag);

    /* A finalized tag is forgotten (no crash on refresh) */
    g_object_unref(buf);
    text_tags_refresh_all();
    palette_reset();
    fonts_reset();
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/text_tags/resolve", test_resolve);
    g_test_add_func("/text_tags/create_and_refresh", test_create_and_refresh);
    return g_test_run();
}
