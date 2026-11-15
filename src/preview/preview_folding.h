#ifndef PREVIEW_FOLDING_H
#define PREVIEW_FOLDING_H

#include <gtk/gtk.h>

#include "core/app_types.h"
#include "preview/preview_doc.h"

/* Folding of the structured previews that record fold points
 * (PreviewDoc.folds: JSON objects and arrays...). The fold state is kept per
 * file for the session; Tab folds or opens the block at the cursor (the
 * enclosing one from a line of values), Shift+Tab cycles the level: the
 * outer members only, two levels, everything open. Each change builds the
 * preview again with the new state (APP_EVENT_PREVIEW_STALE) and keeps the
 * cursor on the block, at the same height in the view. */

/* A copy of the fold state of a file for the preview worker (free with
 * preview_fold_free()), NULL if it was never changed */
PreviewFold* preview_folding_state(AppData* app, const gchar* path);

/* The preview now shows doc, the preview of the file at path (NULL:
 * something without fold points): its blocks are what Tab acts on; after a
 * fold change, the cursor goes back to the block it was on. path is given:
 * a cached preview is shown before preview_state_set_file() */
void preview_folding_shown(AppData* app, const PreviewDoc* doc, const gchar* path);

/* A fold change of the file at path is being built: the cursor will go back
 * to its block */
gboolean preview_folding_restoring(AppData* app, const gchar* path);

/* Tab / Shift+Tab in the preview; TRUE when handled (the preview shows fold
 * points) */
gboolean preview_folding_key(AppData* app, guint keyval, GdkModifierType state);

/* Click at (x, y) in the preview (widget coordinates): on the twisty of a
 * block (PREVIEW_FOLD_MARK_*), folds or opens it; TRUE when handled */
gboolean preview_folding_click(AppData* app, gdouble x, gdouble y);

void preview_folding_free(AppData* app);

#endif /* PREVIEW_FOLDING_H */
