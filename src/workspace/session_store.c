/* Session and recent places between launches (see session_store.h) */
#include "workspace/session_store.h"
#include "core/app_paths.h"

#include <glib/gstdio.h>
#include <string.h>

static void workspace_free(gpointer data) {
    SessionWorkspace* ws = data;
    g_free(ws->dir);
    g_free(ws->remote_id);
    g_strfreev(ws->expanded);
    g_free(ws->selected);
    g_strfreev(ws->tabs);
    g_free(ws->preview_tabs);
    g_free(ws->editor_tabs);
    saved_layout_free(ws->panels);
    g_free(ws);
}

Session* session_new(void) {
    Session* s = g_new0(Session, 1);
    s->workspaces = g_ptr_array_new_with_free_func(workspace_free);
    return s;
}

void session_free(Session* session) {
    if (session == NULL) return;
    g_ptr_array_unref(session->workspaces);
    g_free(session);
}

static gchar** copy_strv(const gchar* const* v) {
    return v != NULL ? g_strdupv((gchar**)v) : g_new0(gchar*, 1);
}

SessionWorkspace* session_add_workspace(Session* session, const gchar* dir, const gchar* const* expanded,
                                        const gchar* selected, gboolean show_hidden, gint sort_mode,
                                        const gchar* const* tabs) {
    SessionWorkspace* ws = g_new0(SessionWorkspace, 1);
    ws->dir = g_strdup(dir);
    ws->expanded = copy_strv(expanded);
    ws->selected = (selected != NULL && selected[0] != '\0') ? g_strdup(selected) : NULL;
    ws->show_hidden = show_hidden;
    ws->sort_mode = sort_mode;
    ws->tabs = copy_strv(tabs);
    g_ptr_array_add(session->workspaces, ws);
    return ws;
}

gchar* session_default_path(void) {
    gchar* dir = app_config_dir();
    gchar* path = g_build_filename(dir, "session.ini", NULL);
    g_free(dir);
    return path;
}

static gchar** get_list(GKeyFile* kf, const gchar* group, const gchar* key) {
    gchar** v = g_key_file_get_string_list(kf, group, key, NULL, NULL);
    return v != NULL ? v : g_new0(gchar*, 1);
}

Session* session_load(const gchar* path) {
    GKeyFile* kf = g_key_file_new();
    if (path == NULL || !g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        return NULL;
    }
    Session* s = session_new();
    gint count = g_key_file_get_integer(kf, "Session", "workspaces", NULL);
    for (gint i = 0; i < count; i++) {
        gchar* group = g_strdup_printf("Workspace %d", i + 1);
        gchar* dir = g_key_file_get_string(kf, group, "dir", NULL);
        if (dir != NULL && dir[0] != '\0') {
            gchar** expanded = get_list(kf, group, "expanded");
            gchar** tabs = get_list(kf, group, "tabs");
            gchar* selected = g_key_file_get_string(kf, group, "selected", NULL);
            SessionWorkspace* ws = session_add_workspace(s, dir, (const gchar* const*)expanded, selected,
                                                         g_key_file_get_boolean(kf, group, "hidden", NULL),
                                                         g_key_file_get_integer(kf, group, "sort", NULL),
                                                         (const gchar* const*)tabs);
            ws->view = g_key_file_get_integer(kf, group, "view", NULL);
            ws->remote_id = g_key_file_get_string(kf, group, "remote", NULL);
            ws->preview_tabs = g_key_file_get_string(kf, group, "preview_tabs", NULL);
            ws->editor_tabs = g_key_file_get_string(kf, group, "editor_tabs", NULL);
            if (g_key_file_has_key(kf, group, "Explorer", NULL)) {
                ws->panels = saved_layout_read(kf, group, "Session");
            }
            g_strfreev(expanded);
            g_strfreev(tabs);
            g_free(selected);
        }
        g_free(dir);
        g_free(group);
    }
    s->current = g_key_file_get_integer(kf, "Session", "current", NULL) - 1;
    g_key_file_free(kf);
    if (s->workspaces->len == 0) {
        session_free(s);
        return NULL;
    }
    s->current = CLAMP(s->current, 0, (gint)s->workspaces->len - 1);
    return s;
}

static gboolean save_keyfile(GKeyFile* kf, const gchar* path, GError** error) {
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    return g_key_file_save_to_file(kf, path, error);
}

gboolean session_save(const Session* session, const gchar* path, GError** error) {
    GKeyFile* kf = g_key_file_new();
    g_key_file_set_integer(kf, "Session", "workspaces", (gint)session->workspaces->len);
    g_key_file_set_integer(kf, "Session", "current", session->current + 1);
    for (guint i = 0; i < session->workspaces->len; i++) {
        SessionWorkspace* ws = g_ptr_array_index(session->workspaces, i);
        gchar* group = g_strdup_printf("Workspace %u", i + 1);
        g_key_file_set_string(kf, group, "dir", ws->dir);
        g_key_file_set_string_list(kf, group, "expanded", (const gchar* const*)ws->expanded,
                                   g_strv_length(ws->expanded));
        if (ws->selected != NULL) g_key_file_set_string(kf, group, "selected", ws->selected);
        g_key_file_set_boolean(kf, group, "hidden", ws->show_hidden);
        g_key_file_set_integer(kf, group, "sort", ws->sort_mode);
        if (ws->view != 0) g_key_file_set_integer(kf, group, "view", ws->view);
        g_key_file_set_string_list(kf, group, "tabs", (const gchar* const*)ws->tabs, g_strv_length(ws->tabs));
        if (ws->remote_id != NULL) g_key_file_set_string(kf, group, "remote", ws->remote_id);
        if (ws->preview_tabs != NULL) g_key_file_set_string(kf, group, "preview_tabs", ws->preview_tabs);
        if (ws->editor_tabs != NULL) g_key_file_set_string(kf, group, "editor_tabs", ws->editor_tabs);
        if (ws->panels != NULL) saved_layout_write(kf, group, ws->panels, FALSE);
        g_free(group);
    }
    gboolean ok = save_keyfile(kf, path, error);
    g_key_file_free(kf);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Saved workspaces                                                          */
/* ------------------------------------------------------------------------- */

#define SNAPSHOT_ID_MAX 32

static gboolean snapshot_id_valid(const gchar* id) {
    if (id == NULL || id[0] == '\0' || strlen(id) > SNAPSHOT_ID_MAX) return FALSE;
    for (const gchar* p = id; *p != '\0'; p++) {
        if (!g_ascii_isxdigit(*p) || g_ascii_isupper(*p)) return FALSE;
    }
    return TRUE;
}

gchar* session_target_make(const gchar* id) {
    return snapshot_id_valid(id) ? g_strconcat(SESSION_TARGET_PREFIX, id, NULL) : NULL;
}

const gchar* session_target_id(const gchar* target) {
    if (target == NULL || !g_str_has_prefix(target, SESSION_TARGET_PREFIX)) return NULL;
    const gchar* id = target + strlen(SESSION_TARGET_PREFIX);
    return snapshot_id_valid(id) ? id : NULL;
}

gchar* session_snapshot_new_id(void) {
    return g_strdup_printf("%08x%08x", g_random_int(), g_random_int());
}

gchar* session_snapshot_path(const gchar* id) {
    if (!snapshot_id_valid(id)) return NULL;
    gchar* dir = app_config_dir();
    gchar* name = g_strconcat(id, ".ini", NULL);
    gchar* path = g_build_filename(dir, "sessions", name, NULL);
    g_free(name);
    g_free(dir);
    return path;
}

void session_snapshot_remove(const gchar* id) {
    gchar* path = session_snapshot_path(id);
    if (path != NULL) g_unlink(path);
    g_free(path);
}

/* ------------------------------------------------------------------------- */
/* Recent folders and files                                                  */
/* ------------------------------------------------------------------------- */

RecentList* recent_list_new(void) {
    RecentList* r = g_new0(RecentList, 1);
    r->folders = g_ptr_array_new_with_free_func(g_free);
    r->files = g_ptr_array_new_with_free_func(g_free);
    return r;
}

void recent_list_free(RecentList* recent) {
    if (recent == NULL) return;
    g_ptr_array_unref(recent->folders);
    g_ptr_array_unref(recent->files);
    g_free(recent);
}

void recent_list_add(GPtrArray* list, const gchar* path) {
    if (list == NULL || path == NULL || path[0] == '\0') return;
    for (guint i = 0; i < list->len; i++) {
        if (strcmp(g_ptr_array_index(list, i), path) == 0) {
            g_ptr_array_remove_index(list, i);
            break;
        }
    }
    g_ptr_array_insert(list, 0, g_strdup(path));
    if (list->len > RECENT_MAX) g_ptr_array_set_size(list, RECENT_MAX);
}

gchar* recent_default_path(void) {
    gchar* dir = app_config_dir();
    gchar* path = g_build_filename(dir, "recent.ini", NULL);
    g_free(dir);
    return path;
}

static void load_into(GKeyFile* kf, const gchar* key, GPtrArray* list) {
    gchar** v = get_list(kf, "Recent", key);
    for (gchar** p = v; *p != NULL && list->len < RECENT_MAX; p++) {
        if ((*p)[0] != '\0') g_ptr_array_add(list, g_strdup(*p));
    }
    g_strfreev(v);
}

RecentList* recent_list_load(const gchar* path) {
    RecentList* r = recent_list_new();
    GKeyFile* kf = g_key_file_new();
    if (path != NULL && g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        load_into(kf, "folders", r->folders);
        load_into(kf, "files", r->files);
    }
    g_key_file_free(kf);
    return r;
}

gboolean recent_list_save(const RecentList* recent, const gchar* path, GError** error) {
    GKeyFile* kf = g_key_file_new();
    g_key_file_set_string_list(kf, "Recent", "folders", (const gchar* const*)recent->folders->pdata,
                               recent->folders->len);
    g_key_file_set_string_list(kf, "Recent", "files", (const gchar* const*)recent->files->pdata,
                               recent->files->len);
    gboolean ok = save_keyfile(kf, path, error);
    g_key_file_free(kf);
    return ok;
}
