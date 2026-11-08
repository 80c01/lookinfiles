#ifndef PREVIEW_FULL_H
#define PREVIEW_FULL_H

/* "Load Whole File" bar of the preview: shown above a text preview that
 * stops before the end of the file (large files show their first
 * PREVIEW_BYTES); its button previews the file again, read to the end
 * (after a confirmation above 64 MB).
 * The request holds for that file until another one is selected. */
#include "core/app_types.h"

/* Put the bar above content (the preview's text area); returns the box to
 * put in its place */
GtkWidget* preview_full_wrap(AppData* app, GtkWidget* content);
/* The preview of path shows shown of its total bytes: the bar is shown
 * while shown < total (shown < 0: hide it) */
void preview_full_update(AppData* app, const gchar* path, gint64 shown, gint64 total);
/* TRUE if the whole of path was asked for */
gboolean preview_full_wanted(AppData* app, const gchar* path);
/* Another entry is previewed: hide the bar, forget a request for another file */
void preview_full_reset(AppData* app, const gchar* path);

#endif /* PREVIEW_FULL_H */
