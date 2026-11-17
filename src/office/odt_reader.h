#ifndef ODT_READER_H
#define ODT_READER_H

#include <glib.h>

#include "formats/zip_reader.h"
#include "office/doc_writer.h"
#include "preview/preview_doc.h"

/* OpenDocument text (.odt, .ott) drawn page by page (doc_pages.h): the
 * paragraphs, headings, lists, tables and pictures of content.xml written
 * with doc_writer, their fonts, sizes, colours, alignment, indents and
 * spacing from the styles (odp_styles), page breaks, the page size and
 * margins, header and footer of the master page. Its text view stays the
 * HTML of odf.h. Pure GLib and Pango, unit-tested. */

/* A text document read */
typedef struct {
    DocModel* body;
    DocModel* header;       /* of the master page, NULL: none */
    DocModel* footer;
    DocPageSetup page;
    gchar* title;           /* meta.xml, NULL: none */
    gchar* author;
    gboolean truncated;     /* stopped after max_text characters */
} OdtDocument;

/* Read content.xml, styles.xml and meta.xml; NULL with error set if it is
 * not a readable OpenDocument text */
OdtDocument* odt_reader_read(ZipReader* zip, gsize max_text, GError** error);
void odt_document_free(OdtDocument* doc);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(OdtDocument, odt_document_free)

/* The pages of the document under its title (else name) and a subtitle
 * ("OpenDocument text · author · size_text"; size_text may be NULL), drawn
 * at zoom (0: 1). Stops after about max_text characters with a note. NULL
 * with error set if it is not a readable OpenDocument text. */
PreviewDoc* odt_reader_pages(ZipReader* zip, const gchar* name, const gchar* size_text, gsize max_text,
                             gdouble zoom, GError** error);

/* Same, for a file on disk */
PreviewDoc* odt_file_pages(const gchar* path, const gchar* name, const gchar* size_text, gsize max_text,
                           gdouble zoom, GError** error);

#endif /* ODT_READER_H */
