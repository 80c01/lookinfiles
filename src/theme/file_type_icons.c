/* VS Code file-type icons (see file_type_icons.h) */
#include "theme/file_type_icons.h"

#include <string.h>

typedef struct {
    const gchar* key;
    FileTypeIcon icon;
} FileTypeIconEntry;

#include "theme/file_type_icons_table.h"

const gchar* const FILE_TYPE_ICON_COLORS[] = {
    "blue", "grey", "grey_light", "green", "orange", "pink",
    "purple", "red", "white", "yellow", "ignore", NULL
};

/* key -> const FileTypeIcon*, built once */
static GHashTable* build_index(const FileTypeIconEntry* entries, gsize n) {
    GHashTable* index = g_hash_table_new(g_str_hash, g_str_equal);
    for (gsize i = 0; i < n; i++) {
        g_hash_table_insert(index, (gpointer)entries[i].key, (gpointer)&entries[i].icon);
    }
    return index;
}

FileTypeIcon file_type_icon(const gchar* name, gboolean is_dir) {
    static GHashTable* names = NULL;
    static GHashTable* exts = NULL;
    static const FileTypeIcon folder = {"folder", "grey_light"};
    static const FileTypeIcon fallback = FILE_TYPE_ICON_DEFAULT;

    if (is_dir) return folder;
    if (name == NULL || name[0] == '\0') return fallback;
    if (g_once_init_enter(&names)) {
        exts = build_index(FILE_TYPE_ICON_EXTS, G_N_ELEMENTS(FILE_TYPE_ICON_EXTS));
        g_once_init_leave(&names, build_index(FILE_TYPE_ICON_NAMES, G_N_ELEMENTS(FILE_TYPE_ICON_NAMES)));
    }

    gchar* lower = g_ascii_strdown(name, -1);
    const FileTypeIcon* found = g_hash_table_lookup(names, lower);
    /* "a.spec.ts" tries "spec.ts", then "ts"; ".gitignore" tries "gitignore" */
    for (const gchar* p = lower; found == NULL && (p = strchr(p, '.')) != NULL;) {
        p++;
        found = g_hash_table_lookup(exts, p);
    }
    g_free(lower);
    return found != NULL ? *found : fallback;
}
