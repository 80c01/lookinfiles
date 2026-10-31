#ifndef PPT_READER_H
#define PPT_READER_H

/* PowerPoint 97-2003 presentations (.ppt, .pps) read into slides
 * (slide_render), like pptx: the shapes of each slide's drawing (OfficeArt:
 * anchors, groups, geometry, fill, outline, rotation, pictures from the
 * Pictures stream, placeholders) drawn over its background, with the
 * master's shapes; text from the drawing's text boxes or the slide list
 * (SlideListWithText, OutlineTextRefAtom), styled by the StyleTextPropAtom
 * runs over the master's text styles (TxMasterStyleAtom); colours from the
 * colour schemes, fonts from the font collection; the speaker notes.
 * Slides without a drawing get their text in the default title and body
 * boxes. Pure GLib (and the slide model), unit-tested. */
#include <glib.h>

#include "ole_reader.h"
#include "slide_render.h"

/* The slides; NULL with error set when they cannot be read; *encrypted
 * (may be NULL) tells when the presentation is password-protected */
SlideDeck* ppt_reader_read(OleReader* ole, gboolean* encrypted, GError** error);

#endif /* PPT_READER_H */
