#ifndef PREVIEW_BUSY_H
#define PREVIEW_BUSY_H

/* "Rendering…" notice of the preview: when the preview of a newly selected
 * file is not ready after a short delay, the previous preview is dimmed and
 * a spinner with the file name is shown above it, until the new preview is
 * shown or its rendering is cancelled. */
#include "core/app_types.h"

/* Put the notice above content (the Preview page); returns the widget to
 * put in its place */
GtkWidget* preview_busy_wrap(GtkWidget* content);
/* The notice of wrap (from preview_busy_wrap()) is app's preview notice */
void preview_busy_attach(AppData* app, GtkWidget* wrap);
/* The preview of name is being rendered under cancellable: show the notice
 * if it is still rendering after the delay (cancelling it ends the notice) */
void preview_busy_start(AppData* app, const gchar* name, GCancellable* cancellable);
/* The preview is shown (or another entry is selected): hide the notice */
void preview_busy_stop(AppData* app);

#endif /* PREVIEW_BUSY_H */
