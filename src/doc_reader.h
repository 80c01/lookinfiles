#ifndef DOC_READER_H
#define DOC_READER_H

/* Word 97-2003 documents (.doc) shown like Word (doc_writer), like docx:
 * the main text from the piece table (8-bit and UTF-16 pieces); character
 * formatting (bold, italic, underline, strike, capitals, super- and
 * subscript, size, font from the font table, colour, highlight) and
 * paragraph layout (alignment, indents, spacing, page break before) from
 * the styles (STSH, with their base styles) and the direct formatting (PAPX
 * / CHPX FKPs); list items (bullets), tables (cell and row marks),
 * hyperlink fields, page breaks; the first header and footer. Other field
 * codes, pictures and footnote marks are skipped. Word 6 / 95 files: the
 * text of their single 8-bit piece, as paragraphs. Pure GLib, unit-tested. */
#include <glib.h>

#include "ole_reader.h"
#include "preview_doc.h"

/* The document under a title and subtitle. NULL with error set when it
 * cannot be read; *encrypted (may be NULL) tells when it is
 * password-protected. Stops after max_text characters of text. */
PreviewDoc* doc_reader_preview(OleReader* ole, const gchar* title, const gchar* subtitle, gsize max_text,
                               gboolean* encrypted, GError** error);

#endif /* DOC_READER_H */
