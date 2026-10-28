#ifndef FILE_TYPES_H
#define FILE_TYPES_H

#include <glib.h>

/* File extension without the dot ("" if none; a leading dot of dot-files is ignored) */
const gchar* file_extension(const gchar* name);

gboolean is_markdown_file(const gchar* name);
gboolean is_html_file(const gchar* name);
gboolean is_org_file(const gchar* name);
gboolean is_image_file(const gchar* name);
gboolean is_pdf_file(const gchar* name);
gboolean is_video_file(const gchar* name);
gboolean is_audio_file(const gchar* name);

/* TRUE if the preview pane has a rich renderer for this file */
gboolean is_renderable_file(const gchar* name);

/* VS Code-like emoji icon for a tree entry */
const gchar* file_icon(const gchar* name, gboolean is_dir);

#endif /* FILE_TYPES_H */
