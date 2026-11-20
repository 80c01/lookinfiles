#ifndef DEEP_SEARCH_REQUEST_H
#define DEEP_SEARCH_REQUEST_H

/* A Deep Search asked by another tab (app->deep_search_request, see
 * app_types.h): the Disk Usage tab lists the files of a group of its
 * statistics this way. Wired to APP_EVENT_RUN_DEEP_SEARCH in main.c. */
#include "core/app_types.h"

/* APP_EVENT_RUN_DEEP_SEARCH: fill the Deep Search tab with the request
 * (files only, hidden entries included) and start the search */
void deep_search_request_run(AppData* app);

#endif /* DEEP_SEARCH_REQUEST_H */
