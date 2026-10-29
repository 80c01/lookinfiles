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

/* Kind of content, detected from the first bytes of a file (magic numbers,
 * text vs binary). The name is only used where content cannot tell:
 * Markdown / Org / HTML fragments (plain text) and ambiguous containers. */
typedef enum {
    FILE_KIND_TEXT = 0,  /* plain text or source code */
    FILE_KIND_MARKDOWN,
    FILE_KIND_HTML,
    FILE_KIND_ORG,
    FILE_KIND_IMAGE,
    FILE_KIND_PDF,
    FILE_KIND_VIDEO,
    FILE_KIND_AUDIO,
    FILE_KIND_BINARY     /* binary content of an unknown format */
} FileKind;

/* Number of leading bytes file_kind_detect() needs */
#define FILE_SNIFF_BYTES 4096

/* Kind guessed from the name only (no I/O) */
FileKind file_kind_from_name(const gchar* name);

/* Kind from the name and the first bytes of the content (pure) */
FileKind file_kind_detect(const gchar* name, const guchar* head, gsize len);

/* Kind of a file on disk: reads its first FILE_SNIFF_BYTES bytes.
 * Falls back to file_kind_from_name() when the file cannot be read. */
FileKind file_kind_of_path(const gchar* path, const gchar* name);

/* TRUE for kinds shown by a media viewer (image, PDF, video, audio), not as text */
gboolean file_kind_is_media(FileKind kind);

/* TRUE if the preview pane has a rich renderer for this kind */
gboolean file_kind_is_renderable(FileKind kind);

/* VS Code-like emoji icon for a tree entry */
const gchar* file_icon(const gchar* name, gboolean is_dir);

#endif /* FILE_TYPES_H */
