#ifndef PPTX_H
#define PPTX_H

#include <glib.h>

#include "preview/preview_doc.h"
#include "slide_render.h"

/* PowerPoint presentations (.pptx, Office Open XML) drawn like PowerPoint
 * (slide_render): each slide from its shapes, its layout and master
 * (pptx_shapes), in presentation order, with its speaker notes. Pure GLib
 * (and cairo / Pango for the pictures), unit-tested. */

/* Slides drawn as pictures at most (the others are listed) */
#define PPTX_MAX_DRAWN 20

/* The slides of a presentation; NULL with error set if it is not one */
SlideDeck* pptx_read_deck(const gchar* path, GError** error);

/* The preview: title (the file name), subtitle ("PowerPoint presentation ·
 * N slides · size_text"; size_text may be NULL), the slides. *n_slides
 * (may be NULL) receives the number of slides. NULL with error set if it is
 * not a readable presentation. */
PreviewDoc* pptx_file_preview(const gchar* path, const gchar* name, const gchar* size_text, guint* n_slides,
                              GError** error);

#endif /* PPTX_H */
