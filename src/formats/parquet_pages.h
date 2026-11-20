#ifndef PARQUET_PAGES_H
#define PARQUET_PAGES_H

#include <glib.h>

#include "formats/parquet_meta.h"

/* Apache Parquet pages: the first rows of a column as text. Decompresses
 * the pages (uncompressed, Snappy, GZIP, LZ4, LZ4_RAW, and ZSTD with
 * libzstd: HAVE_ZSTD), decodes the levels and the values (PLAIN,
 * dictionary, RLE, DELTA_*, BYTE_STREAM_SPLIT) and formats them by their
 * logical type. Reads only the pages it needs, through a callback; every
 * size is checked against the file. Pure, unit-tested. */

/* Read len bytes at offset; FALSE if they cannot be read */
typedef gboolean (*ParquetReadFunc)(gpointer source, guint64 offset, guchar* buf, gsize len);

/* Largest page read (compressed or not) */
#define PARQUET_PAGE_MAX (64 * 1024 * 1024)

/* Decompress in to exactly out_len bytes. FALSE with error set otherwise
 * (an unsupported codec, corrupt data). */
gboolean parquet_decompress(gint codec, const guchar* in, gsize in_len, guchar* out, gsize out_len,
                            GError** error);

/* TRUE if this build decompresses the codec */
gboolean parquet_codec_supported(gint codec);

/* Append the cells of the first max_rows rows of leaf column `leaf` to
 * cells (gchar*, NULL for a null value; lists as "[a, b]"), reading the row
 * groups in turn. FALSE with error set if a page cannot be read or decoded
 * (the cells read before stay in cells). */
gboolean parquet_column_read(const ParquetMeta* meta, guint leaf, ParquetReadFunc read, gpointer source,
                             guint64 file_size, guint max_rows, GPtrArray* cells, GError** error);

/* A value of a node as text: bytes in the PLAIN layout of its physical
 * type (a BOOLEAN: one byte 0 or 1; a BYTE_ARRAY: the bytes without their
 * length). Caller frees. */
gchar* parquet_format_value(const ParquetNode* node, const guchar* bytes, gsize len);

#endif /* PARQUET_PAGES_H */
