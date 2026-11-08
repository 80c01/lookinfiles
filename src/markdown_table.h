#ifndef MARKDOWN_TABLE_H
#define MARKDOWN_TABLE_H

/* Markdown tables for the Markdown renderer (markdown.h): a row split into
 * cells, and the table inserted in a text buffer, as a grid widget anchored
 * in the text view (VS Code look; a table wider than the text scrolls
 * horizontally in its own frame) or, without a view, as a text table with
 * box-drawing borders (tags md_table, md_table_header, md_table_border of
 * ensure_markdown_tags()). */
#include <gtk/gtk.h>

/* Cells of a table row ("| a | b |"), trimmed; "\|" is a pipe. Free with
 * g_ptr_array_unref(). */
GPtrArray* markdown_table_split_row(const gchar* row);

/* Insert at iter the table rows (GPtrArray of cell arrays, header first,
 * cells may be replaced) with cols columns; aligns: the cells of the
 * separator row (":--", "--:", ":-:"). view may be NULL. */
void markdown_table_insert(GtkTextBuffer* buffer, GtkTextView* view, GtkTextIter* iter,
                           GPtrArray* rows, GPtrArray* aligns, guint cols);

#endif /* MARKDOWN_TABLE_H */
