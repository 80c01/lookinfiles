#ifndef FOLDER_SUGGEST_H
#define FOLDER_SUGGEST_H

/* Folder suggestions under a text field that expects a folder (the folder
 * of the Deep Search tab, the location of a bookmark): a popover listing,
 * as you type, the subfolders of the path typed ("~/pro" -> ~/projects),
 * read in the background, then the recent folders that go on from it (Alt+R
 * list, nav_history.h); for any other text, the recent folders matching it
 * as in Ctrl+P (fuzzy_match.h). Ctrl+Space shows them for the text as it is.
 * In the field: Down / Up choose one, Tab completes with it (or the first)
 * and lists its subfolders, Enter takes the one chosen (none chosen: the
 * field's own Enter), Escape hides them; a click takes one. */
#include "core/app_types.h"

void folder_suggest_attach(GtkWidget* entry, AppData* app);

/* TRUE while the suggestions of entry are on screen (its key handlers then
 * leave Down, Up, Enter and Escape to them) */
gboolean folder_suggest_is_open(GtkWidget* entry);

#endif /* FOLDER_SUGGEST_H */
