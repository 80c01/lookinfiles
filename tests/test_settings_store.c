/* Unit tests for src/core/settings_store.c: settings.ini */
#include <string.h>
#include "core/settings_store.h"

#include <glib/gstdio.h>

static void test_editor_names(void) {
    SettingsEditor e = SETTINGS_EDITOR_NOTEPAD;
    g_assert_true(settings_editor_parse("Vim", &e));
    g_assert_cmpint(e, ==, SETTINGS_EDITOR_VIM);
    g_assert_true(settings_editor_parse("emacs", &e));
    g_assert_cmpint(e, ==, SETTINGS_EDITOR_EMACS);
    g_assert_false(settings_editor_parse("nano", &e));
    g_assert_false(settings_editor_parse(NULL, &e));
    g_assert_cmpint(e, ==, SETTINGS_EDITOR_EMACS); /* unchanged */
    g_assert_cmpstr(settings_editor_name(SETTINGS_EDITOR_NOTEPAD), ==, "notepad");
    g_assert_cmpstr(settings_editor_name(SETTINGS_EDITOR_VIM), ==, "vim");
}

static void test_missing_file(void) {
    Settings s;
    s.width = 1;
    g_assert_false(settings_store_load("/nonexistent/settings.ini", &s));
    g_assert_cmpint(s.editor, ==, SETTINGS_EDITOR_NOTEPAD);
    g_assert_false(s.line_numbers);
    g_assert_false(s.wrap);
    g_assert_false(s.tagbar);
    g_assert_false(s.preview_tagbar);
    g_assert_true(s.minimap);
    g_assert_cmpint(s.auto_terminal, ==, SETTINGS_PANEL_KEEP);
    g_assert_cmpint(s.auto_preview, ==, SETTINGS_PANEL_KEEP);
    g_assert_cmpint(s.auto_tagbar, ==, SETTINGS_PANEL_KEEP);
    g_assert_cmpint(s.auto_terminal_place, ==, SETTINGS_PLACE_KEEP);
    g_assert_cmpint(s.color_column, ==, 81);
    g_assert_cmpint(s.timer_minutes, ==, 60);
    g_assert_cmpint(s.width, ==, 1000);
    g_assert_cmpint(s.height, ==, 600);
}

static void test_save_load(void) {
    gchar* dir = g_dir_make_tmp("settings-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "sub", "settings.ini", NULL);
    /* Another group is kept by a save */
    gchar* sub = g_path_get_dirname(path);
    g_mkdir_with_parents(sub, 0700);
    g_assert_true(g_file_set_contents(path, "[Other]\nkey=value\n", -1, NULL));

    Settings s;
    settings_defaults(&s);
    s.editor = SETTINGS_EDITOR_VIM;
    s.line_numbers = TRUE;
    s.wrap = TRUE;
    s.tagbar = TRUE;
    s.preview_tagbar = TRUE;
    s.minimap = FALSE;
    s.auto_terminal = SETTINGS_PANEL_SHOW;
    s.auto_preview = SETTINGS_PANEL_HIDE;
    s.auto_tagbar = SETTINGS_PANEL_HIDE;
    s.auto_terminal_place = SETTINGS_PLACE_BESIDE;
    s.color_column = 81;
    s.timer_minutes = 0;
    s.width = 1920;
    s.height = 1080;
    g_assert_true(settings_store_save(path, &s, NULL));
    gchar* written = NULL;
    g_assert_true(g_file_get_contents(path, &written, NULL, NULL));
    g_assert_nonnull(strstr(written, "[Preview]\ntagbar=true"));
    g_free(written);

    Settings r;
    g_assert_true(settings_store_load(path, &r));
    g_assert_cmpint(r.editor, ==, SETTINGS_EDITOR_VIM);
    g_assert_true(r.line_numbers);
    g_assert_true(r.wrap);
    g_assert_true(r.tagbar);
    g_assert_true(r.preview_tagbar);
    g_assert_false(r.minimap);
    g_assert_cmpint(r.auto_terminal, ==, SETTINGS_PANEL_SHOW);
    g_assert_cmpint(r.auto_preview, ==, SETTINGS_PANEL_HIDE);
    g_assert_cmpint(r.auto_tagbar, ==, SETTINGS_PANEL_HIDE);
    g_assert_cmpint(r.auto_terminal_place, ==, SETTINGS_PLACE_BESIDE);
    g_assert_cmpint(r.color_column, ==, 81);
    g_assert_cmpint(r.timer_minutes, ==, 0);
    g_assert_cmpint(r.width, ==, 1920);
    g_assert_cmpint(r.height, ==, 1080);

    gchar* text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(g_strstr_len(text, -1, "key=value"));
    g_free(text);

    /* Back to keeping the panels: the keys go away */
    r.auto_terminal = SETTINGS_PANEL_KEEP;
    r.auto_preview = SETTINGS_PANEL_KEEP;
    r.auto_tagbar = SETTINGS_PANEL_KEEP;
    r.auto_terminal_place = SETTINGS_PLACE_KEEP;
    g_assert_true(settings_store_save(path, &r, NULL));
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_null(strstr(text, "auto-"));
    g_free(text);
    g_assert_true(settings_store_load(path, &r));
    g_assert_cmpint(r.auto_terminal, ==, SETTINGS_PANEL_KEEP);

    g_remove(path);
    g_rmdir(sub);
    g_rmdir(dir);
    g_free(sub);
    g_free(path);
    g_free(dir);
}

static void test_invalid_values(void) {
    gchar* dir = g_dir_make_tmp("settings-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "settings.ini", NULL);
    g_assert_true(g_file_set_contents(path,
                                      "[Editor]\nmode=nano\nline-numbers=maybe\nwrap=true\ncolor-column=-3\n"
                                      "[Clock]\ntimer-minutes=5000\n[Window]\nwidth=10\nheight=abc\n",
                                      -1, NULL));
    Settings s;
    g_assert_true(settings_store_load(path, &s));
    g_assert_cmpint(s.editor, ==, SETTINGS_EDITOR_NOTEPAD);
    g_assert_false(s.line_numbers);
    g_assert_true(s.wrap);
    g_assert_cmpint(s.color_column, ==, 81);
    g_assert_cmpint(s.timer_minutes, ==, 60);
    g_assert_cmpint(s.width, ==, 1000);
    g_assert_cmpint(s.height, ==, 600);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/settings_store/editor_names", test_editor_names);
    g_test_add_func("/settings_store/missing_file", test_missing_file);
    g_test_add_func("/settings_store/save_load", test_save_load);
    g_test_add_func("/settings_store/invalid_values", test_invalid_values);
    return g_test_run();
}
