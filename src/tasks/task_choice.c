/* The task chosen last in each project (see task_choice.h) */
#include "tasks/task_choice.h"
#include "core/app_paths.h"
#include <glib/gstdio.h>
#include <string.h>

gchar* task_choice_path(void) {
    return g_build_filename(app_config_dir(), "tasks.ini", NULL);
}

GHashTable* task_choice_load(const gchar* path) {
    GHashTable* chosen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)task_def_free);
    GKeyFile* kf = g_key_file_new();
    if (path == NULL || !g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        return chosen;
    }
    gchar** groups = g_key_file_get_groups(kf, NULL);
    for (gchar** g = groups; *g != NULL; g++) {
        gchar* label = g_key_file_get_string(kf, *g, "label", NULL);
        gchar* dir = g_key_file_get_string(kf, *g, "dir", NULL);
        gchar** argv = g_key_file_get_string_list(kf, *g, "argv", NULL, NULL);
        gchar** env = g_key_file_get_string_list(kf, *g, "env", NULL, NULL);
        if (label != NULL && dir != NULL && g_path_is_absolute(dir) && argv != NULL && argv[0] != NULL) {
            TaskDef* def = task_def_new(label, (const gchar* const*)argv, dir);
            if (env != NULL && env[0] != NULL) def->env = g_steal_pointer(&env);
            g_hash_table_replace(chosen, g_strdup(*g), def);
        }
        g_strfreev(env);
        g_strfreev(argv);
        g_free(dir);
        g_free(label);
    }
    g_strfreev(groups);
    g_key_file_free(kf);
    return chosen;
}

/* A folder a group can be named after */
static gboolean group_name_ok(const gchar* name) {
    if (name == NULL || name[0] == '\0') return FALSE;
    for (const gchar* c = name; *c != '\0'; c++) {
        if (*c == '[' || *c == ']' || (guchar)*c < 0x20) return FALSE;
    }
    return TRUE;
}

gboolean task_choice_save(const gchar* path, GHashTable* chosen, GError** error) {
    GKeyFile* kf = g_key_file_new();
    GHashTableIter it;
    gpointer key = NULL, value = NULL;
    g_hash_table_iter_init(&it, chosen);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        const TaskDef* def = value;
        if (!group_name_ok(key) || def == NULL || def->argv == NULL) continue;
        g_key_file_set_string(kf, key, "label", def->label);
        g_key_file_set_string(kf, key, "dir", def->dir);
        g_key_file_set_string_list(kf, key, "argv", (const gchar* const*)def->argv, g_strv_length(def->argv));
        if (def->env != NULL && def->env[0] != NULL) {
            g_key_file_set_string_list(kf, key, "env", (const gchar* const*)def->env, g_strv_length(def->env));
        }
    }
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    gboolean ok = g_key_file_save_to_file(kf, path, error);
    g_key_file_free(kf);
    return ok;
}
