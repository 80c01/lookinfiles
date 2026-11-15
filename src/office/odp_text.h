#ifndef ODP_TEXT_H
#define ODP_TEXT_H

#include <glib.h>

#include "office/odp_styles.h"
#include "office/slide_render.h"

/* Text of OpenDocument drawings as slide paragraphs: text:p / text:h with
 * their spans (fonts, sizes, weights, colours, positions), alignment,
 * margins and spacing, lists with bullets and numbers from the list styles,
 * spaces, tabs and line breaks, page numbers and the footer / date fields;
 * and tables (columns, rows, spans, cell styles and table templates). Pure
 * GLib (and the slide model), unit-tested through odp_reader. */

typedef struct {
    const OdpStyles* styles;
    OdpCascade base;            /* the shape's styles, most specific first */
    const gchar* outline_prefix; /* "<master>-outline" in outline frames, else NULL */
    const OdpList* list;        /* list style of the shape, NULL: none */
    guint page_number;
    const gchar* footer;        /* field texts, may be NULL */
    const gchar* date_time;
    const gchar* header;
    guint32 background;         /* colour under the text (automatic text colour) */
    gdouble default_size;       /* text size no style sets, 0: 18 pt */
} OdpTextContext;

/* Append the paragraphs of a text container (draw:text-box, a shape, a
 * table cell) to out (SlideParagraph*) */
void odp_text_paragraphs(const OdpTextContext* ctx, const OdpNode* container, GPtrArray* out);

/* Table of a table:table in a frame width x height (points); NULL if it has
 * no cell */
SlideTable* odp_text_table(const OdpTextContext* ctx, const OdpNode* table, gdouble width, gdouble height);

#endif /* ODP_TEXT_H */
