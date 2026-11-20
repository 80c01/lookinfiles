/* A Deep Search asked by another tab (see deep_search_request.h) */
#include "search/deep_search_request.h"
#include "search/deep_search_store.h"
#include "search/deep_search_tab.h"

void deep_search_request_run(AppData* app) {
    const DeepSearchRequest* r = app != NULL ? app->deep_search_request : NULL;
    if (r == NULL || r->folder == NULL) return;
    SavedDeepSearch s;
    saved_deep_search_init(&s);
    s.folder = g_strdup(r->folder);
    s.extensions = g_strdup(r->extensions);
    s.min_size = g_strdup(r->min_size);
    s.max_size = g_strdup(r->max_size);
    s.after = g_strdup(r->after);
    s.before = g_strdup(r->before);
    s.exclude = g_strdup(r->exclude);
    s.kind = DEEP_KIND_FILES;
    s.hidden = TRUE; /* as the Disk Usage tab counts them */
    deep_search_tab_run_saved(app, &s);
    saved_deep_search_clear(&s);
}
