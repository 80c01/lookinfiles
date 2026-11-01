/* Themes: built-in and user theme files (see theme_store.h). */
#include "theme_store.h"
#include "app_paths.h"
#include "palette.h"

#include <glib/gstdio.h>
#include <string.h>

/* Built-in themes, in list order. Dark Modern is the palette and font
 * defaults. */
static const gchar* const BUILTIN_THEMES[] = {
    "[Theme]\n"
    "name=" THEME_DEFAULT_NAME "\n"
    "dark=true\n",
    /* VS Code Light Modern / Light+ */
    "[Theme]\n"
    "name=Light Modern\n"
    "dark=false\n"
    "[Colors]\n"
    "bg_window=#f8f8f8\n"
    "bg_bar=#f3f3f3\n"
    "bg_view=#ffffff\n"
    "bg_terminal=#ffffff\n"
    "bg_raised=#f8f8f8\n"
    "bg_input=#ffffff\n"
    "bg_block=#f3f3f3\n"
    "bg_code_inline=#ebebeb\n"
    "bg_hover=#ebebeb\n"
    "bg_selected=#cfe3f8\n"
    "bg_selected_inactive=#e4e6f1\n"
    "hover_overlay=#9a9a9a\n"
    "shadow=#000000\n"
    "scrollbar=#646464\n"
    "border=#e5e5e5\n"
    "border_strong=#cecece\n"
    "rule=#dcdcdc\n"
    "fg=#3b3b3b\n"
    "fg_text=#1f1f1f\n"
    "fg_strong=#111111\n"
    "fg_bright=#000000\n"
    "fg_muted=#5f5f5f\n"
    "fg_subtle=#6b6b6b\n"
    "fg_faint=#8b8b8b\n"
    "accent=#005fb8\n"
    "accent_hover=#0258a8\n"
    "accent_fg=#005fb8\n"
    "accent_text=#ffffff\n"
    "focus_ring=#005fb8\n"
    "link=#005fb8\n"
    "error=#c4314b\n"
    "warning=#895503\n"
    "success=#2e7d32\n"
    "highlight=#ffd33d\n"
    "highlight_fg=#1f1f1f\n"
    "git_modified=#895503\n"
    "git_added=#007100\n"
    "git_deleted=#ad0707\n"
    "git_conflict=#ad0707\n"
    "git_renamed=#005fb8\n"
    "icon_blue=#498ba7\n"
    "icon_grey=#455155\n"
    "icon_grey_light=#627379\n"
    "icon_green=#7fae42\n"
    "icon_orange=#cc6d2e\n"
    "icon_pink=#dd4b78\n"
    "icon_purple=#9068b0\n"
    "icon_red=#b8383d\n"
    "icon_white=#6c6c6c\n"
    "icon_yellow=#b7b73b\n"
    "icon_ignore=#8a9499\n"
    "gutter_added=#2ea043\n"
    "gutter_modified=#005fb8\n"
    "gutter_deleted=#e5534b\n"
    "diff_added_fg=#1a5e1a\n"
    "diff_added_bg=#e6ffec\n"
    "diff_removed_fg=#8b1a1a\n"
    "diff_removed_bg=#ffebe9\n"
    "diff_hunk_fg=#0550ae\n"
    "diff_hunk_bg=#ddf4ff\n"
    "syn_comment=#008000\n"
    "syn_keyword=#0000ff\n"
    "syn_control=#af00db\n"
    "syn_string=#a31515\n"
    "syn_number=#098658\n"
    "syn_type=#267f99\n"
    "syn_function=#795e26\n"
    "syn_variable=#001080\n"
    "syn_escape=#a35c00\n"
    "syn_constant=#0070c1\n"
    "syn_error=#e51400\n"
    "editor_selection=#add6ff\n"
    "editor_current_line=#f5f5f5\n"
    "editor_cursor=#000000\n"
    "editor_bracket=#d0e8d0\n"
    "editor_occurrence=#e0e0e0\n"
    "editor_match=#f8d5b0\n"
    "editor_match_current=#f0b46c\n"
    "editor_ruler=#e51400\n"
    "term_fg=#3b3b3b\n"
    "term_cursor=#000000\n"
    "ansi_black=#000000\n"
    "ansi_red=#cd3131\n"
    "ansi_green=#107c10\n"
    "ansi_yellow=#949800\n"
    "ansi_blue=#0451a5\n"
    "ansi_magenta=#bc05bc\n"
    "ansi_cyan=#0598bc\n"
    "ansi_white=#555555\n"
    "ansi_bright_black=#666666\n"
    "ansi_bright_red=#cd3131\n"
    "ansi_bright_green=#14ce14\n"
    "ansi_bright_yellow=#b5ba00\n"
    "ansi_bright_blue=#0451a5\n"
    "ansi_bright_magenta=#bc05bc\n"
    "ansi_bright_cyan=#0598bc\n"
    "ansi_bright_white=#a5a5a5\n",
    /* High contrast on black, from Dark Modern */
    "[Theme]\n"
    "name=High Contrast Dark\n"
    "dark=true\n"
    "inherits=" THEME_DEFAULT_NAME "\n"
    "[Colors]\n"
    "bg_window=#000000\n"
    "bg_bar=#000000\n"
    "bg_view=#000000\n"
    "bg_terminal=#000000\n"
    "bg_raised=#0b0b0b\n"
    "bg_input=#000000\n"
    "bg_block=#111111\n"
    "bg_code_inline=#1c1c1c\n"
    "bg_hover=#1f1f1f\n"
    "bg_selected=#0f4a85\n"
    "bg_selected_inactive=#333333\n"
    "hover_overlay=#ffffff\n"
    "border=#6fc3df\n"
    "border_strong=#6fc3df\n"
    "rule=#7f7f7f\n"
    "fg=#ffffff\n"
    "fg_text=#ffffff\n"
    "fg_strong=#ffffff\n"
    "fg_bright=#ffffff\n"
    "fg_muted=#e0e0e0\n"
    "fg_subtle=#c8c8c8\n"
    "fg_faint=#a8a8a8\n"
    "accent=#0f4a85\n"
    "accent_hover=#1b5c9e\n"
    "accent_fg=#6fc3df\n"
    "focus_ring=#f38518\n"
    "link=#21a6ff\n"
    "error=#f48771\n"
    "warning=#ffd700\n"
    "success=#89d185\n"
    "highlight=#ffd700\n"
    "highlight_fg=#000000\n"
    "diff_added_bg=#0f2b10\n"
    "diff_removed_bg=#3a0f0f\n"
    "diff_hunk_bg=#0b2230\n"
    "syn_comment=#7ca668\n"
    "syn_error=#f48771\n"
    "editor_current_line=#111111\n"
    "editor_cursor=#ffffff\n"
    "editor_bracket=#555555\n"
    "editor_occurrence=#333333\n"
    "term_fg=#ffffff\n",
};

/* [Fonts] keys of each role */
static const struct {
    const gchar* family_key;
    const gchar* size_key;
} FONT_KEYS[FONT_COUNT] = {
    [FONT_UI] = {"ui", "ui_size"},
    [FONT_CODE] = {"code", "code_size"},
};

#define THEME_ERROR (g_quark_from_static_string("theme-store-error"))

void theme_free(gpointer data) {
    Theme* t = data;
    if (t == NULL) {
        return;
    }
    g_free(t->name);
    g_free(t->inherits);
    g_free(t->path);
    if (t->colors != NULL) {
        g_hash_table_unref(t->colors);
    }
    for (gint i = 0; i < FONT_COUNT; i++) {
        g_free(t->font_family[i]);
    }
    g_free(t);
}

/* ------------------------------------------------------------------------- */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------- */

static gboolean fail(GError** error, const gchar* format, ...) G_GNUC_PRINTF(2, 3);

static gboolean fail(GError** error, const gchar* format, ...) {
    va_list args;
    va_start(args, format);
    gchar* message = g_strdup_vprintf(format, args);
    va_end(args);
    g_set_error_literal(error, THEME_ERROR, 0, message);
    g_free(message);
    return FALSE;
}

static gboolean parse_theme_group(GKeyFile* kf, Theme* t, GError** error) {
    gchar** keys = g_key_file_get_keys(kf, "Theme", NULL, NULL);
    gboolean ok = TRUE;
    for (gint i = 0; ok && keys != NULL && keys[i] != NULL; i++) {
        const gchar* key = keys[i];
        gchar* value = g_key_file_get_string(kf, "Theme", key, NULL);
        gchar* v = g_strstrip(value != NULL ? value : g_strdup(""));
        if (strcmp(key, "name") == 0) {
            if (v[0] == '\0') {
                ok = fail(error, "[Theme] name is empty");
            } else {
                g_free(t->name);
                t->name = g_strdup(v);
            }
        } else if (strcmp(key, "inherits") == 0) {
            g_free(t->inherits);
            t->inherits = (v[0] != '\0') ? g_strdup(v) : NULL;
        } else if (strcmp(key, "dark") == 0) {
            if (g_ascii_strcasecmp(v, "true") == 0) {
                t->dark = TRUE;
            } else if (g_ascii_strcasecmp(v, "false") == 0) {
                t->dark = FALSE;
            } else {
                ok = fail(error, "[Theme] dark: \"%s\" is not true or false", v);
            }
            t->has_dark = TRUE;
        } else {
            ok = fail(error, "[Theme] %s: unknown key (name, dark, inherits)", key);
        }
        g_free(v);
    }
    g_strfreev(keys);
    if (ok && t->name == NULL) {
        ok = fail(error, "[Theme] name is missing");
    }
    return ok;
}

static gboolean parse_colors_group(GKeyFile* kf, Theme* t, GError** error) {
    gchar** keys = g_key_file_get_keys(kf, "Colors", NULL, NULL);
    gboolean ok = TRUE;
    for (gint i = 0; ok && keys != NULL && keys[i] != NULL; i++) {
        gchar* value = g_key_file_get_string(kf, "Colors", keys[i], NULL);
        gchar* v = g_strstrip(value != NULL ? value : g_strdup(""));
        gchar color[8];
        if (!palette_lookup(keys[i], NULL)) {
            ok = fail(error, "[Colors] %s: unknown colour role", keys[i]);
        } else if (!palette_parse_color(v, color)) {
            ok = fail(error, "[Colors] %s: \"%s\" is not a colour (#rgb or #rrggbb)", keys[i], v);
        } else {
            g_hash_table_replace(t->colors, g_strdup(keys[i]), g_strdup(color));
        }
        g_free(v);
    }
    g_strfreev(keys);
    return ok;
}

static gboolean parse_fonts_group(GKeyFile* kf, Theme* t, GError** error) {
    gchar** keys = g_key_file_get_keys(kf, "Fonts", NULL, NULL);
    gboolean ok = TRUE;
    for (gint i = 0; ok && keys != NULL && keys[i] != NULL; i++) {
        const gchar* key = keys[i];
        gchar* value = g_key_file_get_string(kf, "Fonts", key, NULL);
        gchar* v = g_strstrip(value != NULL ? value : g_strdup(""));
        gboolean known = FALSE;
        for (gint r = 0; ok && r < FONT_COUNT; r++) {
            if (strcmp(key, FONT_KEYS[r].family_key) == 0) {
                known = TRUE;
                if (!fonts_valid_family(v)) {
                    ok = fail(error, "[Fonts] %s: \"%s\" is not a list of font names", key, v);
                } else {
                    g_free(t->font_family[r]);
                    t->font_family[r] = g_strdup(v);
                }
            } else if (strcmp(key, FONT_KEYS[r].size_key) == 0) {
                known = TRUE;
                gchar* end = NULL;
                gint64 size = g_ascii_strtoll(v, &end, 10);
                if (v[0] == '\0' || *end != '\0' || size < FONTS_MIN_SIZE || size > FONTS_MAX_SIZE) {
                    ok = fail(error, "[Fonts] %s: \"%s\" is not a size in pixels (%d to %d)", key, v,
                              FONTS_MIN_SIZE, FONTS_MAX_SIZE);
                } else {
                    t->font_size[r] = (gint)size;
                }
            }
        }
        if (ok && !known) {
            ok = fail(error, "[Fonts] %s: unknown key (ui, ui_size, code, code_size)", key);
        }
        g_free(v);
    }
    g_strfreev(keys);
    return ok;
}

Theme* theme_parse(const gchar* text, const gchar* path, GError** error) {
    g_return_val_if_fail(text != NULL, NULL);
    GKeyFile* kf = g_key_file_new();
    GError* local = NULL;
    if (!g_key_file_load_from_data(kf, text, (gsize)-1, G_KEY_FILE_NONE, &local)) {
        fail(error, "not a theme file: %s", local->message);
        g_error_free(local);
        g_key_file_free(kf);
        return NULL;
    }
    Theme* t = g_new0(Theme, 1);
    t->dark = TRUE;
    t->path = g_strdup(path);
    t->colors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    gboolean ok = TRUE;
    gchar** groups = g_key_file_get_groups(kf, NULL);
    for (gint i = 0; ok && groups[i] != NULL; i++) {
        if (strcmp(groups[i], "Theme") != 0 && strcmp(groups[i], "Colors") != 0 &&
            strcmp(groups[i], "Fonts") != 0) {
            ok = fail(error, "[%s]: unknown group (Theme, Colors, Fonts)", groups[i]);
        }
    }
    g_strfreev(groups);
    if (ok && !g_key_file_has_group(kf, "Theme")) {
        ok = fail(error, "[Theme] group is missing");
    }
    ok = ok && parse_theme_group(kf, t, error) && parse_colors_group(kf, t, error) &&
         parse_fonts_group(kf, t, error);
    if (ok && t->inherits != NULL && strcmp(t->inherits, t->name) == 0) {
        ok = fail(error, "[Theme] inherits: a theme cannot inherit from itself");
    }
    g_key_file_free(kf);
    if (!ok) {
        theme_free(t);
        return NULL;
    }
    return t;
}

/* ------------------------------------------------------------------------- */
/* List                                                                      */
/* ------------------------------------------------------------------------- */

Theme* theme_store_find(GPtrArray* themes, const gchar* name) {
    for (guint i = 0; themes != NULL && name != NULL && i < themes->len; i++) {
        Theme* t = g_ptr_array_index(themes, i);
        if (strcmp(t->name, name) == 0) {
            return t;
        }
    }
    return NULL;
}

static void put_theme(GPtrArray* themes, Theme* theme) {
    for (guint i = 0; i < themes->len; i++) {
        Theme* t = g_ptr_array_index(themes, i);
        if (strcmp(t->name, theme->name) == 0) {
            theme_free(t);
            themes->pdata[i] = theme;
            return;
        }
    }
    g_ptr_array_add(themes, theme);
}

static gint compare_names(gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(const gchar* const*)a, *(const gchar* const*)b);
}

GPtrArray* theme_store_list(const gchar* dir, GPtrArray* errors) {
    GPtrArray* themes = g_ptr_array_new_with_free_func(theme_free);
    for (gsize i = 0; i < G_N_ELEMENTS(BUILTIN_THEMES); i++) {
        Theme* t = theme_parse(BUILTIN_THEMES[i], NULL, NULL);
        g_assert(t != NULL);
        g_ptr_array_add(themes, t);
    }
    GDir* d = (dir != NULL) ? g_dir_open(dir, 0, NULL) : NULL;
    if (d == NULL) {
        return themes;
    }
    /* Sorted file names: the same order on every run */
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    const gchar* entry;
    while ((entry = g_dir_read_name(d)) != NULL) {
        if (g_str_has_suffix(entry, ".ini")) {
            g_ptr_array_add(names, g_strdup(entry));
        }
    }
    g_dir_close(d);
    g_ptr_array_sort(names, compare_names);
    for (guint i = 0; i < names->len; i++) {
        gchar* path = g_build_filename(dir, g_ptr_array_index(names, i), NULL);
        gchar* text = NULL;
        GError* error = NULL;
        Theme* t = NULL;
        if (g_file_get_contents(path, &text, NULL, &error)) {
            t = theme_parse(text, path, &error);
        }
        if (t != NULL) {
            put_theme(themes, t);
        } else if (errors != NULL) {
            g_ptr_array_add(errors, g_strdup_printf("%s: %s", path, error->message));
        }
        g_clear_error(&error);
        g_free(text);
        g_free(path);
    }
    g_ptr_array_free(names, TRUE);
    return themes;
}

/* ------------------------------------------------------------------------- */
/* Apply                                                                     */
/* ------------------------------------------------------------------------- */

gboolean theme_store_apply(GPtrArray* themes, const gchar* name, gboolean* dark, GError** error) {
    /* The inheritance chain, from the theme to its root */
    GPtrArray* chain = g_ptr_array_new();
    const gchar* next = name;
    gboolean ok = TRUE;
    while (ok && next != NULL) {
        Theme* t = theme_store_find(themes, next);
        if (t == NULL) {
            ok = (chain->len == 0) ? fail(error, "no theme named \"%s\"", next)
                                   : fail(error, "%s inherits from \"%s\", which does not exist",
                                          ((Theme*)g_ptr_array_index(chain, chain->len - 1))->name, next);
        } else if (g_ptr_array_find(chain, t, NULL)) {
            ok = fail(error, "inheritance loop through \"%s\"", t->name);
        } else {
            g_ptr_array_add(chain, t);
            next = t->inherits;
        }
    }
    if (!ok) {
        g_ptr_array_free(chain, TRUE);
        return FALSE;
    }

    palette_reset();
    fonts_reset();
    gboolean is_dark = TRUE;
    for (gint i = (gint)chain->len - 1; i >= 0; i--) {
        Theme* t = g_ptr_array_index(chain, i);
        if (t->has_dark) {
            is_dark = t->dark;
        }
        GHashTableIter it;
        gpointer key, value;
        g_hash_table_iter_init(&it, t->colors);
        while (g_hash_table_iter_next(&it, &key, &value)) {
            PaletteRole role;
            if (palette_lookup(key, &role)) {
                palette_set(role, value);
            }
        }
        for (gint r = 0; r < FONT_COUNT; r++) {
            fonts_set((FontRole)r, t->font_family[r], t->font_size[r]);
        }
    }
    g_ptr_array_free(chain, TRUE);
    if (dark != NULL) {
        *dark = is_dark;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Save                                                                      */
/* ------------------------------------------------------------------------- */

gchar* theme_store_current_to_ini(const gchar* name, gboolean dark) {
    GKeyFile* kf = g_key_file_new();
    g_key_file_set_string(kf, "Theme", "name", name);
    g_key_file_set_boolean(kf, "Theme", "dark", dark);
    for (gint i = 0; i < PAL_COUNT; i++) {
        g_key_file_set_string(kf, "Colors", palette_name((PaletteRole)i), palette_color((PaletteRole)i));
        gchar* comment = g_strconcat(" ", palette_description((PaletteRole)i), NULL);
        g_key_file_set_comment(kf, "Colors", palette_name((PaletteRole)i), comment, NULL);
        g_free(comment);
    }
    for (gint r = 0; r < FONT_COUNT; r++) {
        g_key_file_set_string(kf, "Fonts", FONT_KEYS[r].family_key, fonts_family((FontRole)r));
        g_key_file_set_integer(kf, "Fonts", FONT_KEYS[r].size_key, fonts_size((FontRole)r));
    }
    gchar* data = g_key_file_to_data(kf, NULL, NULL);
    g_key_file_free(kf);
    return data;
}

gchar* theme_store_save_current(const gchar* dir, const gchar* name, gboolean dark, GError** error) {
    gchar* trimmed = g_strstrip(g_strdup(name != NULL ? name : ""));
    gboolean empty = (trimmed[0] == '\0');
    g_free(trimmed);
    if (empty) {
        fail(error, "the theme needs a name");
        return NULL;
    }
    GString* slug = g_string_new(NULL);
    for (const gchar* p = name; *p != '\0'; p++) {
        gchar c = g_ascii_tolower(*p);
        if (g_ascii_isalnum(c)) {
            g_string_append_c(slug, c);
        } else if (slug->len > 0 && slug->str[slug->len - 1] != '-') {
            g_string_append_c(slug, '-');
        }
    }
    while (slug->len > 0 && slug->str[slug->len - 1] == '-') {
        g_string_truncate(slug, slug->len - 1);
    }
    if (slug->len == 0) {
        g_string_assign(slug, "theme");
    }
    g_string_append(slug, ".ini");
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        fail(error, "cannot create %s", dir);
        g_string_free(slug, TRUE);
        return NULL;
    }
    gchar* path = g_build_filename(dir, slug->str, NULL);
    g_string_free(slug, TRUE);
    gchar* data = theme_store_current_to_ini(name, dark);
    gboolean ok = g_file_set_contents(path, data, -1, error);
    g_free(data);
    if (!ok) {
        g_free(path);
        return NULL;
    }
    return path;
}

/* ------------------------------------------------------------------------- */
/* Chosen theme                                                              */
/* ------------------------------------------------------------------------- */

gchar* theme_store_user_dir(void) {
    gchar* config = app_config_dir();
    gchar* dir = g_build_filename(config, "themes", NULL);
    g_free(config);
    return dir;
}

gchar* theme_store_choice_path(void) {
    gchar* config = app_config_dir();
    gchar* path = g_build_filename(config, "theme.ini", NULL);
    g_free(config);
    return path;
}

gchar* theme_store_load_choice(const gchar* path) {
    GKeyFile* kf = g_key_file_new();
    gchar* name = NULL;
    if (path != NULL && g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        name = g_key_file_get_string(kf, "Theme", "current", NULL);
        if (name != NULL) {
            g_strstrip(name);
        }
    }
    g_key_file_free(kf);
    if (name == NULL || name[0] == '\0') {
        g_free(name);
        name = g_strdup(THEME_DEFAULT_NAME);
    }
    return name;
}

/* theme.ini as it is (empty if missing) */
static GKeyFile* open_choice_file(const gchar* path) {
    GKeyFile* kf = g_key_file_new();
    if (path != NULL) {
        g_key_file_load_from_file(kf, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    }
    return kf;
}

static gboolean save_choice_file(GKeyFile* kf, const gchar* path, GError** error) {
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    return g_key_file_save_to_file(kf, path, error);
}

gboolean theme_store_save_choice(const gchar* path, const gchar* name, GError** error) {
    GKeyFile* kf = open_choice_file(path);
    g_key_file_set_string(kf, "Theme", "current", name);
    gboolean ok = save_choice_file(kf, path, error);
    g_key_file_free(kf);
    return ok;
}

void font_choice_clear(FontChoice* fonts) {
    for (gint r = 0; r < FONT_COUNT; r++) {
        g_clear_pointer(&fonts->family[r], g_free);
        fonts->size[r] = 0;
    }
}

void theme_store_load_fonts(const gchar* path, FontChoice* fonts) {
    font_choice_clear(fonts);
    GKeyFile* kf = open_choice_file(path);
    for (gint r = 0; r < FONT_COUNT; r++) {
        gchar* family = g_key_file_get_string(kf, "Fonts", FONT_KEYS[r].family_key, NULL);
        if (family != NULL && fonts_valid_family(g_strstrip(family))) {
            fonts->family[r] = family;
        } else {
            g_free(family);
        }
        GError* error = NULL;
        gint size = g_key_file_get_integer(kf, "Fonts", FONT_KEYS[r].size_key, &error);
        if (error == NULL && size >= FONTS_MIN_SIZE && size <= FONTS_MAX_SIZE) {
            fonts->size[r] = size;
        }
        g_clear_error(&error);
    }
    g_key_file_free(kf);
}

gboolean theme_store_save_fonts(const gchar* path, const FontChoice* fonts, GError** error) {
    GKeyFile* kf = open_choice_file(path);
    g_key_file_remove_group(kf, "Fonts", NULL);
    for (gint r = 0; r < FONT_COUNT; r++) {
        if (fonts->family[r] != NULL) {
            g_key_file_set_string(kf, "Fonts", FONT_KEYS[r].family_key, fonts->family[r]);
        }
        if (fonts->size[r] > 0) {
            g_key_file_set_integer(kf, "Fonts", FONT_KEYS[r].size_key, fonts->size[r]);
        }
    }
    gboolean ok = save_choice_file(kf, path, error);
    g_key_file_free(kf);
    return ok;
}

void theme_store_apply_fonts(const FontChoice* fonts) {
    for (gint r = 0; r < FONT_COUNT; r++) {
        fonts_set((FontRole)r, fonts->family[r], fonts->size[r]);
    }
}
