#ifndef PPTX_H
#define PPTX_H

#include <glib.h>

/* PowerPoint presentations (.pptx, Office Open XML) converted to simple HTML
 * for the HTML renderer: one section per slide in presentation order, with
 * the title, subtitle, bullet points (nested by level), text boxes, tables
 * and pictures (as data: URIs). Speaker notes and layouts are not shown.
 * Pure GLib, unit-tested. */

/* HTML of a presentation, about max_text bytes of text at most; *n_slides
 * (may be NULL) receives the number of slides. NULL with error set if it is
 * not a readable presentation. */
gchar* pptx_file_to_html(const gchar* path, gsize max_text, guint* n_slides, GError** error);

#endif /* PPTX_H */
