#ifndef QUICK_OPEN_H
#define QUICK_OPEN_H

/* Quick open (Ctrl+P; Emacs C-x C-f): an AppDialog listing every file of
 * the project (its git repository, else the explorer's directory), read in
 * a worker thread with code_index_all_files(), filtered by fuzzy matching
 * as you type. Enter shows the file in the explorer (preview), Ctrl+Enter
 * opens it in an editor tab. */
#include "app_types.h"

void quick_open_show(AppData* app);

#endif /* QUICK_OPEN_H */
