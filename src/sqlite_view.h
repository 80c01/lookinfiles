#ifndef SQLITE_VIEW_H
#define SQLITE_VIEW_H

#include <glib.h>

#include "preview/preview_doc.h"

/* SQLite databases for the preview. With the SQLite library (HAVE_SQLITE):
 * the tables and views with their row counts and columns, then the first
 * rows of the first tables, the database opened read-only and immutable (no
 * lock, no journal written). Without it: the details of the file header.
 * Every query is bounded in time. Unit-tested. */

/* Header details of a database file (pure, no SQLite needed); FALSE if the
 * bytes do not start with the SQLite header */
typedef struct {
    guint page_size;
    guint64 page_count;
    const gchar* encoding;   /* "UTF-8", "UTF-16le", "UTF-16be" */
    guint32 user_version;
    guint32 application_id;
    guint32 sqlite_version;  /* e.g. 3046001 */
    gboolean wal;            /* write-ahead log mode */
} SqliteHeader;

gboolean sqlite_view_read_header(const guchar* head, gsize len, SqliteHeader* header);

/* Append the header fields, then (with SQLite) the tables and their first
 * rows. FALSE with error set if the file cannot be read as a database. */
gboolean sqlite_view_append(PreviewDoc* doc, const gchar* path, const guchar* head, gsize head_len,
                            GError** error);

/* Rows shown per table, tables shown with their rows */
#define SQLITE_VIEW_ROWS 50
#define SQLITE_VIEW_TABLES 3

#endif /* SQLITE_VIEW_H */
