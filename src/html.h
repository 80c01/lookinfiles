#ifndef HTML_H
#define HTML_H

#include "app_types.h"

void ensure_html_tags(GtkTextBuffer* buffer);
/* view: where images fit their width (NULL: natural size) */
void render_html_to_buffer(GtkTextBuffer* buffer,
                           GtkTextView* view,
                           const gchar* html_text,
                           const gchar* doc_path,
                           gboolean is_preview,
                           const gchar* file_name,
                           gint64 total_size);

#endif /* HTML_H */
