#ifndef DIR_PREVIEW_H
#define DIR_PREVIEW_H

/* Directory preview in the right pane: header with totals and a table of the
 * entries (icon, name, size, date). Reads one directory level only, in a
 * worker thread started after a short delay, so moving quickly through the
 * explorer never waits on a directory. A button over its top right corner
 * opens the Disk Usage tab on the folder. */
#include "core/app_types.h"

/* Show the overview of directory full (display name name) in app->buffer.
 * Cancels the current preview task and replaces app->preview_cancellable. */
void dir_preview_show(AppData* app, const gchar* name, const gchar* full);

/* Put the Disk Usage button over content (the preview's text area, after
 * app->text_view is set): a framed button in the top right corner, shown
 * while a folder overview is the text shown; returns the overlay to put in
 * place of content */
GtkWidget* dir_preview_wrap(AppData* app, GtkWidget* content);

#endif /* DIR_PREVIEW_H */
