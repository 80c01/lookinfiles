#ifndef PREVIEW_DOC_VIEW_H
#define PREVIEW_DOC_VIEW_H

#include <gtk/gtk.h>

#include "preview/preview_doc.h"

/* Show a PreviewDoc in a text view (UI thread): styled text with its tags
 * and images, or the HTML / Markdown renderers for those formats. Sets the
 * font (monospace or not) and wrapping the document asks for. path is the
 * previewed file (relative links and images), name and size its name and
 * size. */
void preview_doc_show(GtkTextView* view, const PreviewDoc* doc, const gchar* path,
                      const gchar* name, gint64 size);

/* Insert a styled-text document at the end of a buffer (no view settings) */
void preview_doc_insert(GtkTextBuffer* buffer, const PreviewDoc* doc);

#endif /* PREVIEW_DOC_VIEW_H */
