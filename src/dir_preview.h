#ifndef DIR_PREVIEW_H
#define DIR_PREVIEW_H

/* Directory preview in the right pane: header with totals and a table of the
 * entries (icon, name, size, date). Reads one directory level only, in a
 * worker thread started after a short delay, so moving quickly through the
 * explorer never waits on a directory. */
#include "app_types.h"

/* Show the overview of directory full (display name name) in app->buffer.
 * Cancels the current preview task and replaces app->preview_cancellable. */
void dir_preview_show(AppData* app, const gchar* name, const gchar* full);

#endif /* DIR_PREVIEW_H */
