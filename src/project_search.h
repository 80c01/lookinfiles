#ifndef PROJECT_SEARCH_H
#define PROJECT_SEARCH_H

/* Search in files (Ctrl+Shift+F): asks for a text (the selection of the
 * current editor tab, else the last search, filled in) and the options of
 * text_search (match case, whole word, regex, letters in order) and the
 * files to include / exclude (path_globs) or the ignored ones, all kept for
 * the next search of the workspace (each workspace has its own last search
 * and Search tab), then searches every
 * file of the project (its git repository, else the explorer's directory)
 * in a worker thread with text_search_files(). The results go to the Search
 * tab of the preview notebook (code_refs.h); Enter opens one. Files are read
 * from disk: unsaved changes are not searched. */
#include "core/app_types.h"

void project_search_show(AppData* app);

/* Workspace switch (workspace.c): the Search tab of the workspace left is
 * put aside, that of the workspace shown comes back; a search still running
 * fills the tab of the workspace that started it */
void project_search_workspace_hidden(AppData* app, VirtualLayout* ws);
void project_search_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its search stops, its Search tab goes */
void project_search_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* PROJECT_SEARCH_H */
