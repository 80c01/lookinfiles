#ifndef MARKDOWN_PARSE_H
#define MARKDOWN_PARSE_H

/* Markdown parsed into the text, tags and objects markdown_show() puts in a
 * text view (md_out.h). No widgets: runs in a worker thread (pictures are
 * read there too). */
#include <glib.h>

#include "markup/md_out.h"

/* doc_path: the Markdown file (relative pictures), or NULL. is_preview: the
 * text stops before the end of the file (a banner says so, with file_name
 * and total_size). widget_tables: tables as objects (grid widgets when
 * shown); FALSE: text tables with box-drawing borders. */
MdOut* markdown_parse(const gchar* markdown_text, const gchar* doc_path, gboolean is_preview,
                      const gchar* file_name, gint64 total_size, gboolean widget_tables);

#endif /* MARKDOWN_PARSE_H */
