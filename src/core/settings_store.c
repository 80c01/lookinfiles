/* User preferences in settings.ini (see settings_store.h). */
#include "core/settings_store.h"
#include "core/app_paths.h"

#include <glib/gstdio.h>

static const gchar* const EDITOR_NAMES[] = {
    [SETTINGS_EDITOR_NOTEPAD] = "notepad",
    [SETTINGS_EDITOR_VIM] = "vim",
    [SETTINGS_EDITOR_EMACS] = "emacs",
};

void settings_defaults(Settings* s) {
    s->editor = SETTINGS_EDITOR_NOTEPAD;
    s->line_numbers = FALSE;
    s->wrap = FALSE;
    s->tagbar = FALSE;
    s->preview_tagbar = FALSE;
    s->color_column = 81;
    s->width = 1000;
    s->height = 600;
}

gboolean settings_editor_parse(const gchar* name, SettingsEditor* editor) {
    if (name == NULL) {
        return FALSE;
    }
    for (gsize i = 0; i < G_N_ELEMENTS(EDITOR_NAMES); i++) {
        if (g_ascii_strcasecmp(name, EDITOR_NAMES[i]) == 0) {
            *editor = (SettingsEditor)i;
            return TRUE;
        }
    }
    return FALSE;
}

const gchar* settings_editor_name(SettingsEditor editor) {
    return ((gsize)editor < G_N_ELEMENTS(EDITOR_NAMES)) ? EDITOR_NAMES[editor] : EDITOR_NAMES[0];
}

gchar* settings_store_path(void) {
    gchar* dir = app_config_dir();
    gchar* path = g_build_filename(dir, "settings.ini", NULL);
    g_free(dir);
    return path;
}

/* A boolean key, or its default when missing or invalid */
static gboolean read_bool(GKeyFile* kf, const gchar* group, const gchar* key, gboolean def) {
    GError* error = NULL;
    gboolean value = g_key_file_get_boolean(kf, group, key, &error);
    if (error != NULL) {
        g_error_free(error);
        return def;
    }
    return value;
}

/* An integer key within [min, max], or its default */
static gint read_int(GKeyFile* kf, const gchar* group, const gchar* key, gint min, gint max, gint def) {
    GError* error = NULL;
    gint value = g_key_file_get_integer(kf, group, key, &error);
    if (error != NULL) {
        g_error_free(error);
        return def;
    }
    return (value >= min && value <= max) ? value : def;
}

/* A size key within [min, SETTINGS_MAX_SIZE], or its default */
static gint read_size(GKeyFile* kf, const gchar* key, gint min, gint def) {
    return read_int(kf, "Window", key, min, SETTINGS_MAX_SIZE, def);
}

gboolean settings_store_load(const gchar* path, Settings* s) {
    settings_defaults(s);
    GKeyFile* kf = g_key_file_new();
    if (path == NULL || !g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        return FALSE;
    }
    gchar* mode = g_key_file_get_string(kf, "Editor", "mode", NULL);
    settings_editor_parse(mode, &s->editor);
    g_free(mode);
    s->line_numbers = read_bool(kf, "Editor", "line-numbers", s->line_numbers);
    s->wrap = read_bool(kf, "Editor", "wrap", s->wrap);
    s->tagbar = read_bool(kf, "Editor", "tagbar", s->tagbar);
    s->preview_tagbar = read_bool(kf, "Preview", "tagbar", s->preview_tagbar);
    s->color_column = read_int(kf, "Editor", "color-column", 0, SETTINGS_MAX_COLUMN, s->color_column);
    s->width = read_size(kf, "width", SETTINGS_MIN_WIDTH, s->width);
    s->height = read_size(kf, "height", SETTINGS_MIN_HEIGHT, s->height);
    g_key_file_free(kf);
    return TRUE;
}

gboolean settings_store_save(const gchar* path, const Settings* s, GError** error) {
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_string(kf, "Editor", "mode", settings_editor_name(s->editor));
    g_key_file_set_boolean(kf, "Editor", "line-numbers", s->line_numbers);
    g_key_file_set_boolean(kf, "Editor", "wrap", s->wrap);
    g_key_file_set_boolean(kf, "Editor", "tagbar", s->tagbar);
    g_key_file_set_boolean(kf, "Preview", "tagbar", s->preview_tagbar);
    g_key_file_set_integer(kf, "Editor", "color-column", s->color_column);
    g_key_file_set_integer(kf, "Window", "width", s->width);
    g_key_file_set_integer(kf, "Window", "height", s->height);
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    gboolean ok = g_key_file_save_to_file(kf, path, error);
    g_key_file_free(kf);
    return ok;
}
