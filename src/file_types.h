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
/* Word document: .docx, .docm, .dotx, .dotm (not the legacy binary .doc) */
gboolean is_word_document_file(const gchar* name);

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
    FILE_KIND_BINARY,    /* binary content of an unknown format */
    FILE_KIND_EXECUTABLE,/* ELF: executable, shared library, object file, core dump */
    FILE_KIND_DOCUMENT,  /* word processor document (.docx, .odt) */
    FILE_KIND_SPREADSHEET, /* .xlsx, .ods */
    FILE_KIND_PRESENTATION,/* .pptx, .odp */
    FILE_KIND_EBOOK,     /* .epub */
    FILE_KIND_ARCHIVE,   /* zip, tar, gzip, bzip2, xz, zstd, 7z, rar, ar */
    FILE_KIND_FONT,      /* TrueType, OpenType, WOFF */
    FILE_KIND_DATABASE,  /* SQLite */
    FILE_KIND_CERTIFICATE, /* PEM (text) or DER (binary) certificates and keys */
    /* Text formats with a structured preview */
    FILE_KIND_CSV,       /* comma / tab separated values */
    FILE_KIND_JSON,      /* JSON, JSON Lines */
    FILE_KIND_NOTEBOOK,  /* Jupyter notebook (.ipynb) */
    FILE_KIND_DIFF,      /* unified diff, git patch */
    FILE_KIND_LOG,       /* log file */
    FILE_KIND_CALENDAR,  /* iCalendar (.ics) */
    FILE_KIND_CONTACT,   /* vCard (.vcf) */
    FILE_KIND_DESKTOP,   /* freedesktop .desktop entry */
    FILE_KIND_MANPAGE,   /* roff manual page */
    FILE_KIND_RTF,       /* Rich Text Format */
    FILE_KIND_TRANSLATION /* gettext catalogs (.po, .pot, .mo); keep last (FILE_KIND_LAST) */
} FileKind;

#define FILE_KIND_LAST FILE_KIND_TRANSLATION

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

/* TRUE for kinds whose content is text (shown as text by the raw view) */
gboolean file_kind_is_text(FileKind kind);

/* TRUE if the first bytes of a file look like text (no NUL byte, UTF-8 or
 * few control characters). A kind can hold both: a PEM certificate is text,
 * a DER one binary; a manual page can be gzipped. */
gboolean file_content_is_text(const guchar* head, gsize len);

/* Short name of a kind for the status bar ("CSV", "Archive"...); NULL for
 * plain text (the syntax language names it) */
const gchar* file_kind_label(FileKind kind);

/* VS Code-like emoji icon for a tree entry */
const gchar* file_icon(const gchar* name, gboolean is_dir);

#endif /* FILE_TYPES_H */
