/* Folder history and recent places (see nav_history.h) */
#include "nav_history.h"
#include "jump_list.h"

#define FOLDER_HISTORY_MAX 100

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

static JumpList* history_of(AppData* app) {
    if (app->current_ws < 0 || app->current_ws >= app->num_workspaces) return NULL;
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    if (ws->dir_history == NULL) ws->dir_history = jump_list_new(FOLDER_HISTORY_MAX);
    return ws->dir_history;
}

void nav_history_folder_changed(AppData* app, const gchar* old_dir, const gchar* new_dir) {
    if (app == NULL || new_dir == NULL || g_strcmp0(old_dir, new_dir) == 0) return;
    JumpList* h = history_of(app);
    if (!walking && h != NULL && old_dir != NULL) jump_list_push(h, old_dir, 0, 0);
    walking = FALSE;
    recent_list_add(recent_get()->folders, new_dir);
    recent_save();
}

void nav_history_file_opened(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || !g_path_is_absolute(path)) return;
    recent_list_add(recent_get()->files, path);
    recent_save();
}

/* Step until an existing folder (deleted ones are skipped) */
static gchar* walk(AppData* app, gboolean back) {
    JumpList* h = app != NULL ? history_of(app) : NULL;
    if (h == NULL || app->base_dir == NULL) return NULL;
    for (;;) {
        const JumpLocation* loc = back ? jump_list_back(h, app->base_dir, 0, 0) : jump_list_forward(h);
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

const RecentList* nav_history_recent(AppData* app) {
    (void)app;
    return recent_get();
}

void nav_history_workspace_removed(VirtualLayout* ws) {
    if (ws != NULL && ws->dir_history != NULL) {
        jump_list_free(ws->dir_history);
        ws->dir_history = NULL;
    }
}
