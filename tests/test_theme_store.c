/* Unit tests for src/theme_store.c: theme files, inheritance, save and the
 * remembered choice. */
#include "theme_store.h"
#include "palette.h"
#include "fonts.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

static gchar* make_dir(void) {
    return g_dir_make_tmp("lookin-themes-XXXXXX", NULL);
}

static void write_file(const gchar* dir, const gchar* name, const gchar* text) {
    gchar* path = g_build_filename(dir, name, NULL);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(path);
}

static void remove_dir(const gchar* dir) {
    GDir* d = g_dir_open(dir, 0, NULL);
    const gchar* entry;
    while (d != NULL && (entry = g_dir_read_name(d)) != NULL) {
        gchar* path = g_build_filename(dir, entry, NULL);
        g_remove(path);
        g_free(path);
    }
    if (d != NULL) g_dir_close(d);
    g_rmdir(dir);
}

static void test_parse(void) {
    GError* error = NULL;
    Theme* t = theme_parse("# a comment\n"
                           "[Theme]\nname = Sand \ndark=false\ninherits=Dark Modern\n"
                           "[Colors]\nbg_window=#ABC\naccent = #123456\n"
                           "[Fonts]\nui=Ubuntu, sans-serif\nui_size=14\ncode_size=15\n",
                           "/x/sand.ini", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(t->name, ==, "Sand");
    g_assert_false(t->dark);
    g_assert_true(t->has_dark);
    g_assert_cmpstr(t->inherits, ==, "Dark Modern");
    g_assert_cmpstr(t->path, ==, "/x/sand.ini");
    g_assert_cmpstr(g_hash_table_lookup(t->colors, "bg_window"), ==, "#aabbcc");
    g_assert_cmpstr(g_hash_table_lookup(t->colors, "accent"), ==, "#123456");
    g_assert_cmpstr(t->font_family[FONT_UI], ==, "Ubuntu, sans-serif");
    g_assert_null(t->font_family[FONT_CODE]);
    g_assert_cmpint(t->font_size[FONT_UI], ==, 14);
    g_assert_cmpint(t->font_size[FONT_CODE], ==, 15);
    theme_free(t);
}

static void expect_error(const gchar* text, const gchar* fragment);

static void test_icon_set(void) {
    g_assert_true(theme_store_icon_set_valid("desktop"));
    g_assert_true(theme_store_icon_set_valid("my_set-2"));
    g_assert_false(theme_store_icon_set_valid(""));
    g_assert_false(theme_store_icon_set_valid("../x"));
    g_assert_false(theme_store_icon_set_valid("a b"));

    GError* error = NULL;
    GPtrArray* themes = g_ptr_array_new_with_free_func(theme_free);
    g_ptr_array_add(themes, theme_parse("[Theme]\nname=Retro\nicons=desktop\n", NULL, &error));
    g_assert_no_error(error);
    g_ptr_array_add(themes, theme_parse("[Theme]\nname=Kid\ninherits=Retro\n", NULL, &error));
    g_ptr_array_add(themes, theme_parse("[Theme]\nname=Plain\ninherits=Retro\nicons=default\n", NULL, &error));
    g_ptr_array_add(themes, theme_parse("[Theme]\nname=Other\ninherits=Retro\nicons=flat\n", NULL, &error));
    g_assert_no_error(error);
    g_assert_true(theme_store_apply(themes, "Retro", NULL, NULL));
    g_assert_cmpstr(theme_store_icon_set(), ==, "desktop");
    g_assert_true(theme_store_apply(themes, "Kid", NULL, NULL));
    g_assert_cmpstr(theme_store_icon_set(), ==, "desktop");  /* inherited */
    g_assert_true(theme_store_apply(themes, "Other", NULL, NULL));
    g_assert_cmpstr(theme_store_icon_set(), ==, "flat");
    g_assert_true(theme_store_apply(themes, "Plain", NULL, NULL));
    g_assert_null(theme_store_icon_set());  /* "default" */
    g_assert_true(theme_store_apply(themes, "Retro", NULL, NULL));
    gchar* ini = theme_store_current_to_ini("Copy", FALSE);
    g_assert_nonnull(strstr(ini, "icons=desktop"));
    g_free(ini);
    g_ptr_array_free(themes, TRUE);

    expect_error("[Theme]\nname=A\nicons=../etc\n", "[Theme] icons:");
}

static void expect_error(const gchar* text, const gchar* fragment) {
    GError* error = NULL;
    Theme* t = theme_parse(text, NULL, &error);
    g_assert_null(t);
    g_assert_nonnull(error);
    if (strstr(error->message, fragment) == NULL) {
        g_printerr("message \"%s\" lacks \"%s\"\n", error->message, fragment);
        g_assert_not_reached();
    }
    g_error_free(error);
}

static void test_parse_errors(void) {
    expect_error("not ini at all", "not a theme file");
    expect_error("[Colors]\nfg=#fff\n", "[Theme] group is missing");
    expect_error("[Theme]\ndark=true\n", "name is missing");
    expect_error("[Theme]\nname=A\ndark=maybe\n", "dark");
    expect_error("[Theme]\nname=A\ncolour=red\n", "[Theme] colour: unknown key");
    expect_error("[Theme]\nname=A\n[Extra]\nx=1\n", "[Extra]: unknown group");
    expect_error("[Theme]\nname=A\n[Colors]\nacent=#fff\n", "[Colors] acent: unknown colour role");
    expect_error("[Theme]\nname=A\n[Colors]\naccent=blue\n", "[Colors] accent: \"blue\" is not a colour");
    expect_error("[Theme]\nname=A\n[Fonts]\nui=Evil'}\n", "[Fonts] ui:");
    expect_error("[Theme]\nname=A\n[Fonts]\ncode_size=big\n", "[Fonts] code_size:");
    expect_error("[Theme]\nname=A\n[Fonts]\ncode_size=200\n", "[Fonts] code_size:");
    expect_error("[Theme]\nname=A\n[Fonts]\nheading=X\n", "[Fonts] heading: unknown key");
    expect_error("[Theme]\nname=A\ninherits=A\n", "itself");
}

static void test_list(void) {
    gchar* dir = make_dir();
    write_file(dir, "b.ini", "[Theme]\nname=Beta\n");
    write_file(dir, "a.ini", "[Theme]\nname=Alpha\n");
    write_file(dir, "broken.ini", "[Theme]\nname=Broken\n[Colors]\nfg=nope\n");
    write_file(dir, "notes.txt", "[Theme]\nname=Ignored\n");
    /* A user theme named like a built-in one replaces it */
    write_file(dir, "dark.ini", "[Theme]\nname=Dark Modern\n[Colors]\naccent=#ff0000\n");
    GPtrArray* errors = g_ptr_array_new_with_free_func(g_free);
    GPtrArray* themes = theme_store_list(dir, errors);
    /* The 3 built-in themes, then the user's in file name order */
    g_assert_cmpuint(themes->len, ==, 5);
    Theme* first = g_ptr_array_index(themes, 0);
    g_assert_cmpstr(first->name, ==, "Dark Modern");
    g_assert_nonnull(first->path); /* the user's version */
    g_assert_cmpstr(((Theme*)g_ptr_array_index(themes, 1))->name, ==, "Light Modern");
    g_assert_cmpstr(((Theme*)g_ptr_array_index(themes, 2))->name, ==, "High Contrast Dark");
    g_assert_cmpstr(((Theme*)g_ptr_array_index(themes, 3))->name, ==, "Alpha");
    g_assert_cmpstr(((Theme*)g_ptr_array_index(themes, 4))->name, ==, "Beta");
    g_assert_null(theme_store_find(themes, "Ignored"));
    g_assert_cmpuint(errors->len, ==, 1);
    g_assert_nonnull(strstr(g_ptr_array_index(errors, 0), "broken.ini"));
    g_ptr_array_free(themes, TRUE);
    g_ptr_array_free(errors, TRUE);

    /* No folder: the built-in themes only */
    themes = theme_store_list("/nonexistent/lookin-themes", NULL);
    g_assert_nonnull(theme_store_find(themes, THEME_DEFAULT_NAME));
    g_ptr_array_free(themes, TRUE);
    remove_dir(dir);
    g_free(dir);
}

static void test_apply_inheritance(void) {
    gchar* dir = make_dir();
    write_file(dir, "base.ini", "[Theme]\nname=Base\ndark=false\n[Colors]\nfg=#111111\naccent=#222222\n"
                                "[Fonts]\ncode=Hack\ncode_size=15\n");
    write_file(dir, "child.ini", "[Theme]\nname=Child\ninherits=Base\n[Colors]\naccent=#333333\n");
    GPtrArray* themes = theme_store_list(dir, NULL);
    gboolean dark = TRUE;
    GError* error = NULL;
    g_assert_true(theme_store_apply(themes, "Child", &dark, &error));
    g_assert_no_error(error);
    g_assert_false(dark); /* inherited */
    g_assert_cmpstr(palette_color(PAL_FG), ==, "#111111");
    g_assert_cmpstr(palette_color(PAL_ACCENT), ==, "#333333");
    g_assert_cmpstr(palette_color(PAL_BG_WINDOW), ==, palette_default(PAL_BG_WINDOW));
    g_assert_cmpstr(fonts_family(FONT_CODE), ==, "Hack");
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 15);
    g_assert_cmpint(fonts_size(FONT_UI), ==, 13);

    /* Back to the default: everything reset */
    g_assert_true(theme_store_apply(themes, THEME_DEFAULT_NAME, &dark, NULL));
    g_assert_true(dark);
    g_assert_cmpstr(palette_color(PAL_FG), ==, palette_default(PAL_FG));
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 13);
    g_ptr_array_free(themes, TRUE);
    remove_dir(dir);
    g_free(dir);
}

static void test_apply_errors(void) {
    gchar* dir = make_dir();
    write_file(dir, "a.ini", "[Theme]\nname=A\ninherits=B\n[Colors]\nfg=#010101\n");
    write_file(dir, "b.ini", "[Theme]\nname=B\ninherits=A\n");
    write_file(dir, "c.ini", "[Theme]\nname=C\ninherits=Missing\n");
    GPtrArray* themes = theme_store_list(dir, NULL);
    palette_reset();
    GError* error = NULL;
    g_assert_false(theme_store_apply(themes, "A", NULL, &error));
    g_assert_nonnull(strstr(error->message, "loop"));
    g_clear_error(&error);
    g_assert_false(theme_store_apply(themes, "C", NULL, &error));
    g_assert_nonnull(strstr(error->message, "Missing"));
    g_clear_error(&error);
    g_assert_false(theme_store_apply(themes, "Nope", NULL, &error));
    g_clear_error(&error);
    /* Nothing changed */
    g_assert_cmpstr(palette_color(PAL_FG), ==, palette_default(PAL_FG));
    g_ptr_array_free(themes, TRUE);
    remove_dir(dir);
    g_free(dir);
}

static void test_save_round_trip(void) {
    gchar* dir = make_dir();
    palette_reset();
    fonts_reset();
    palette_set(PAL_ACCENT, "#abcdef");
    fonts_set(FONT_UI, "Ubuntu, sans-serif", 15);
    GError* error = NULL;
    gchar* path = theme_store_save_current(dir, "My  Theme!", FALSE, &error);
    g_assert_no_error(error);
    g_assert_true(g_str_has_suffix(path, "/my-theme.ini"));
    palette_reset();
    fonts_reset();

    GPtrArray* themes = theme_store_list(dir, NULL);
    gboolean dark = TRUE;
    g_assert_true(theme_store_apply(themes, "My  Theme!", &dark, NULL));
    g_assert_false(dark);
    g_assert_cmpstr(palette_color(PAL_ACCENT), ==, "#abcdef");
    g_assert_cmpstr(fonts_family(FONT_UI), ==, "Ubuntu, sans-serif");
    g_assert_cmpint(fonts_size(FONT_UI), ==, 15);
    g_assert_null(theme_store_save_current(dir, "  ", TRUE, &error));
    g_clear_error(&error);
    palette_reset();
    fonts_reset();
    g_ptr_array_free(themes, TRUE);
    g_free(path);
    remove_dir(dir);
    g_free(dir);
}

static void test_choice(void) {
    gchar* dir = make_dir();
    gchar* path = g_build_filename(dir, "theme.ini", NULL);
    gchar* name = theme_store_load_choice(path);
    g_assert_cmpstr(name, ==, THEME_DEFAULT_NAME);
    g_free(name);
    g_assert_true(theme_store_save_choice(path, "Light Modern", NULL));
    name = theme_store_load_choice(path);
    g_assert_cmpstr(name, ==, "Light Modern");
    g_free(name);

    /* Fonts saved next to the choice, keeping it, and applied over a theme */
    FontChoice fonts = {0};
    fonts.family[FONT_CODE] = g_strdup("Hack, monospace");
    fonts.size[FONT_UI] = 15;
    g_assert_true(theme_store_save_fonts(path, &fonts, NULL));
    font_choice_clear(&fonts);
    name = theme_store_load_choice(path);
    g_assert_cmpstr(name, ==, "Light Modern");
    g_free(name);
    theme_store_load_fonts(path, &fonts);
    g_assert_cmpstr(fonts.family[FONT_CODE], ==, "Hack, monospace");
    g_assert_null(fonts.family[FONT_UI]);
    g_assert_cmpint(fonts.size[FONT_UI], ==, 15);
    g_assert_cmpint(fonts.size[FONT_CODE], ==, 0);
    /* Saving the choice keeps the fonts */
    g_assert_true(theme_store_save_choice(path, "Dark Modern", NULL));
    font_choice_clear(&fonts);
    theme_store_load_fonts(path, &fonts);
    g_assert_cmpint(fonts.size[FONT_UI], ==, 15);
    fonts_reset();
    theme_store_apply_fonts(&fonts);
    g_assert_cmpstr(fonts_family(FONT_CODE), ==, "Hack, monospace");
    g_assert_cmpint(fonts_size(FONT_UI), ==, 15);
    g_assert_cmpint(fonts_size(FONT_CODE), ==, 13);
    fonts_reset();
    font_choice_clear(&fonts);

    /* Invalid values are ignored */
    g_assert_true(g_file_set_contents(path, "[Fonts]\nui=Bad'}\nui_size=500\ncode_size=x\n", -1, NULL));
    theme_store_load_fonts(path, &fonts);
    g_assert_null(fonts.family[FONT_UI]);
    g_assert_cmpint(fonts.size[FONT_UI], ==, 0);
    g_assert_cmpint(fonts.size[FONT_CODE], ==, 0);
    g_free(path);
    remove_dir(dir);
    g_free(dir);
}

/* WCAG relative luminance and contrast ratio of two palette roles */
static double channel(double c) {
    return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double luminance(PaletteRole role) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

static double contrast(PaletteRole a, PaletteRole b) {
    double la = luminance(a), lb = luminance(b);
    return (MAX(la, lb) + 0.05) / (MIN(la, lb) + 0.05);
}

/* Apply a theme and check that its main text is readable */
static void assert_readable(GPtrArray* themes, const gchar* name) {
    static const struct {
        PaletteRole text, background;
        double min;
    } PAIRS[] = {
        {PAL_FG_TEXT, PAL_BG_VIEW, 4.5},      {PAL_FG, PAL_BG_WINDOW, 4.5},
        {PAL_FG, PAL_BG_BAR, 4.5},            {PAL_FG, PAL_BG_RAISED, 4.5},
        {PAL_FG, PAL_BG_INPUT, 4.5},          {PAL_FG_MUTED, PAL_BG_BAR, 4.5},
        {PAL_FG_SUBTLE, PAL_BG_VIEW, 4.5},    {PAL_FG_BRIGHT, PAL_BG_SELECTED, 4.5},
        {PAL_ACCENT_FG, PAL_BG_VIEW, 4.5},    {PAL_ACCENT_TEXT, PAL_ACCENT, 4.5},
        {PAL_LINK, PAL_BG_VIEW, 4.5},         {PAL_ERROR, PAL_BG_VIEW, 4.5},
        {PAL_WARNING, PAL_BG_VIEW, 4.5},      {PAL_SYN_COMMENT, PAL_BG_VIEW, 4.5},
        {PAL_SYN_KEYWORD, PAL_BG_VIEW, 4.5},  {PAL_SYN_STRING, PAL_BG_VIEW, 4.5},
        {PAL_SYN_FUNCTION, PAL_BG_VIEW, 4.5}, {PAL_TERM_FG, PAL_BG_TERMINAL, 4.5},
        {PAL_DIFF_ADDED_FG, PAL_DIFF_ADDED_BG, 4.5}, {PAL_DIFF_REMOVED_FG, PAL_DIFF_REMOVED_BG, 4.5},
        {PAL_HIGHLIGHT_FG, PAL_HIGHLIGHT, 4.5},
        /* Bold badges: large-text level */
        {PAL_BADGE_FG, PAL_BADGE_RED, 3.0},   {PAL_BADGE_FG, PAL_BADGE_GREEN, 3.0},
        {PAL_BADGE_FG, PAL_BADGE_BLUE, 3.0},  {PAL_BADGE_FG, PAL_BADGE_ORANGE, 3.0},
    };
    GError* error = NULL;
    g_assert_true(theme_store_apply(themes, name, NULL, &error));
    g_assert_no_error(error);
    for (gsize p = 0; p < G_N_ELEMENTS(PAIRS); p++) {
        double c = contrast(PAIRS[p].text, PAIRS[p].background);
        if (c < PAIRS[p].min) {
            g_printerr("%s: %s on %s is %.2f:1 (needs %.1f)\n", name, palette_name(PAIRS[p].text),
                       palette_name(PAIRS[p].background), c, PAIRS[p].min);
            g_assert_not_reached();
        }
    }
}

/* Every built-in theme applies, and keeps its main text readable */
static void test_builtin_contrast(void) {
    GPtrArray* themes = theme_store_list(NULL, NULL);
    g_assert_cmpuint(themes->len, ==, 3);
    for (guint i = 0; i < themes->len; i++) {
        assert_readable(themes, ((Theme*)g_ptr_array_index(themes, i))->name);
    }
    g_ptr_array_free(themes, TRUE);
    palette_reset();
    fonts_reset();
}

/* The themes of themes/ (one per decade) are built in: they parse, are
 * complete (every colour and font), and keep their text readable. */
static void test_bundled_themes(void) {
    static const gchar* const NAMES[] = {"1970s Phosphor", "1980s Text Mode", "1990s Desktop",
                                         "2000s Glossy",   "2010s Flat Dark", "2020s Pastel"};
    GPtrArray* themes = theme_store_list(NULL, NULL);
    for (gsize n = 0; n < G_N_ELEMENTS(NAMES); n++) {
        Theme* t = theme_store_find(themes, NAMES[n]);
        g_assert_nonnull(t);
        g_assert_null(t->path);
        g_assert_null(t->inherits);
        g_assert_true(t->has_dark);
        g_assert_cmpuint(g_hash_table_size(t->colors), ==, PAL_COUNT);
        for (gint r = 0; r < FONT_COUNT; r++) {
            g_assert_nonnull(t->font_family[r]);
            g_assert_cmpint(t->font_size[r], >, 0);
        }
        assert_readable(themes, t->name);
    }
    g_ptr_array_free(themes, TRUE);
    palette_reset();
    fonts_reset();
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/theme_store/parse", test_parse);
    g_test_add_func("/theme_store/parse_errors", test_parse_errors);
    g_test_add_func("/theme_store/list", test_list);
    g_test_add_func("/theme_store/apply_inheritance", test_apply_inheritance);
    g_test_add_func("/theme_store/apply_errors", test_apply_errors);
    g_test_add_func("/theme_store/icon_set", test_icon_set);
    g_test_add_func("/theme_store/save_round_trip", test_save_round_trip);
    g_test_add_func("/theme_store/choice", test_choice);
    g_test_add_func("/theme_store/builtin_contrast", test_builtin_contrast);
    g_test_add_func("/theme_store/bundled_themes", test_bundled_themes);
    return g_test_run();
}
