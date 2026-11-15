#ifndef PREVIEW_LOCATION_H
#define PREVIEW_LOCATION_H

#include <gtk/gtk.h>

#include "core/app_types.h"
#include "preview/preview_doc.h"

/* Path of the value at the cursor of a data preview (PreviewDoc.paths:
 * "$.servers[2].host" in JSON), followed as the cursor moves: the preview
 * bar shows it (APP_EVENT_PREVIEW_LOCATION when it changes), y and Copy
 * Path in the right-click menu copy it. */

/* The preview now shows doc, the preview of the file at path (NULL:
 * something without paths); path is given: a cached preview is shown
 * before preview_state_set_file() */
void preview_location_shown(AppData* app, const PreviewDoc* doc, const gchar* path);

/* Path at the cursor of the file previewed now, or NULL */
const gchar* preview_location_current(AppData* app);

/* Copy the path at the cursor to the clipboard (with a notice); FALSE if
 * there is none */
gboolean preview_location_copy(AppData* app);

/* y in the preview; TRUE when handled (a path was copied) */
gboolean preview_location_key(AppData* app, guint keyval, GdkModifierType state);

void preview_location_free(AppData* app);

#endif /* PREVIEW_LOCATION_H */
