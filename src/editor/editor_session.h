#ifndef EDITOR_SESSION_H
#define EDITOR_SESSION_H

/* The editor tabs of a workspace saved with the session and reopened at
 * the same place: notebooks, splits and their sizes, current pages, view
 * last focused (text format: tab_tree.h). Browser tabs are kept too, by
 * their address (an entry of files with a scheme), reopened without WebKit
 * history. Other pages that name their file (editor_page_set_file():
 * Jupyter notebooks, videos) are kept by that file. */
#include "core/app_types.h"

/* Describe workspace ws's editor tabs: appends their files and browser tab
 * addresses to files (gchar*, each once) and sets *preview_tabs (the tabs of the preview
 * notebook) and *editor_tabs (the dedicated editor panel) to their tree
 * text, NULL when there are none (caller frees). */
void editor_session_capture(AppData* app, gint ws, GPtrArray* files, gchar** preview_tabs,
                            gchar** editor_tabs);
/* Reopen files in the current workspace where editor_session_capture() saw
 * them; files missing or not editable are skipped, browser tabs too when
 * built without WebKit. */
void editor_session_restore(AppData* app, const gchar* const* files, const gchar* preview_tabs,
                            const gchar* editor_tabs);

/* Opens path as a page of its own in nb (a video tab, media/video_tab.h)
 * when it takes that file, else returns NULL: the restore tries it before
 * an editor tab */
typedef GtkWidget* (*EditorSessionPageHook)(AppData* app, const gchar* path, GtkNotebook* nb);
void editor_session_set_page_hook(EditorSessionPageHook hook);

#endif /* EDITOR_SESSION_H */
