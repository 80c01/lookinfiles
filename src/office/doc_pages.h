#ifndef DOC_PAGES_H
#define DOC_PAGES_H

#include <glib.h>

#include "office/doc_writer.h"
#include "preview/preview_doc.h"

/* Word processing documents (.docx, .doc, .odt) drawn page by page like
 * the print layout of Word or Writer: the model of doc_writer laid out with
 * Pango on pages of the document's size and margins, the header and footer
 * on every page, paragraphs split across pages line by line, table rows
 * kept whole, pictures inline. Pages are pictures (pixels) of a paged
 * PreviewDoc, zoomed like slides (+ / -). Line spacing, columns, floating
 * pictures and page number fields are not reproduced. Usable in a worker
 * thread, unit-tested. */

/* Pages drawn at most (memory); the others are counted */
#define DOC_PAGES_MAX_DRAWN 20

/* The preview: title, subtitle and page count, then the pages drawn at
 * zoom (0: 1), then the note (may be NULL). header and footer may be NULL. */
PreviewDoc* doc_pages_preview(const gchar* title, const gchar* subtitle, const DocPageSetup* setup,
                              const DocModel* header, const DocModel* body, const DocModel* footer,
                              const gchar* note, gdouble zoom);

/* Pages the body fills (nothing drawn) */
guint doc_pages_count(const DocPageSetup* setup, const DocModel* header, const DocModel* body,
                      const DocModel* footer);

/* The page view (pages) or the text view (doc_model_preview() under
 * doc_writer_page()) of a document */
PreviewDoc* doc_pages_or_text(gboolean pages, const gchar* title, const gchar* subtitle, const DocPageSetup* setup,
                              const DocModel* header, const DocModel* body, const DocModel* footer,
                              const gchar* note, gdouble zoom);

#endif /* DOC_PAGES_H */
