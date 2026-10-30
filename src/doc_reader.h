#ifndef DOC_READER_H
#define DOC_READER_H

/* Word 97-2003 documents (.doc) to simple HTML for the preview, like docx:
 * the main text from the piece table (8-bit and UTF-16 pieces), paragraphs,
 * headings (heading styles or outline levels), list items with their
 * level, tables (cell and row marks), bold / italic / underline / strike /
 * super- and subscript runs, hyperlink fields; other field codes, pictures
 * and footnote marks are skipped. Word 6 / 95 files: the text of their
 * single 8-bit piece, as paragraphs. Pure GLib, unit-tested. */
#include <glib.h>

#include "ole_reader.h"

/* The document as an HTML page with head (the <head> element, may be NULL).
 * NULL with error set when the document cannot be read; *encrypted (may be
 * NULL) tells when it is password-protected. Stops after max_text
 * characters of text. */
gchar* doc_reader_to_html(OleReader* ole, const gchar* head, gsize max_text, gboolean* encrypted, GError** error);

#endif /* DOC_READER_H */
