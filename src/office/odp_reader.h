#ifndef ODP_READER_H
#define ODP_READER_H

#include <glib.h>

#include "preview/preview_doc.h"
#include "office/slide_render.h"

/* OpenDocument presentations (.odp) drawn like Impress (slide_render):
 * the page size of the master's page layout; each page's background (its
 * own or the master's: colour, gradient, picture) and the master's shapes
 * behind its own (placeholders left out; footer, date and page number
 * fields shown as the page asks); frames with text, pictures and tables,
 * rectangles, ellipses, lines, polygons, paths and custom shapes with their
 * position, transform (rotation), fill, opacity and outline; text styled
 * by the presentation, graphic, paragraph and text styles (odp_text); the
 * speaker notes. Pure GLib (and the slide model), unit-tested. */

/* Slides drawn as pictures at most (the others are listed) */
#define ODP_MAX_DRAWN 20

/* The slides of a presentation; NULL with error set if it is not one */
SlideDeck* odp_read_deck(const gchar* path, GError** error);

/* The preview: title (the file name), subtitle ("OpenDocument presentation
 * · N slides · size_text"; size_text may be NULL), the slides. *n_slides
 * (may be NULL) receives the number of slides. NULL with error set if it is
 * not a readable presentation. */
PreviewDoc* odp_file_preview(const gchar* path, const gchar* name, const gchar* size_text, guint* n_slides,
                             GError** error);

#endif /* ODP_READER_H */
