#ifndef DEEP_SEARCH_BOOKMARK_H
#define DEEP_SEARCH_BOOKMARK_H

/* Search bookmarks: save the current Deep Search criteria under a name in
 * the bookmarks tree (lookinfiles-search://<id>, stored in
 * app_config_dir()/searches/<id>.ini), and run a saved search when opened.
 * Wired to APP_EVENT_SAVE_SEARCH_BOOKMARK / APP_EVENT_OPEN_SEARCH_BOOKMARK
 * in main.c. */
#include "core/app_types.h"

/* APP_EVENT_SAVE_SEARCH_BOOKMARK: ask for a name, bookmark folder and
 * whether to use the explorer's current folder, save the criteria and add
 * the bookmark. */
void deep_search_bookmark_save(AppData* app);

/* APP_EVENT_OPEN_SEARCH_BOOKMARK: load app->deep_search_open_target (taken),
 * fill the Deep Search tab and start the search. */
void deep_search_bookmark_open(AppData* app);

#endif /* DEEP_SEARCH_BOOKMARK_H */
