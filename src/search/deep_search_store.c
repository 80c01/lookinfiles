/* Saved Deep Search criteria (see deep_search_store.h) */
#include "search/deep_search_store.h"
#include "core/app_paths.h"
#include "core/i18n.h"

#include <glib/gstdio.h>
#include <string.h>

#define GROUP_NAME "DeepSearch"
#define SEARCH_ID_MAX 32

/* Stable config names for FileFamily (FILE_FAMILY_DOCUMENTS .. FILE_FAMILY_PROGRAMS) */
static const gchar* const FAMILY_KEYS[FILE_FAMILY_OTHER] = {
    [FILE_FAMILY_DOCUMENTS] = "documents",
    [FILE_FAMILY_IMAGES]    = "images",
    [FILE_FAMILY_VIDEOS]    = "videos",
    [FILE_FAMILY_AUDIO]     = "audio",
    [FILE_FAMILY_ARCHIVES]  = "archives",
    [FILE_FAMILY_CODE]      = "code",
    [FILE_FAMILY_PROGRAMS]  = "programs",
};

void saved_deep_search_init(SavedDeepSearch* s) {
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->family = -1;
    s->kind = DEEP_KIND_ANY;
    s->one_filesystem = TRUE;
}

void saved_deep_search_clear(SavedDeepSearch* s) {
    if (s == NULL) return;
    g_free(s->folder);
    g_free(s->name);
    g_free(s->extensions);
    g_free(s->content);
    g_free(s->min_size);
    g_free(s->max_size);
    g_free(s->after);
    g_free(s->before);
    g_free(s->exclude);
    g_free(s->depth);
    saved_deep_search_init(s);
}

void saved_deep_search_free(SavedDeepSearch* s) {
    if (s == NULL) return;
    saved_deep_search_clear(s);
    g_free(s);
}

static gboolean search_id_valid(const gchar* id) {
    if (id == NULL || id[0] == '\0' || strlen(id) > SEARCH_ID_MAX) return FALSE;
    for (const gchar* p = id; *p != '\0'; p++) {
        if (!g_ascii_isxdigit(*p) || g_ascii_isupper(*p)) return FALSE;
    }
    return TRUE;
}

gchar* deep_search_target_make(const gchar* id) {
    return search_id_valid(id) ? g_strconcat(DEEP_SEARCH_TARGET_PREFIX, id, NULL) : NULL;
}

const gchar* deep_search_target_id(const gchar* target) {
    if (target == NULL || !g_str_has_prefix(target, DEEP_SEARCH_TARGET_PREFIX)) return NULL;
    const gchar* id = target + strlen(DEEP_SEARCH_TARGET_PREFIX);
    return search_id_valid(id) ? id : NULL;
}

gchar* deep_search_store_new_id(void) {
    return g_strdup_printf("%08x%08x", g_random_int(), g_random_int());
}

gchar* deep_search_store_path(const gchar* id) {
    if (!search_id_valid(id)) return NULL;
    gchar* dir = app_config_dir();
    gchar* name = g_strconcat(id, ".ini", NULL);
    gchar* path = g_build_filename(dir, "searches", name, NULL);
    g_free(name);
    g_free(dir);
    return path;
}

void deep_search_store_remove(const gchar* id) {
    gchar* path = deep_search_store_path(id);
    if (path != NULL) g_unlink(path);
    g_free(path);
}

static gchar* get_nonempty(GKeyFile* kf, const gchar* key) {
    gchar* v = g_key_file_get_string(kf, GROUP_NAME, key, NULL);
    if (v != NULL && v[0] == '\0') {
        g_free(v);
        return NULL;
    }
    return v;
}

static gboolean get_bool(GKeyFile* kf, const gchar* key, gboolean fallback) {
    GError* err = NULL;
    gboolean v = g_key_file_get_boolean(kf, GROUP_NAME, key, &err);
    if (err != NULL) {
        g_error_free(err);
        return fallback;
    }
    return v;
}

static gint parse_family(const gchar* text) {
    if (text == NULL || text[0] == '\0') return -1;
    for (gint i = 0; i < FILE_FAMILY_OTHER; i++) {
        if (g_strcmp0(text, FAMILY_KEYS[i]) == 0) return i;
    }
    return -1;
}

static DeepKind parse_kind(const gchar* text) {
    if (g_strcmp0(text, "files") == 0) return DEEP_KIND_FILES;
    if (g_strcmp0(text, "folders") == 0) return DEEP_KIND_FOLDERS;
    return DEEP_KIND_ANY;
}

SavedDeepSearch* deep_search_store_load(const gchar* path) {
    if (path == NULL) return NULL;
    GKeyFile* kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL) ||
        !g_key_file_has_group(kf, GROUP_NAME)) {
        g_key_file_free(kf);
        return NULL;
    }
    SavedDeepSearch* s = g_new0(SavedDeepSearch, 1);
    saved_deep_search_init(s);
    s->use_current_folder = get_bool(kf, "use_current_folder", FALSE);
    if (!s->use_current_folder) s->folder = get_nonempty(kf, "folder");
    s->name = get_nonempty(kf, "name");
    gchar* cat = get_nonempty(kf, "category");
    s->family = parse_family(cat);
    g_free(cat);
    if (s->family < 0) s->extensions = get_nonempty(kf, "extensions");
    s->content = get_nonempty(kf, "content");
    s->content_opts.match_case = get_bool(kf, "match_case", FALSE);
    s->content_opts.whole_word = get_bool(kf, "whole_word", FALSE);
    s->content_opts.regex = get_bool(kf, "regex", FALSE);
    s->content_opts.fuzzy = !s->content_opts.regex && get_bool(kf, "fuzzy", FALSE);
    gchar* kind = get_nonempty(kf, "type");
    s->kind = parse_kind(kind);
    g_free(kind);
    s->min_size = get_nonempty(kf, "min_size");
    s->max_size = get_nonempty(kf, "max_size");
    s->after = get_nonempty(kf, "after");
    s->before = get_nonempty(kf, "before");
    s->exclude = get_nonempty(kf, "exclude");
    s->depth = get_nonempty(kf, "depth");
    s->hidden = get_bool(kf, "hidden", FALSE);
    s->one_filesystem = get_bool(kf, "one_filesystem", TRUE);
    g_key_file_free(kf);
    return s;
}

static void set_nonempty(GKeyFile* kf, const gchar* key, const gchar* val) {
    if (val != NULL && val[0] != '\0') g_key_file_set_string(kf, GROUP_NAME, key, val);
}

gboolean deep_search_store_save(const SavedDeepSearch* s, const gchar* path, GError** error) {
    if (s == NULL || path == NULL) return FALSE;
    GKeyFile* kf = g_key_file_new();
    /* Always written so the [DeepSearch] group exists even when all criteria are empty */
    g_key_file_set_boolean(kf, GROUP_NAME, "use_current_folder", s->use_current_folder);
    if (!s->use_current_folder) set_nonempty(kf, "folder", s->folder);
    set_nonempty(kf, "name", s->name);
    if (s->family >= 0 && s->family < FILE_FAMILY_OTHER) {
        g_key_file_set_string(kf, GROUP_NAME, "category", FAMILY_KEYS[s->family]);
    } else {
        set_nonempty(kf, "extensions", s->extensions);
    }
    set_nonempty(kf, "content", s->content);
    if (s->content_opts.match_case) g_key_file_set_boolean(kf, GROUP_NAME, "match_case", TRUE);
    if (s->content_opts.whole_word) g_key_file_set_boolean(kf, GROUP_NAME, "whole_word", TRUE);
    if (s->content_opts.regex) {
        g_key_file_set_boolean(kf, GROUP_NAME, "regex", TRUE);
    } else if (s->content_opts.fuzzy) {
        g_key_file_set_boolean(kf, GROUP_NAME, "fuzzy", TRUE);
    }
    if (s->kind == DEEP_KIND_FILES) {
        g_key_file_set_string(kf, GROUP_NAME, "type", "files");
    } else if (s->kind == DEEP_KIND_FOLDERS) {
        g_key_file_set_string(kf, GROUP_NAME, "type", "folders");
    }
    set_nonempty(kf, "min_size", s->min_size);
    set_nonempty(kf, "max_size", s->max_size);
    set_nonempty(kf, "after", s->after);
    set_nonempty(kf, "before", s->before);
    set_nonempty(kf, "exclude", s->exclude);
    set_nonempty(kf, "depth", s->depth);
    if (s->hidden) g_key_file_set_boolean(kf, GROUP_NAME, "hidden", TRUE);
    if (!s->one_filesystem) g_key_file_set_boolean(kf, GROUP_NAME, "one_filesystem", FALSE);

    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    gboolean ok = g_key_file_save_to_file(kf, path, error);
    g_key_file_free(kf);
    return ok;
}

gchar* saved_deep_search_default_name(const SavedDeepSearch* s) {
    if (s != NULL) {
        if (s->name != NULL && s->name[0] != '\0') return g_strdup(s->name);
        if (s->content != NULL && s->content[0] != '\0') return g_strdup(s->content);
        if (s->extensions != NULL && s->extensions[0] != '\0') return g_strdup(s->extensions);
        if (s->family >= 0 && s->family < FILE_FAMILY_OTHER) {
            return g_strdup(file_family_name((FileFamily)s->family));
        }
        if (!s->use_current_folder && s->folder != NULL && s->folder[0] != '\0') {
            return g_path_get_basename(s->folder);
        }
    }
    return g_strdup(_("Deep Search"));
}
