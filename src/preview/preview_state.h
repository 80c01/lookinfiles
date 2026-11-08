#ifndef PREVIEW_STATE_H
#define PREVIEW_STATE_H

#include "core/app_types.h"

/* What the preview shows: the file, its detected kind, raw or rendered view,
 * zoom and page. Read by any module; written by the viewer and the few places
 * that change what is shown (explorer, editor, workspaces). Freed at exit. */

/* The file shown in the preview, NULL for a folder, a summary or nothing */
const gchar* preview_state_path(const AppData* app);
const gchar* preview_state_name(const AppData* app);
/* Show path (named name) in the preview; NULL path: no file */
void preview_state_set_file(AppData* app, const gchar* path, const gchar* name);

/* Kind of the shown file, detected from its content (from its name until read) */
FileKind preview_state_kind(const AppData* app);
/* GIO content type of the shown text, NULL until read */
const gchar* preview_state_content_type(const AppData* app);
/* Text encoding of the shown file (static string), NULL if not text */
const gchar* preview_state_encoding(const AppData* app);
/* Set the three above; TRUE when one of them changed */
gboolean preview_state_set_detected(AppData* app, FileKind kind, const gchar* encoding,
                                    const gchar* content_type);

/* FALSE = rendered preview (default), TRUE = raw text */
gboolean preview_state_raw(const AppData* app);
void preview_state_set_raw(AppData* app, gboolean raw);

/* Image and PDF zoom, relative to the fitted size (1.0 by default) */
gdouble preview_state_zoom(const AppData* app);
void preview_state_set_zoom(AppData* app, gdouble zoom);

/* Paged previews (mailbox messages): the page shown and the number of pages
 * after the first view (0: not paged); page is clamped to pages */
guint preview_state_page(const AppData* app);
guint preview_state_pages(const AppData* app);
void preview_state_set_page(AppData* app, guint page, guint pages);

void preview_state_free(AppData* app);

#endif /* PREVIEW_STATE_H */
