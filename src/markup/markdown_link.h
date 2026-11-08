#ifndef MARKDOWN_LINK_H
#define MARKDOWN_LINK_H

/* Links that can be followed in rendered Markdown (the Help tab). Off by
 * default: Markdown previews keep their links as coloured text.
 * markdown_link_attach() turns them on for a text view: the renderer
 * (markdown.h, markdown_table.h) then records each link of the view's
 * buffer, a click on a link (also in a table cell) calls the view's handler
 * with its URL, and the pointer becomes a hand over links. */
#include <gtk/gtk.h>

typedef void (*MarkdownLinkFunc)(GtkTextView* view, const gchar* url, gpointer data);

/* Follow the links rendered in view with func (data: its user data) */
void markdown_link_attach(GtkTextView* view, MarkdownLinkFunc func, gpointer data);

/* Renderer side: TRUE when the links of buffer are recorded */
gboolean markdown_link_enabled(GtkTextBuffer* buffer);
/* Forget the recorded links (the buffer is rendered again) */
void markdown_link_clear(GtkTextBuffer* buffer);
/* Record a link to url on the text from start to end */
void markdown_link_add(GtkTextBuffer* buffer, const GtkTextIter* start, const GtkTextIter* end, const gchar* url);
/* A table cell label with <a href> links: clicking one follows it in view */
void markdown_link_connect_label(GtkWidget* label, GtkTextView* view);

/* Links recorded in buffer, in text order (table cells not included) */
guint markdown_link_count(GtkTextBuffer* buffer);
/* URL of link i and its text range (start / end may be NULL), or NULL */
const gchar* markdown_link_get(GtkTextBuffer* buffer, guint i, GtkTextIter* start, GtkTextIter* end);
/* Index of the link at iter, or -1 */
gint markdown_link_index_at(GtkTextBuffer* buffer, const GtkTextIter* iter);

/* Follow url in view as if it was clicked */
void markdown_link_activate(GtkTextView* view, const gchar* url);

#endif /* MARKDOWN_LINK_H */
