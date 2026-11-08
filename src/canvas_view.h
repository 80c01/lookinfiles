#ifndef CANVAS_VIEW_H
#define CANVAS_VIEW_H

#include "app_types.h"

/* Canvas tabs: each edits a Canvas note (canvas_doc.h), text boxes and
 * pictures placed freely on a page. A canvas opens as a tab of the editor
 * panel, one tab per note, several open together; the tab moves like an
 * editor tab (dragged to another pane, a split, the preview notebook) and
 * belongs to the workspace it was opened in.
 *
 * - A floating tool bar (canvas_toolbar.h) picks the tool: select, shapes
 *   and pencil drawn with the mouse, text box, table, eraser; a panel
 *   (canvas_props.h) sets the shape style, a bar above the text box
 *   (canvas_format_bar.h) formats the text.
 * - Double click on a shape (or Enter on it): type its label, centred.
 * - Double click on the page (or Ctrl+Enter): new text box; Ctrl+V with a
 *   picture in the clipboard, or a dropped image file: new picture.
 * - Drag the bar above a block (or a picture itself) to move it, its corner
 *   to resize it; Esc selects the block being edited, then Delete removes it,
 *   the arrows move it, Enter edits it, Tab goes to the next block.
 * - An arrow or a line drawn from or to a block is bound to it
 *   (canvas_bind.h): its end stays on the block's outline when it moves.
 * - A drag on the empty page selects the blocks inside its box; Shift+click
 *   adds a block or takes it out. Moving, Delete, the arrows, the style and
 *   the align buttons of the panel act on every selected block.
 * - Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D: copy, cut, paste, duplicate the
 *   selected blocks (canvas_clip.h).
 * - Ctrl+Z / Ctrl+Shift+Z (Ctrl+Y): undo / redo on the page (canvas_history.h);
 *   while typing, the text box or table cell undoes its own text.
 * - Ctrl+wheel, Ctrl+= / Ctrl+- zoom, Ctrl+0 back to 100 %; a middle-button
 *   drag, or Space held and a drag, moves the view (canvas_viewport.h).
 * - Ctrl+B / Ctrl+I / Ctrl+U / Ctrl+Shift+S / Ctrl+E / Ctrl+Alt+1: bold,
 *   italic, underline, strikethrough, code, heading.
 *
 * Saved automatically (shortly after each change, when the tab is closed,
 * when its workspace is removed, and at quit). */

/* Show the tab of the Canvas note in directory dir, opened in the editor
 * panel when it is not open (title: tab and status bar); a tab of another
 * workspace moves to this one. Returns the tab's page. */
GtkWidget* canvas_view_open(AppData* app, const gchar* dir, const gchar* title);

/* The note directory of page when it is a Canvas tab, else NULL */
const gchar* canvas_view_page_dir(GtkWidget* page);

/* The style of the Canvas page and its blocks (canvas_preview.c draws
 * them too), added once */
void canvas_view_ensure_css(void);

/* Save every open canvas now (all workspaces), if it has changes */
void canvas_view_save(AppData* app);

/* Save and close the tabs of the note at path, or of the notes under the
 * directory path (before they go to the trash or are deleted), in every
 * workspace. Returns TRUE if one was open. */
gboolean canvas_view_close_path(AppData* app, const gchar* path);

/* The note at old_path (or a directory holding it) was renamed or moved to
 * new_path: keep editing it there (title: its new title, or NULL) */
void canvas_view_path_moved(AppData* app, const gchar* old_path, const gchar* new_path, const gchar* title);

/* Close the Canvas tab with the focus, else the one shown as the current
 * page of the preview notebook (Ctrl+W). Returns TRUE if one was closed. */
gboolean canvas_view_close_shown(AppData* app);

/* A Canvas tab has the focus and takes this key (zoom: Ctrl+= / Ctrl+- /
 * Ctrl+0, the font sizes elsewhere): the window lets it through */
gboolean canvas_view_claims_key(AppData* app, guint keyval, GdkModifierType state);

/* Canvas tabs of the preview notebook leave it with their workspace
 * (hidden), come back with it (shown), or close with it (removed: call it
 * before the editor panel goes, every canvas is saved). Those of the editor
 * panel go with the panel (editor_detach_tabs()). */
void canvas_view_workspace_hidden(AppData* app, VirtualLayout* ws);
void canvas_view_workspace_shown(AppData* app, VirtualLayout* ws);
void canvas_view_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* CANVAS_VIEW_H */
