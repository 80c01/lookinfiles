#ifndef MARKDOWN_H
#define MARKDOWN_H

#include "core/app_types.h"
#include "markup/md_out.h"

void ensure_markdown_tags(GtkTextBuffer* buffer);
/* Show parsed Markdown (markdown_parse()) in buffer: one insertion, then
 * the objects and the tags. With a view, tables are grid widgets; without
 * (NULL), table objects are left out (parse with widget_tables FALSE). */
void markdown_show(GtkTextBuffer* buffer, GtkTextView* view, const MdOut* md);
/* Parse and show Markdown in buffer. With a view, tables are grid widgets
 * anchored in the text; without (NULL), text tables. */
void render_markdown_to_buffer(GtkTextBuffer* buffer,
                               GtkTextView* view,
                               const gchar* markdown_text,
                               const gchar* doc_path,
                               gboolean is_preview,
                               const gchar* file_name,
                               gint64 total_size);

#endif /* MARKDOWN_H */
