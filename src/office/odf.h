#ifndef ODF_H
#define ODF_H

#include <glib.h>

/* OpenDocument text documents (.odt) and presentations (.odp) converted to
 * simple HTML for the HTML renderer: headings, paragraphs (Title and
 * Subtitle styles as headings), bold / italic / underline / strike-through
 * from the automatic and named styles, bulleted and numbered lists, tables,
 * links, line breaks, images (as data: URIs), and for presentations one
 * section per slide. Notes, comments and tracked deletions are skipped. The
 * author and title from meta.xml go in <meta> / <title>. Pure GLib,
 * unit-tested. */

typedef struct {
    guint n_slides;          /* presentations: number of slides */
    gboolean presentation;
} OdfInfo;

/* HTML of an OpenDocument file, about max_text bytes of text at most (then a
 * truncation note). NULL with error set if it is not a readable
 * OpenDocument text or presentation. */
gchar* odf_file_to_html(const gchar* path, gsize max_text, OdfInfo* info, GError** error);

#endif /* ODF_H */
