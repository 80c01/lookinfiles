#ifndef OFFICE97_H
#define OFFICE97_H

/* Previews of OLE2 compound files (docs/previews.md): the format comes from
 * the stream names (WordDocument, Workbook / Book, PowerPoint Document...),
 * then its reader builds the preview like the modern counterpart's (HTML
 * for documents and presentations, a grid for workbooks). Encrypted files
 * show the property card and "Encrypted"; a reader failure and other
 * compound files (.msi, .vsd, Works...) show the property card and the
 * stream list. Pure GLib, unit-tested. */
#include <glib.h>

#include "office/ole_reader.h"
#include "preview/preview_doc.h"

typedef enum {
    OFFICE97_UNKNOWN = 0,
    OFFICE97_WORD,         /* WordDocument */
    OFFICE97_EXCEL,        /* Workbook (BIFF8) */
    OFFICE97_EXCEL5,       /* Book (BIFF5: Excel 5.0 / 95) */
    OFFICE97_POWERPOINT,   /* PowerPoint Document */
    OFFICE97_OUTLOOK_MSG   /* __substg1.0_* streams */
} Office97Format;

Office97Format office97_format(const OleReader* ole);
/* "Word 97-2003 document", "Compound file"... */
const gchar* office97_format_label(Office97Format format);

/* Preview of the compound file at path (NULL with error set when it is not
 * a readable compound file: the viewer then shows the hex dump) */
PreviewDoc* office97_preview(const gchar* path, const gchar* name, gint64 size, gsize max_text, GError** error);

/* The fallback: title, property card, a note (reason, may be NULL) and the
 * stream list */
PreviewDoc* office97_summary(OleReader* ole, const gchar* name, gint64 size, const gchar* reason);

#endif /* OFFICE97_H */
