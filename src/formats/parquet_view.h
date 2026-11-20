#ifndef PARQUET_VIEW_H
#define PARQUET_VIEW_H

#include <gio/gio.h>

#include "preview/preview_doc.h"

/* Apache Parquet files for the preview: the first rows as a table (like a
 * data viewer shows them), then the schema with each column's type,
 * encodings, compression and size, the row groups and the key / value
 * metadata. Only the footer and the first pages of each column are read
 * (parquet_meta, parquet_pages). Unit-tested. */

/* Rows and columns shown in the table */
#define PARQUET_VIEW_ROWS 100
#define PARQUET_VIEW_COLUMNS 64
/* Largest footer read */
#define PARQUET_VIEW_FOOTER_MAX (64 * 1024 * 1024)

/* Append the preview of the file at path. FALSE with error set if it is not
 * a readable Parquet file (a column that cannot be decoded only adds a
 * note). */
gboolean parquet_view_append_file(PreviewDoc* doc, const gchar* path, GCancellable* cancellable,
                                  GError** error);
/* The same for a file in memory */
gboolean parquet_view_append_bytes(PreviewDoc* doc, const guchar* data, gsize len, GError** error);

#endif /* PARQUET_VIEW_H */
