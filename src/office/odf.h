#ifndef ODF_H
#define ODF_H

#include <glib.h>

/* OpenDocument text documents (.odt) converted to simple HTML for the HTML
 * renderer: headings, paragraphs (Title and Subtitle styles as headings),
 * bold / italic / underline / strike-through from the automatic and named
 * styles, bulleted and numbered lists, tables, links, line breaks, images
 * (as data: URIs). Notes, comments and tracked deletions are skipped. The
 * author and title from meta.xml go in <meta> / <title>. Presentations are
 * drawn by odp_reader. Pure GLib, unit-tested. */

/* HTML of an OpenDocument text, about max_text bytes of text at most (then a
 * truncation note). NULL with error set if it is not a readable
 * OpenDocument file. */
gchar* odf_file_to_html(const gchar* path, gsize max_text, GError** error);

#endif /* ODF_H */
