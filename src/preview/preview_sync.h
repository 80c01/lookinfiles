#ifndef PREVIEW_SYNC_H
#define PREVIEW_SYNC_H

#include <gtk/gtk.h>

#include "core/app_types.h"
#include "markup/md_out.h"

/* The preview kept on the line being edited. A file shown again (saved,
 * changed on disk, re-rendered after a fold or m) keeps the line at the top
 * of the view instead of going back to the start; the preview of a file
 * open in an editor follows the editor's cursor, its line shown at the
 * height of the cursor in the editor (as VS Code's Markdown preview).
 * Rendered documents map their source lines (MdOut.sources: Markdown, Org);
 * text previews are shown line by line; other previews keep their offset.
 * Saving the file edited leaves the view where it is: the cursor is
 * followed when it moves. */

/* The preview starts building the file at path (its old content is still
 * on screen): remember where the view is when path is the file shown */
void preview_sync_loading(AppData* app, const gchar* path);

/* The preview of path goes into the buffer: the view starts at the top, then
 * goes back to the line kept or to the editor's cursor. md: the rendered
 * document, or NULL; keep FALSE: the preview opens at its own place (a log at
 * its end, an error) */
void preview_sync_showing(AppData* app, const gchar* path, const MdOut* md, gboolean keep);

/* The preview shows the file as text, line by line from buffer line first_line */
void preview_sync_text_lines(AppData* app, gint first_line);

void preview_sync_free(AppData* app);

#endif /* PREVIEW_SYNC_H */
