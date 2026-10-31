#ifndef CANVAS_VIEW_H
#define CANVAS_VIEW_H

#include "app_types.h"

/* The Canvas page of the preview notebook: edits a Canvas note (canvas_doc.h),
 * text boxes and pictures placed freely on a page.
 *
 * - Double click on the page (or Ctrl+Enter): new text box; Ctrl+V with a
 *   picture in the clipboard, or a dropped image file: new picture.
 * - Drag the bar above a block (or a picture itself) to move it, its corner
 *   to resize it; Esc selects the block being edited, then Delete removes it,
 *   the arrows move it, Enter edits it, Tab goes to the next block.
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

#endif /* CANVAS_VIEW_H */
