#ifndef PREVIEW_FORMATS_H
#define PREVIEW_FORMATS_H

#include <gio/gio.h>

#include "core/file_types.h"
#include "preview_doc.h"

/* Registry of the structured previews: one builder per FileKind, run in the
 * preview worker thread. A builder reads only what it needs (bounded) and
 * returns a PreviewDoc, or NULL with an error so the viewer falls back to the
 * next level down (plain text, or the hex dump for binary content).
 *
 * Kinds without a builder (plain text, Markdown, HTML, Org, media) keep
 * their own renderers in the viewer. No widgets: unit-tested, and used by
 * the preview benchmark (tools/preview_tool.c). */

typedef struct {
    const gchar* path;        /* file on disk */
    const gchar* name;        /* display name (type detection, titles) */
    gint64 size;              /* file size in bytes */
    FileKind kind;
    const guchar* head;       /* first bytes of the file (up to FILE_SNIFF_BYTES) */
    gsize head_len;
    /* Text kinds (file_kind_is_text()): the text read by the viewer, valid
     * UTF-8, and whether it stops before the end of the file */
    const gchar* text;
    gsize text_len;
    gboolean text_truncated;
    /* Page asked with PgDn / PgUp in documents that have pages after their
     * first view (PreviewDoc.pages: messages of a mailbox), 0 otherwise */
    guint page;
} PreviewInput;

/* Largest text a builder converts (documents, notebooks...) */
#define PREVIEW_FORMAT_MAX_TEXT (256 * 1024)

/* TRUE if the kind has a structured preview builder */
gboolean preview_format_has_builder(FileKind kind);

/* Build the preview of a file. NULL with error set when the content cannot
 * be shown this way (G_IO_ERROR_NOT_SUPPORTED: no builder for the kind). */
PreviewDoc* preview_format_build(const PreviewInput* input, GCancellable* cancellable,
                                 GError** error);

/* Whole pipeline on a file, as the viewer runs it: detect the kind, read the
 * text of text kinds (up to text_limit bytes), build. NULL with error set if
 * the file cannot be read or the kind has no builder (or it failed).
 * *kind (optional) receives the detected kind. Used by tests and tools. */
PreviewDoc* preview_format_build_file(const gchar* path, const gchar* name, gsize text_limit,
                                      GCancellable* cancellable, FileKind* kind, GError** error);

/* Hex dump preview of the first bytes of a binary file: a dimmed header
 * (kind, name, size, how much is shown, why the structured preview failed if
 * reason is set), then the dump with dimmed offsets and a coloured ASCII
 * column. */
PreviewDoc* preview_format_hex_dump(const gchar* name, FileKind kind, gint64 size,
                                    const guchar* data, gsize len, const gchar* reason);

/* "12.3 kB" style size, or "" for a negative size. Caller frees. */
gchar* preview_format_size(gint64 size);

#endif /* PREVIEW_FORMATS_H */
