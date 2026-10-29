#ifndef MAN_VIEW_H
#define MAN_VIEW_H

#include <glib.h>

#include "preview_doc.h"

/* Manual pages (roff with the man(7) macros, and the common mdoc(7) ones)
 * formatted for the preview like man does in a terminal: the title line,
 * bold section headings, indented paragraphs, tagged paragraphs (.TP, .IP),
 * bold and italic text (\fB, \fI, .B, .I, .BR...), special characters
 * (\(em, \(bu, \-...), no-fill blocks (.nf, .EX). Tables (.TS) are shown as
 * their raw rows. Pure GLib, unit-tested. */

/* Append the formatted page; FALSE (doc unchanged) if the text has no roff
 * request at all */
gboolean man_view_append(PreviewDoc* doc, const gchar* text, gsize len);

/* Text of a roff line with its escapes resolved and font changes dropped
 * (used for titles and tests). Caller frees. */
gchar* man_view_plain(const gchar* line);

#endif /* MAN_VIEW_H */
