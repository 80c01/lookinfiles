#ifndef SESSION_STORE_H
#define SESSION_STORE_H

/* What LookinFiles remembers between launches, in app_config_dir():
 *  - session.ini: the workspaces at quit (folder, expanded folders, selected
 *    entry, hidden files, sort, explorer view, open files and notes and their
 *    place, panels) and the
 *    current one;
 *  - recent.ini: recent folders and files, most recent first.
 * Pure data and file I/O (GKeyFile), no widgets: unit-tested in tests/. */
#include <glib.h>
#include "layout_store.h"

#define RECENT_MAX 30

typedef struct {
    gchar* dir;          /* the workspace's folder */
    gchar** expanded;    /* expanded folders (NULL-terminated, may be empty) */
    gchar* selected;     /* selected entry, or NULL */
    gboolean show_hidden;
    gint sort_mode;
    gint view;           /* explorer view (SIDEBAR_VIEW_* of app_types.h), 0: the files */
    gchar** tabs;        /* files open in editor tabs, in order, each once */
    /* Where the tabs are (tab_tree.h, indices into tabs), NULL when not
     * saved (older session files: every tab in one notebook) */
    gchar* remote_id;    /* remote machine the folder is mounted from, or NULL */
    gchar* preview_tabs; /* the editor tabs of the preview notebook: one notebook */
    gchar* editor_tabs;  /* the dedicated editor panel: notebooks and splits */
    SavedLayout* panels; /* panels, split sizes and terminal panes, or NULL */
} SessionWorkspace;

typedef struct {
    GPtrArray* workspaces; /* SessionWorkspace* */
    gint current;          /* index of the workspace shown */
} Session;

Session* session_new(void);
void session_free(Session* session);
/* Add a workspace (strings copied; expanded and tabs may be NULL; the
 * caller then sets preview_tabs, editor_tabs and panels, owned by it) */
SessionWorkspace* session_add_workspace(Session* session, const gchar* dir, const gchar* const* expanded,
                                        const gchar* selected, gboolean show_hidden, gint sort_mode,
                                        const gchar* const* tabs);

/* app_config_dir()/session.ini (caller frees) */
gchar* session_default_path(void);
/* NULL when the file is missing or has no workspace */
Session* session_load(const gchar* path);
gboolean session_save(const Session* session, const gchar* path, GError** error);

/* Saved workspaces (workspace bookmarks): one workspace per file
 * app_config_dir()/sessions/<id>.ini, in the session.ini format. A bookmark
 * points to it with the target "lookinfiles-session://<id>"; an id is
 * lowercase hex only, so no target can name a file outside that folder. */
#define SESSION_TARGET_PREFIX "lookinfiles-session://"

/* The bookmark target of a saved workspace (caller frees) */
gchar* session_target_make(const gchar* id);
/* The id inside a target, or NULL when target is not a valid saved
 * workspace target. Points into target. */
const gchar* session_target_id(const gchar* target);
/* A new unique id (caller frees) */
gchar* session_snapshot_new_id(void);
/* The file of a saved workspace, NULL for an invalid id (caller frees) */
gchar* session_snapshot_path(const gchar* id);
/* Delete the file of a saved workspace; missing is fine */
void session_snapshot_remove(const gchar* id);

/* Recent folders and files */
typedef struct {
    GPtrArray* folders; /* gchar*, most recent first */
    GPtrArray* files;
} RecentList;

RecentList* recent_list_new(void);
void recent_list_free(RecentList* recent);
/* Put path first in list (removed from where it was), at most RECENT_MAX */
void recent_list_add(GPtrArray* list, const gchar* path);
/* app_config_dir()/recent.ini (caller frees) */
gchar* recent_default_path(void);
/* Empty lists when the file is missing */
RecentList* recent_list_load(const gchar* path);
gboolean recent_list_save(const RecentList* recent, const gchar* path, GError** error);

#endif /* SESSION_STORE_H */
