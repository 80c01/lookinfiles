#ifndef CANVAS_PREVIEW_H
#define CANVAS_PREVIEW_H

#include "app_types.h"

/* The preview of a Canvas note: its page drawn by the same block widgets
 * as the Canvas tab (canvas_item.h), read-only, at 100 % from the top-left
 * corner of the page, as the tab opens. Ctrl+wheel zooms, a middle-button
 * drag moves the view (canvas_viewport.h). Shown as the "canvas" page of
 * the preview stack. */

/* Show the note in directory dir; FALSE (nothing changed) when its page
 * cannot be read or has no block */
gboolean canvas_preview_show(AppData* app, const gchar* dir);

#endif /* CANVAS_PREVIEW_H */
