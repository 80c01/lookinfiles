#ifndef ORG_FOLD_H
#define ORG_FOLD_H

#include "core/app_types.h"

/* Folding of the Org file in the preview: headings folded by hand and the
 * global state (Shift+Tab). Kept across previews, freed at exit. */

/* Fold or unfold the heading on file_line; TRUE when the preview re-renders */
gboolean orgmode_toggle_heading_fold(AppData* app, gint file_line);
/* Cycle global folding state (Show All -> Overview -> Contents -> Show All) */
void orgmode_cycle_global_fold(AppData* app);
/* Lines folded by hand (set of GINT_TO_POINTER(file_line)), NULL if none;
 * global gets 0 all expanded, 1 overview, 2 contents */
GHashTable* org_fold_state(AppData* app, gint* global);
/* A copy of the lines folded by hand (free with g_hash_table_unref()), NULL
 * if none, for a worker thread; global as org_fold_state() */
GHashTable* org_fold_state_copy(AppData* app, gint* global);
void org_fold_free(AppData* app);

#endif /* ORG_FOLD_H */
