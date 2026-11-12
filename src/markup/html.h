#ifndef HTML_H
#define HTML_H

#include "core/app_types.h"
#include "markup/md_out.h"

/* HTML parsed into text, tags of ensure_html_tags() and objects (md_out.h):
 * no widgets, runs in a worker thread (pictures are read there too) */
MdOut* html_parse(const gchar* html_text, const gchar* doc_path, gboolean is_preview,
                  const gchar* file_name, gint64 total_size);

/* Parse and show HTML in buffer; view: where images fit their width (NULL:
 * natural size) */
void render_html_to_buffer(GtkTextBuffer* buffer,
                           GtkTextView* view,
                           const gchar* html_text,
                           const gchar* doc_path,
                           gboolean is_preview,
                           const gchar* file_name,
                           gint64 total_size);

#endif /* HTML_H */
