#ifndef PARQUET_META_H
#define PARQUET_META_H

#include <glib.h>

/* Apache Parquet metadata: the footer (FileMetaData) and the page headers,
 * read from their Thrift compact encoding. Pure, no I/O; parquet_pages
 * decodes the pages, parquet_view shows the file. Unit-tested. */

/* The file starts and ends with this; an encrypted footer ends with "PARE" */
#define PARQUET_MAGIC "PAR1"
#define PARQUET_MAGIC_ENCRYPTED "PARE"

typedef enum {
    PARQUET_BOOLEAN = 0,
    PARQUET_INT32 = 1,
    PARQUET_INT64 = 2,
    PARQUET_INT96 = 3,
    PARQUET_FLOAT = 4,
    PARQUET_DOUBLE = 5,
    PARQUET_BYTE_ARRAY = 6,
    PARQUET_FIXED_LEN_BYTE_ARRAY = 7
} ParquetType;

/* LogicalType (the union's field ids) */
typedef enum {
    PARQUET_LOGICAL_NONE = 0,
    PARQUET_LOGICAL_STRING = 1,
    PARQUET_LOGICAL_MAP = 2,
    PARQUET_LOGICAL_LIST = 3,
    PARQUET_LOGICAL_ENUM = 4,
    PARQUET_LOGICAL_DECIMAL = 5,
    PARQUET_LOGICAL_DATE = 6,
    PARQUET_LOGICAL_TIME = 7,
    PARQUET_LOGICAL_TIMESTAMP = 8,
    PARQUET_LOGICAL_INTEGER = 10,
    PARQUET_LOGICAL_UNKNOWN = 11,
    PARQUET_LOGICAL_JSON = 12,
    PARQUET_LOGICAL_BSON = 13,
    PARQUET_LOGICAL_UUID = 14,
    PARQUET_LOGICAL_FLOAT16 = 15,
    PARQUET_LOGICAL_VARIANT = 16,
    PARQUET_LOGICAL_GEOMETRY = 17,
    PARQUET_LOGICAL_GEOGRAPHY = 18
} ParquetLogical;

typedef enum { PARQUET_MILLIS = 0, PARQUET_MICROS = 1, PARQUET_NANOS = 2 } ParquetTimeUnit;

typedef enum { PARQUET_REQUIRED = 0, PARQUET_OPTIONAL = 1, PARQUET_REPEATED = 2 } ParquetRepetition;

/* Legacy ConvertedType values used when there is no LogicalType */
enum {
    PARQUET_CONVERTED_UTF8 = 0, PARQUET_CONVERTED_MAP = 1, PARQUET_CONVERTED_LIST = 3,
    PARQUET_CONVERTED_ENUM = 4, PARQUET_CONVERTED_DECIMAL = 5, PARQUET_CONVERTED_DATE = 6,
    PARQUET_CONVERTED_TIME_MILLIS = 7, PARQUET_CONVERTED_TIME_MICROS = 8,
    PARQUET_CONVERTED_TIMESTAMP_MILLIS = 9, PARQUET_CONVERTED_TIMESTAMP_MICROS = 10,
    PARQUET_CONVERTED_UINT_8 = 11, PARQUET_CONVERTED_UINT_64 = 14,
    PARQUET_CONVERTED_INT_8 = 15, PARQUET_CONVERTED_INT_64 = 18,
    PARQUET_CONVERTED_JSON = 19, PARQUET_CONVERTED_BSON = 20, PARQUET_CONVERTED_INTERVAL = 21
};

/* Compression codecs */
enum {
    PARQUET_UNCOMPRESSED = 0, PARQUET_SNAPPY = 1, PARQUET_GZIP = 2, PARQUET_LZO = 3,
    PARQUET_BROTLI = 4, PARQUET_LZ4 = 5, PARQUET_ZSTD = 6, PARQUET_LZ4_RAW = 7
};

/* Encodings */
enum {
    PARQUET_PLAIN = 0, PARQUET_PLAIN_DICTIONARY = 2, PARQUET_RLE = 3, PARQUET_BIT_PACKED = 4,
    PARQUET_DELTA_BINARY_PACKED = 5, PARQUET_DELTA_LENGTH_BYTE_ARRAY = 6,
    PARQUET_DELTA_BYTE_ARRAY = 7, PARQUET_RLE_DICTIONARY = 8, PARQUET_BYTE_STREAM_SPLIT = 9,
    PARQUET_ENCODING_COUNT = 10
};

/* A node of the schema (depth-first order, the root first) */
typedef struct {
    gchar* name;
    gint type;               /* ParquetType, -1 for a group */
    gint type_length;        /* FIXED_LEN_BYTE_ARRAY width */
    gint repetition;         /* ParquetRepetition (the root: REQUIRED) */
    gint num_children;
    gint converted;          /* ConvertedType, -1 if none */
    gint scale, precision;   /* DECIMAL */
    ParquetLogical logical;
    ParquetTimeUnit unit;    /* TIME, TIMESTAMP */
    gboolean utc;            /* TIME, TIMESTAMP: adjusted to UTC */
    gint int_bits;           /* INTEGER: 8, 16, 32, 64 */
    gboolean int_signed;
} ParquetNode;

/* Repetition levels described in a leaf (deeper ones are shown as "…") */
#define PARQUET_MAX_REP 8

/* A leaf column: a primitive node with its path from the root */
typedef struct {
    gchar* path;             /* "tags.list.element" */
    gchar* label;            /* name to show: the wrapper levels of lists and maps dropped ("tags", "attrs.key") */
    guint node;              /* index in schema */
    gint max_def;            /* definition level of a present value */
    gint max_rep;            /* 0: one value per row */
    /* For each repetition level: the definition level of a present element,
     * and whether it is the key_value level of a MAP (else a list) */
    gint rep_def[PARQUET_MAX_REP];
    gboolean rep_map[PARQUET_MAX_REP];
} ParquetLeaf;

/* A column chunk of a row group (its ColumnMetaData) */
typedef struct {
    gint type;
    gint codec;
    guint encodings;         /* bit (1 << encoding) for each encoding used */
    gint64 num_values;
    gint64 compressed_size;
    gint64 uncompressed_size;
    gint64 data_page_offset;
    gint64 dictionary_page_offset;  /* 0 or less: none */
    gboolean external;       /* in another file (file_path), or no metadata (encrypted) */
} ParquetChunk;

typedef struct {
    gint64 num_rows;
    gint64 total_byte_size;  /* uncompressed */
    gint64 compressed_size;  /* sum of its chunks */
    guint n_chunks;
    ParquetChunk* chunks;    /* one per leaf, in leaf order */
} ParquetRowGroup;

typedef struct {
    gint version;
    gint64 num_rows;
    gchar* created_by;       /* may be NULL */
    guint n_nodes;
    ParquetNode* nodes;
    guint n_leaves;
    ParquetLeaf* leaves;
    guint n_row_groups;
    ParquetRowGroup* row_groups;
    guint n_keys;
    gchar** keys;            /* key_value_metadata */
    gchar** values;          /* values[i] may be NULL */
} ParquetMeta;

/* Parse the FileMetaData (the footer bytes before its length and magic).
 * NULL with error set if it is not valid. */
ParquetMeta* parquet_meta_parse(const guchar* data, gsize len, GError** error);
void parquet_meta_free(ParquetMeta* meta);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ParquetMeta, parquet_meta_free)

/* Page types */
enum { PARQUET_PAGE_DATA = 0, PARQUET_PAGE_INDEX = 1, PARQUET_PAGE_DICTIONARY = 2, PARQUET_PAGE_DATA_V2 = 3 };

typedef struct {
    gint type;
    gint32 uncompressed_size;
    gint32 compressed_size;
    gint32 num_values;       /* level entries (data), values (dictionary) */
    gint encoding;
    gint def_encoding;       /* DATA_PAGE */
    gint rep_encoding;
    gint32 num_nulls;        /* DATA_PAGE_V2 */
    gint32 num_rows;
    gint32 def_length;       /* DATA_PAGE_V2: level bytes, never compressed */
    gint32 rep_length;
    gboolean is_compressed;  /* DATA_PAGE_V2 */
} ParquetPageHeader;

/* Parse a page header at the start of data; the number of bytes it takes,
 * or 0 if data is too short or not a page header */
gsize parquet_page_header_parse(const guchar* data, gsize len, ParquetPageHeader* header);

/* "SNAPPY", "ZSTD"... (static) */
const gchar* parquet_codec_name(gint codec);
/* "PLAIN", "RLE_DICTIONARY"... (static) */
const gchar* parquet_encoding_name(gint encoding);
/* Type of a leaf as a reader names it: "string", "timestamp[us, UTC]",
 * "decimal(10, 2)", "list<string>"... Caller frees. */
gchar* parquet_leaf_type(const ParquetMeta* meta, const ParquetLeaf* leaf);

#endif /* PARQUET_META_H */
