#ifndef DOCX_H
#define DOCX_H

#include <glib.h>

#include "zip_reader.h"

/* Word documents (.docx, Office Open XML) converted to simple HTML for the
 * preview: headings, paragraphs, bold / italic / underline / strike,
 * superscript, links, bulleted and numbered lists, tables, images (as
 * data: URIs) and the author. Not kept: page layout, fonts, colours, headers
 * and footers, footnotes, comments, text boxes, deleted tracked changes.
 * Pure GLib: usable in a worker thread. */

/* Largest document.xml read (uncompressed) */
#define DOCX_MAX_XML (64u * 1024 * 1024)

/* TRUE if the archive is a Word document (has a main document part) */
gboolean docx_is_document(ZipReader* zip);

/* HTML of a Word document. Stops after about max_text bytes of text and then
 * ends with a note saying the document was truncated. NULL with error set if
 * the archive is not a readable Word document. */
gchar* docx_to_html(ZipReader* zip, gsize max_text, GError** error);

/* Same, for a file on disk */
gchar* docx_file_to_html(const gchar* path, gsize max_text, GError** error);

#endif /* DOCX_H */
