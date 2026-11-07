#ifndef CANVAS_VIEW_H
#define CANVAS_VIEW_H

#include "app_types.h"

/* The Canvas page of the preview notebook: edits a Canvas note (canvas_doc.h),
 * text boxes and pictures placed freely on a page.
 *
 * - A floating tool bar (canvas_toolbar.h) picks the tool: select, shapes
 *   and pencil drawn with the mouse, text box, table, eraser; a panel
 *   (canvas_props.h) sets the shape style or formats the text.
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
 *   the align menu act on every selected block.
 * - Ctrl+Z / Ctrl+Shift+Z (Ctrl+Y): undo / redo on the page (canvas_history.h);
 *   while typing, the text box or table cell undoes its own text.
 * - Ctrl+wheel, Ctrl+= / Ctrl+- zoom, Ctrl+0 back to 100 %; a middle-button
 *   drag, or Space held and a drag, moves the view (canvas_viewport.h).
 * - Ctrl+B / Ctrl+I / Ctrl+U / Ctrl+Shift+S / Ctrl+E / Ctrl+Alt+1: bold,
 *   italic, underline, strikethrough, code, heading.
 *
 * Saved automatically (shortly after each change, when another note is
 * opened, when the page is closed, and at quit). One canvas at a time. */

/* Open the Canvas note in directory dir (title shown above the page) */
void canvas_view_open(AppData* app, const gchar* dir, const gchar* title);

/* Save the open canvas now, if it has changes */
void canvas_view_save(AppData* app);

/* Save and close the page if it shows the note at path, or a note under the
 * directory path (before they go to the trash or are deleted). Returns TRUE
 * if it was open. */
gboolean canvas_view_close_path(AppData* app, const gchar* path);

/* The note at old_path (or a directory holding it) was renamed or moved to
 * new_path: keep editing it there (title: its new title, or NULL) */
void canvas_view_path_moved(AppData* app, const gchar* old_path, const gchar* new_path, const gchar* title);

/* Close the Canvas page if it is the current page of the preview notebook
 * (Ctrl+W). Returns TRUE if it was closed. */
gboolean canvas_view_close_shown(AppData* app);

/* The Canvas page has the focus and takes this key (zoom: Ctrl+= / Ctrl+- /
 * Ctrl+0, the font sizes elsewhere): the window lets it through */
gboolean canvas_view_claims_key(AppData* app, guint keyval, GdkModifierType state);

#endif /* CANVAS_VIEW_H */
