/* Unit tests for src/theme_store.c: theme files, inheritance, save and the
 * remembered choice. */
#include "theme_store.h"
#include "palette.h"
#include "fonts.h"

#include <glib/gstdio.h>
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
    g_assert_cmpuint(themes->len, ==, 3);
    Theme* first = g_ptr_array_index(themes, 0);
    g_assert_cmpstr(first->name, ==, "Dark Modern");
    g_assert_nonnull(first->path); /* the user's version */
    g_assert_cmpstr(((Theme*)g_ptr_array_index(themes, 1))->name, ==, "Alpha");
    g_assert_cmpstr(((Theme*)g_ptr_array_index(themes, 2))->name, ==, "Beta");
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
    g_free(path);
    remove_dir(dir);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/theme_store/parse", test_parse);
    g_test_add_func("/theme_store/parse_errors", test_parse_errors);
    g_test_add_func("/theme_store/list", test_list);
    g_test_add_func("/theme_store/apply_inheritance", test_apply_inheritance);
    g_test_add_func("/theme_store/apply_errors", test_apply_errors);
    g_test_add_func("/theme_store/save_round_trip", test_save_round_trip);
    g_test_add_func("/theme_store/choice", test_choice);
    return g_test_run();
}
