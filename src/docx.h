#ifndef DOCX_H
#define DOCX_H

#include <glib.h>

#include "preview_doc.h"
#include "zip_reader.h"

/* Word documents (.docx, Office Open XML) shown like Word shows them
 * (doc_writer): fonts, sizes, colours, highlights, alignment, indents and
 * spacing from the styles (basedOn chains, document defaults, theme fonts)
 * and the direct formatting; numbered and bulleted lists with Word's labels;
 * tables, pictures, links, page breaks; the first header and footer. Not
 * kept: exact page layout, columns, floating positions, text boxes,
 * footnotes, comments, deleted tracked changes. Pure GLib: usable in a
 * worker thread. */

/* Largest document.xml read (uncompressed) */
#define DOCX_MAX_XML (64u * 1024 * 1024)

/* TRUE if the archive is a Word document (has a main document part) */
gboolean docx_is_document(ZipReader* zip);

/* The preview: title (the document's title, else name), subtitle ("Word
 * document · author · size_text"; size_text may be NULL), the document.
 * Stops after about max_text characters with a note. NULL with error set if
 * the archive is not a readable Word document. */
PreviewDoc* docx_to_preview(ZipReader* zip, const gchar* name, const gchar* size_text, gsize max_text,
                            GError** error);

/* Same, for a file on disk */
PreviewDoc* docx_file_preview(const gchar* path, const gchar* name, const gchar* size_text, gsize max_text,
                              GError** error);

#endif /* DOCX_H */
