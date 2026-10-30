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
        g_key_file_set_comment(kf, "Colors", palette_name((PaletteRole)i),
                               palette_description((PaletteRole)i), NULL);
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

gboolean theme_store_save_choice(const gchar* path, const gchar* name, GError** error) {
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    GKeyFile* kf = g_key_file_new();
    g_key_file_set_string(kf, "Theme", "current", name);
    gboolean ok = g_key_file_save_to_file(kf, path, error);
    g_key_file_free(kf);
    return ok;
}
