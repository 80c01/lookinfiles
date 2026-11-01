#ifndef PROJECT_SEARCH_H
#define PROJECT_SEARCH_H

/* Search in files (Ctrl+Shift+F): asks for a text (the selection of the
 * current editor tab, else the last search, filled in) and the options of
 * text_search (match case, whole word, regex, letters in order; kept for
 * the next search), then searches every
 * file of the project (its git repository, else the explorer's directory)
 * in a worker thread with text_search_files(). The results go to the Search
 * tab of the preview notebook (code_refs.h); Enter opens one. Files are read
 * from disk: unsaved changes are not searched. */
#include "app_types.h"

void project_search_show(AppData* app);

#endif /* PROJECT_SEARCH_H */
