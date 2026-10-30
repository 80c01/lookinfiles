#ifndef PPT_READER_H
#define PPT_READER_H

/* PowerPoint 97-2003 presentations (.ppt, .pps) to HTML for the preview,
 * like pptx: slide by slide, the title then the body text, with the
 * speaker notes. Text comes from the SlideListWithText of the document
 * (PowerPoint) or else from the drawings of the slide containers
 * (LibreOffice); TextCharsAtom (UTF-16) and TextBytesAtom (8-bit), title
 * vs body from TextHeaderAtom. Masters are skipped. Pure GLib,
 * unit-tested. */
#include <glib.h>

#include "ole_reader.h"

/* The presentation as an HTML page with head (may be NULL); *slides (may
 * be NULL) receives the slide count. NULL with error set when it cannot be
 * read; *encrypted (may be NULL) tells when it is password-protected. */
gchar* ppt_reader_to_html(OleReader* ole, const gchar* head, gsize max_text, guint* slides, gboolean* encrypted,
                          GError** error);

#endif /* PPT_READER_H */
