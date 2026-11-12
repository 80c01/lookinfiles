#ifndef IPYNB_TAB_H
#define IPYNB_TAB_H

#include "core/app_types.h"

/* Jupyter notebook tabs: a notebook (.ipynb, nbformat 4; ipynb_doc.h) edited
 * cell by cell in a tab of its own, opened in place of an editor tab
 * (Enter or e on the file; older or broken notebooks open as text). One tab
 * per file; it moves like an editor tab (dragged to another pane, a split,
 * the preview notebook; Ctrl+Alt+Arrow) and belongs to the workspace it was
 * opened in.
 *
 * Each cell shows its type (Code, Markdown, Raw: a menu changes it), its
 * source in the code font (highlighted in the kernel language, or
 * Markdown), and for code cells the outputs kept in the file (text, errors,
 * images). Cells are not run: there is no kernel.
 *
 * - Ctrl+Up / Ctrl+Down: previous / next cell; Alt+Up / Alt+Down: move the
 *   cell; Alt+Enter: new code cell below; Alt+Delete: delete the cell (the
 *   bar's Undo button puts it back).
 * - The bar adds code and Markdown cells, clears every output, and opens
 *   the file as JSON text in an editor tab.
 *
 * Saved automatically (shortly after each change, Ctrl+S at once, when the
 * tab is closed, when its workspace is removed, and at quit), written as
 * Jupyter writes it. */

/* EditorOpenHook (editor.h): open full in a notebook tab when it is a
 * notebook the tab can edit, in the editor panel when dedicated (a tab
 * already open is shown; one of another workspace moves to this one).
 * FALSE when it is not. */
gboolean ipynb_tab_open(AppData* app, const gchar* name, const gchar* full, gboolean dedicated);

/* Ctrl+S in a notebook tab with the focus: save it now. Returns TRUE if one
 * had the focus. */
gboolean ipynb_tab_save_focused(AppData* app);

/* Save every notebook tab with changes now (all workspaces) */
void ipynb_tab_save_all(AppData* app);

/* Close the notebook tab with the focus, else the one shown as the current
 * page of the preview notebook (Ctrl+W), saved first. Returns TRUE if one
 * was closed. */
gboolean ipynb_tab_close_shown(AppData* app);

/* Notebook tabs of the preview notebook leave it with their workspace
 * (hidden), come back with it (shown), or close with it (removed: saved).
 * Those of the editor panel go with the panel (editor_detach_tabs()). */
void ipynb_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void ipynb_tab_workspace_shown(AppData* app, VirtualLayout* ws);
void ipynb_tab_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* IPYNB_TAB_H */
