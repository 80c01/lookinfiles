/* Folder history and recent places (see nav_history.h) */
#include "explorer/nav_history.h"
#include "code/jump_list.h"
#include <string.h>

#define FOLDER_HISTORY_MAX 100
#define SELECTION_MAX 200

/* VirtualLayout.dir_history */
typedef struct FolderHistory {
    JumpList* folders;
    GHashTable* selection;  /* folder -> name of the entry selected when left */
} FolderHistory;

static RecentList* recent;      /* loaded on first use */
static gboolean walking;        /* the next folder change is a back / forward */

static RecentList* recent_get(void) {
    if (recent == NULL) {
        gchar* path = recent_default_path();
        recent = recent_list_load(path);
        g_free(path);
    }
    return recent;
}

static void recent_save(void) {
    gchar* path = recent_default_path();
    recent_list_save(recent_get(), path, NULL);
    g_free(path);
}

static FolderHistory* history_of(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) return NULL;
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    if (ws->dir_history == NULL) {
        FolderHistory* h = g_new0(FolderHistory, 1);
        h->folders = jump_list_new(FOLDER_HISTORY_MAX);
        h->selection = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        ws->dir_history = h;
    }
    return ws->dir_history;
}

/* The entry of dir holding path (dir/a/b -> "a"), or NULL */
static gchar* entry_name(const gchar* dir, const gchar* path) {
    if (dir == NULL || path == NULL) return NULL;
    gsize n = strlen(dir);
    if (n == 0 || strncmp(path, dir, n) != 0) return NULL;
    const gchar* rest = path + n;
    if (dir[n - 1] != '/') {
        if (*rest != '/') return NULL;
        rest++;
    }
    const gchar* end = strchr(rest, '/');
    if (rest[0] == '\0' || end == rest) return NULL;
    return end != NULL ? g_strndup(rest, (gsize)(end - rest)) : g_strdup(rest);
}

void nav_history_folder_changed(AppData* app, const gchar* old_dir, const gchar* new_dir,
                                const gchar* selected) {
    if (app == NULL || new_dir == NULL || g_strcmp0(old_dir, new_dir) == 0) return;
    FolderHistory* h = history_of(app);
    if (h != NULL && old_dir != NULL) {
        if (!walking) jump_list_push(h->folders, old_dir, 0, 0);
        gchar* name = entry_name(old_dir, selected);
        if (name != NULL) {
            if (g_hash_table_size(h->selection) >= SELECTION_MAX) g_hash_table_remove_all(h->selection);
            g_hash_table_replace(h->selection, g_strdup(old_dir), name);
        }
    }
    walking = FALSE;
    recent_list_add(recent_get()->folders, new_dir);
    recent_save();
}

void nav_history_file_opened(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || !g_path_is_absolute(path)) return;
    recent_list_add(recent_get()->files, path);
    recent_save();
}

/* Index of the folder shown: the length when not walking the history */
static guint current_index(FolderHistory* h) {
    guint len = jump_list_length(h->folders);
    guint pos = jump_list_position(h->folders);
    return pos < len ? pos : len;
}

/* Step until an existing folder (deleted ones are skipped) */
static gchar* walk(AppData* app, gboolean back) {
    FolderHistory* h = history_of(app);
    if (h == NULL || app->base_dir == NULL) return NULL;
    for (;;) {
        const JumpLocation* loc = back ? jump_list_back(h->folders, app->base_dir, 0, 0)
                                       : jump_list_forward(h->folders);
        if (loc == NULL) return NULL;
        if (g_strcmp0(loc->path, app->base_dir) != 0 && g_file_test(loc->path, G_FILE_TEST_IS_DIR)) {
            walking = TRUE;
            return g_strdup(loc->path);
        }
    }
}

gchar* nav_history_back(AppData* app) {
    return walk(app, TRUE);
}

gchar* nav_history_forward(AppData* app) {
    return walk(app, FALSE);
}

gchar* nav_history_go(AppData* app, gboolean back, guint steps) {
    FolderHistory* h = history_of(app);
    if (h == NULL || app->base_dir == NULL || steps == 0) return NULL;
    guint cur = current_index(h);
    if (back ? steps > cur : cur + steps >= jump_list_length(h->folders)) return NULL;
    guint index = back ? cur - steps : cur + steps;
    const JumpLocation* target = jump_list_nth(h->folders, index);
    if (target == NULL || !g_file_test(target->path, G_FILE_TEST_IS_DIR)) return NULL;
    const JumpLocation* loc = jump_list_go_to(h->folders, index, app->base_dir, 0, 0);
    if (loc == NULL) return NULL;
    walking = g_strcmp0(loc->path, app->base_dir) != 0;
    return walking ? g_strdup(loc->path) : NULL;
}

gboolean nav_history_can_go(AppData* app, gboolean back) {
    FolderHistory* h = history_of(app);
    if (h == NULL) return FALSE;
    guint cur = current_index(h);
    return back ? cur > 0 : cur + 1 < jump_list_length(h->folders);
}

GPtrArray* nav_history_folders(AppData* app, gboolean back) {
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    FolderHistory* h = history_of(app);
    if (h == NULL) return out;
    guint cur = current_index(h);
    guint len = jump_list_length(h->folders);
    if (back) {
        for (guint i = cur; i-- > 0;) g_ptr_array_add(out, g_strdup(jump_list_nth(h->folders, i)->path));
    } else {
        for (guint i = cur + 1; i < len; i++) g_ptr_array_add(out, g_strdup(jump_list_nth(h->folders, i)->path));
    }
    return out;
}

const gchar* nav_history_selection(AppData* app, const gchar* dir) {
    FolderHistory* h = history_of(app);
    return (h != NULL && dir != NULL) ? g_hash_table_lookup(h->selection, dir) : NULL;
}

const RecentList* nav_history_recent(AppData* app) {
    (void)app;
    return recent_get();
}

void nav_history_workspace_removed(VirtualLayout* ws) {
    if (ws != NULL && ws->dir_history != NULL) {
        FolderHistory* h = ws->dir_history;
        jump_list_free(h->folders);
        g_hash_table_destroy(h->selection);
        g_free(h);
        ws->dir_history = NULL;
    }
}
