#ifndef DEEP_SEARCH_PREVIEW_H
#define DEEP_SEARCH_PREVIEW_H

/* The preview of the Deep Search results (deep_search_tab.h). The tab
 * moves like an editor tab; out of the preview notebook, beside the
 * preview, the result moved to in the list is previewed (rows passed over
 * with a held key are not), as is the result selected when the tab gets
 * there. Space previews the result selected; in the preview notebook, the
 * Preview tab then comes in front with the focus. */
#include "core/app_types.h"

typedef struct DeepSearchPreview DeepSearchPreview;

/* The preview of the results list (deep_search_list) of page, the tab */
DeepSearchPreview* deep_search_preview_new(AppData* app, GtkWidget* page, GtkWidget* list);
/* Freed with the page (its handler goes with it) */
void deep_search_preview_free(DeepSearchPreview* dp);
/* No result waiting to be previewed */
void deep_search_preview_cancel(DeepSearchPreview* dp);
/* A result selected (asked FALSE) or Space on it (asked TRUE): see
 * DeepSearchListHandlers.preview */
void deep_search_preview_result(DeepSearchPreview* dp, const gchar* path, gint line, gboolean asked);

/* The notebook holding page, or NULL (put aside with its workspace) */
GtkNotebook* deep_search_page_notebook(GtkWidget* page);
/* Show the preview panel when hidden */
void deep_search_show_preview_panel(AppData* app);

#endif /* DEEP_SEARCH_PREVIEW_H */
