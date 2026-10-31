#ifndef SESSION_STORE_H
#define SESSION_STORE_H

/* What Lookin remembers between launches, in app_config_dir():
 *  - session.ini: the workspaces at quit (folder, expanded folders, selected
 *    entry, hidden files, sort, open files) and the current one;
 *  - recent.ini: recent folders and files, most recent first.
 * Pure data and file I/O (GKeyFile), no widgets: unit-tested in tests/. */
#include <glib.h>

#define RECENT_MAX 30

typedef struct {
    gchar* dir;          /* the workspace's folder */
    gchar** expanded;    /* expanded folders (NULL-terminated, may be empty) */
    gchar* selected;     /* selected entry, or NULL */
    gboolean show_hidden;
    gint sort_mode;
    gchar** tabs;        /* files open in editor tabs, in order */
} SessionWorkspace;

typedef struct {
    GPtrArray* workspaces; /* SessionWorkspace* */
    gint current;          /* index of the workspace shown */
} Session;

Session* session_new(void);
void session_free(Session* session);
/* Add a workspace (strings copied; expanded and tabs may be NULL) */
SessionWorkspace* session_add_workspace(Session* session, const gchar* dir, const gchar* const* expanded,
                                        const gchar* selected, gboolean show_hidden, gint sort_mode,
                                        const gchar* const* tabs);

/* app_config_dir()/session.ini (caller frees) */
gchar* session_default_path(void);
/* NULL when the file is missing or has no workspace */
Session* session_load(const gchar* path);
gboolean session_save(const Session* session, const gchar* path, GError** error);

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
