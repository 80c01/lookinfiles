#ifndef MARKDOWN_H
#define MARKDOWN_H

#include "app_types.h"

void ensure_markdown_tags(GtkTextBuffer* buffer);
void render_markdown_to_buffer(GtkTextBuffer* buffer,
                               const gchar* markdown_text,
                               const gchar* doc_path,
                               gboolean is_preview,
                               const gchar* file_name,
                               gint64 total_size);

#endif /* MARKDOWN_H */
