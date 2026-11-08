#ifndef EDITOR_SESSION_H
#define EDITOR_SESSION_H

/* The editor tabs of a workspace saved with the session and reopened at
 * the same place: notebooks, splits and their sizes, current pages, view
 * last focused (text format: tab_tree.h). */
#include "core/app_types.h"

/* Describe workspace ws's editor tabs: appends their files to files
 * (gchar*, each once) and sets *preview_tabs (the tabs of the preview
 * notebook) and *editor_tabs (the dedicated editor panel) to their tree
 * text, NULL when there are none (caller frees). */
void editor_session_capture(AppData* app, gint ws, GPtrArray* files, gchar** preview_tabs,
                            gchar** editor_tabs);
/* Reopen files in the current workspace where editor_session_capture() saw
 * them; files missing or not editable are skipped. */
void editor_session_restore(AppData* app, const gchar* const* files, const gchar* preview_tabs,
                            const gchar* editor_tabs);

#endif /* EDITOR_SESSION_H */
