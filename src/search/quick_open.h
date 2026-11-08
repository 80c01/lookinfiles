#ifndef QUICK_OPEN_H
#define QUICK_OPEN_H

/* Quick open (Ctrl+P; Emacs C-x C-f): an AppDialog listing every file of
 * the project (its git repository, else the explorer's directory), read in
 * a worker thread with code_index_all_files(), filtered by fuzzy matching
 * as you type; with nothing typed, the recently opened files come first.
 * Enter shows the file in the explorer (preview), Ctrl+Enter opens it in an
 * editor tab. */
#include "core/app_types.h"

void quick_open_show(AppData* app);

/* Alt+R: the recent folders, then the recent files (nav_history.h), most
 * recent first, in the same dialog. Enter on a folder makes it the
 * explorer's folder; on a file, shows it (Ctrl+Enter edits it). */
void quick_open_show_recent(AppData* app);

#endif /* QUICK_OPEN_H */
