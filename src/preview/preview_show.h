#ifndef PREVIEW_SHOW_H
#define PREVIEW_SHOW_H

/* Preview a file or a folder that is not the explorer's selection (a Deep
 * Search result), as a selected row is previewed: the explorer keeps its
 * selection, which comes back in the preview when the explorer gets the
 * focus again. The page in front of the preview notebook does not change. */
#include "core/app_types.h"

/* Preview path; a text file opens on its line line (0-based, -1: at the
 * top) */
void preview_show_path(AppData* app, const gchar* path, gint line);

/* APP_EVENT_PREVIEW_PATH: preview app->preview_request_path (taken) at
 * app->preview_request_line, the Preview tab in front of its notebook */
void preview_show_requested(AppData* app);

#endif /* PREVIEW_SHOW_H */
