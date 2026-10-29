#ifndef EPUB_H
#define EPUB_H

#include <glib.h>

/* EPUB e-books (2 and 3) converted to HTML for the HTML renderer: title,
 * author and the other metadata, the cover image, the table of contents
 * (EPUB 3 navigation document or EPUB 2 NCX), then the first chapters in
 * reading order, with their images as data: URIs. Pure GLib, unit-tested. */

typedef struct {
    gchar* title;
    gchar* author;
    guint n_chapters;        /* spine items */
} EpubInfo;

/* HTML of the book's start: about max_text bytes of chapter text. NULL with
 * error set if it is not a readable EPUB. info (may be NULL) is filled;
 * free it with epub_info_clear(). */
gchar* epub_file_to_html(const gchar* path, gsize max_text, EpubInfo* info, GError** error);
void epub_info_clear(EpubInfo* info);

#endif /* EPUB_H */
