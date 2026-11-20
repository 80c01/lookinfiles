/* Unit tests for src/formats/parquet_meta.c, parquet_pages.c and
 * parquet_view.c (Apache Parquet preview). The fixtures are written by
 * tests/fixtures/parquet/make_fixtures.py. */
#include "formats/parquet_meta.h"
#include "formats/parquet_pages.h"
#include "formats/parquet_view.h"

#include <string.h>

#define TRIPS "tests/fixtures/preview/trips.parquet"

static guchar* load(const gchar* path, gsize* len) {
    gchar* data = NULL;
    g_assert_true(g_file_get_contents(path, &data, len, NULL));
    return (guchar*)data;
}

/* The footer of a file in memory */
static ParquetMeta* parse_footer(const guchar* data, gsize len) {
    g_assert_cmpuint(len, >, 12);
    g_assert_cmpmem(data + len - 4, 4, PARQUET_MAGIC, 4);
    guint32 n = (guint32)data[len - 8] | ((guint32)data[len - 7] << 8) | ((guint32)data[len - 6] << 16) |
                ((guint32)data[len - 5] << 24);
    GError* error = NULL;
    ParquetMeta* meta = parquet_meta_parse(data + len - 8 - n, n, &error);
    g_assert_no_error(error);
    g_assert_nonnull(meta);
    return meta;
}

typedef struct {
    const guchar* data;
    gsize len;
} Bytes;

static gboolean read_bytes(gpointer source, guint64 offset, guchar* buf, gsize len) {
    Bytes* b = source;
    if (offset > b->len || len > b->len - offset) return FALSE;
    memcpy(buf, b->data + offset, len);
    return TRUE;
}

/* Cells of the first rows of every column, "|"-separated per row */
static gchar* read_rows(const gchar* path, guint rows, GError** error) {
    gsize len;
    g_autofree guchar* data = load(path, &len);
    g_autoptr(ParquetMeta) meta = parse_footer(data, len);
    Bytes b = { data, len };
    GPtrArray** cols = g_new0(GPtrArray*, meta->n_leaves);
    gboolean ok = TRUE;
    for (guint c = 0; c < meta->n_leaves; c++) {
        cols[c] = g_ptr_array_new_with_free_func(g_free);
        if (ok) ok = parquet_column_read(meta, c, read_bytes, &b, len, rows, cols[c], error);
    }
    GString* s = g_string_new(NULL);
    for (guint r = 0; ok && r < rows; r++) {
        for (guint c = 0; c < meta->n_leaves; c++) {
            const gchar* cell = r < cols[c]->len ? g_ptr_array_index(cols[c], r) : NULL;
            g_string_append_printf(s, "%s%s", c > 0 ? "|" : "", cell != NULL ? cell : "null");
        }
        g_string_append_c(s, '\n');
    }
    for (guint c = 0; c < meta->n_leaves; c++) g_ptr_array_unref(cols[c]);
    g_free(cols);
    return g_string_free(s, !ok);
}

static void test_meta(void) {
    gsize len;
    g_autofree guchar* data = load(TRIPS, &len);
    g_autoptr(ParquetMeta) meta = parse_footer(data, len);
    g_assert_cmpint(meta->num_rows, ==, 250);
    g_assert_cmpuint(meta->n_row_groups, ==, 2);
    g_assert_cmpint(meta->row_groups[0].num_rows, ==, 150);
    g_assert_cmpint(meta->row_groups[1].num_rows, ==, 100);
    g_assert_true(g_str_has_prefix(meta->created_by, "parquet-cpp-arrow"));
    g_assert_cmpuint(meta->n_leaves, ==, 9);
    const gchar* labels[] = { "id", "city", "fare", "paid", "pickup", "day", "tip", "tags", "seats" };
    const gchar* types[] = { "int64", "string", "double", "bool", "timestamp[us, UTC]", "date",
                             "decimal(10, 2)", "list<string>", "uint8" };
    for (guint i = 0; i < 9; i++) {
        g_assert_cmpstr(meta->leaves[i].label, ==, labels[i]);
        g_autofree gchar* type = parquet_leaf_type(meta, &meta->leaves[i]);
        g_assert_cmpstr(type, ==, types[i]);
    }
    /* tags (optional LIST) > list (repeated) > element (optional) */
    const ParquetLeaf* tags = &meta->leaves[7];
    g_assert_cmpstr(tags->path, ==, "tags.list.element");
    g_assert_cmpint(tags->max_rep, ==, 1);
    g_assert_cmpint(tags->max_def, ==, 3);
    g_assert_cmpint(tags->rep_def[0], ==, 2);
    g_assert_false(tags->rep_map[0]);
    g_assert_cmpint(meta->row_groups[0].chunks[0].codec, ==, PARQUET_SNAPPY);
    g_assert_true(meta->row_groups[0].chunks[1].encodings & (1u << PARQUET_RLE_DICTIONARY));
    g_assert_cmpuint(meta->n_keys, ==, 2);

    GError* error = NULL;
    g_assert_null(parquet_meta_parse((const guchar*)"\x15\x04", 2, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_assert_null(parquet_meta_parse(NULL, 0, NULL));
}

static void test_page_header(void) {
    /* type 0, sizes 10 / 8, DataPageHeader { num_values 3, PLAIN, RLE, RLE } */
    static const guchar header[] = { 0x15, 0x00, 0x15, 0x14, 0x15, 0x10, 0x2C, 0x15, 0x06, 0x15, 0x00,
                                     0x15, 0x06, 0x15, 0x06, 0x00, 0x00 };
    ParquetPageHeader h;
    g_assert_cmpuint(parquet_page_header_parse(header, sizeof(header), &h), ==, sizeof(header));
    g_assert_cmpint(h.type, ==, PARQUET_PAGE_DATA);
    g_assert_cmpint(h.uncompressed_size, ==, 10);
    g_assert_cmpint(h.compressed_size, ==, 8);
    g_assert_cmpint(h.num_values, ==, 3);
    g_assert_cmpint(h.encoding, ==, PARQUET_PLAIN);
    /* Cut anywhere: not a header */
    for (gsize n = 0; n < sizeof(header); n++) g_assert_cmpuint(parquet_page_header_parse(header, n, &h), ==, 0);
}

static void test_rows(void) {
    GError* error = NULL;
    g_autofree gchar* rows = read_rows(TRIPS, 5, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(rows, ==,
        "1|Lyon|7.5|false|2026-03-01 08:30:00|2026-03-01|0.00|[]|1\n"
        "2|Paris|7.75|true|2026-03-01 08:47:00|2026-03-01|0.01|[night, card]|2\n"
        "3|Nantes|8|true|2026-03-01 09:04:00|2026-03-01|0.02|[night, night, card]|3\n"
        "4|Lille|null|false|2026-03-01 09:21:00|2026-03-01|0.03|[card]|4\n"
        "5|Lyon|8.5|true|2026-03-01 09:38:00|2026-03-01|0.04|[night, card]|1\n");
}

/* Rows read across the two row groups (150 + 100) */
static void test_row_groups(void) {
    gsize len;
    g_autofree guchar* data = load(TRIPS, &len);
    g_autoptr(ParquetMeta) meta = parse_footer(data, len);
    Bytes b = { data, len };
    g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func(g_free);
    g_assert_true(parquet_column_read(meta, 0, read_bytes, &b, len, 400, ids, NULL));
    g_assert_cmpuint(ids->len, ==, 250);
    g_assert_cmpstr(g_ptr_array_index(ids, 150), ==, "151");
    g_assert_cmpstr(g_ptr_array_index(ids, 249), ==, "250");
}

/* Every codec and the v2 pages with DELTA_* / BYTE_STREAM_SPLIT give the
 * same rows as the uncompressed file */
static void test_codecs_and_encodings(void) {
    GError* error = NULL;
    g_autofree gchar* plain = read_rows("tests/fixtures/parquet/codec-none.parquet", 20, &error);
    g_assert_no_error(error);
    g_assert_nonnull(strstr(plain, "\n20|Lille|12.25|true|2026-03-01 13:53:00|2026-03-02|0.19|[night, card]|4\n"));
    const gchar* others[] = { "codec-gzip", "codec-lz4", "codec-zstd", "v2-delta" };
    for (guint i = 0; i < G_N_ELEMENTS(others); i++) {
        g_autofree gchar* path = g_strdup_printf("tests/fixtures/parquet/%s.parquet", others[i]);
        g_autofree gchar* rows = read_rows(path, 20, &error);
#ifndef HAVE_ZSTD
        if (strcmp(others[i], "codec-zstd") == 0) {
            g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
            g_assert_cmpstr(error->message, ==, "ZSTD compression is not supported");
            g_clear_error(&error);
            continue;
        }
#endif
        g_assert_no_error(error);
        g_assert_cmpstr(rows, ==, plain);
    }
}

static void test_decompress(void) {
    /* Snappy: length 9, literal "abc", copy of 6 bytes at offset 3 (overlapping) */
    static const guchar snappy[] = { 9, 0x08, 'a', 'b', 'c', 0x09, 0x03 };
    guchar out[16];
    g_assert_true(parquet_decompress(PARQUET_SNAPPY, snappy, sizeof(snappy), out, 9, NULL));
    g_assert_cmpmem(out, 9, "abcabcabc", 9);
    GError* error = NULL;
    g_assert_false(parquet_decompress(PARQUET_SNAPPY, snappy, sizeof(snappy), out, 8, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    /* LZ4 block: 3 literals, match of 4 at offset 3, then 2 literals */
    static const guchar lz4[] = { 0x30, 'x', 'y', 'z', 0x03, 0x00, 0x20, '!', '?' };
    g_assert_true(parquet_decompress(PARQUET_LZ4_RAW, lz4, sizeof(lz4), out, 9, NULL));
    g_assert_cmpmem(out, 9, "xyzxyzx!?", 9);
    g_assert_false(parquet_decompress(PARQUET_LZ4_RAW, lz4, 5, out, 9, NULL));
    g_assert_false(parquet_decompress(PARQUET_BROTLI, lz4, sizeof(lz4), out, 9, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_clear_error(&error);
    g_assert_false(parquet_codec_supported(PARQUET_BROTLI));
    g_assert_true(parquet_codec_supported(PARQUET_SNAPPY));
}

static void check_value(const ParquetNode* node, const void* bytes, gsize len, const gchar* expected) {
    g_autofree gchar* text = parquet_format_value(node, bytes, len);
    g_assert_cmpstr(text, ==, expected);
}

static void test_format_value(void) {
    ParquetNode n = { .type = PARQUET_INT32, .logical = PARQUET_LOGICAL_DECIMAL, .scale = 2 };
    gint32 i32 = -5;
    check_value(&n, &i32, 4, "-0.05");
    i32 = 12345;
    check_value(&n, &i32, 4, "123.45");
    n.logical = PARQUET_LOGICAL_DATE;
    i32 = 20513;
    check_value(&n, &i32, 4, "2026-03-01");
    n.logical = PARQUET_LOGICAL_INTEGER;
    n.int_signed = FALSE;
    i32 = -1;
    check_value(&n, &i32, 4, "4294967295");

    ParquetNode t = { .type = PARQUET_INT64, .logical = PARQUET_LOGICAL_TIMESTAMP, .unit = PARQUET_MILLIS };
    gint64 i64 = G_GINT64_CONSTANT(1772353800250);
    check_value(&t, &i64, 8, "2026-03-01 08:30:00.250");
    i64 = -1;
    check_value(&t, &i64, 8, "1969-12-31 23:59:59.999");
    t.logical = PARQUET_LOGICAL_TIME;
    t.unit = PARQUET_MICROS;
    i64 = G_GINT64_CONSTANT(3723000004);
    check_value(&t, &i64, 8, "01:02:03.000004");

    ParquetNode d = { .type = PARQUET_DOUBLE };
    gdouble dv = 0.1;
    check_value(&d, &dv, 8, "0.1");
    dv = 1.0 / 3.0;
    check_value(&d, &dv, 8, "0.3333333333333333");
    ParquetNode f = { .type = PARQUET_FLOAT };
    gfloat fv = 0.1f;
    check_value(&f, &fv, 4, "0.1");

    ParquetNode b = { .type = PARQUET_BYTE_ARRAY };
    check_value(&b, "tab\there", 8, "tab here");
    check_value(&b, "\x00\x01\xFF", 3, "0x0001ff");
    ParquetNode u = { .type = PARQUET_FIXED_LEN_BYTE_ARRAY, .type_length = 16, .logical = PARQUET_LOGICAL_UUID };
    check_value(&u, "\x12\x34\x56\x78\x9a\xbc\xde\xf0\x01\x23\x45\x67\x89\xab\xcd\xef", 16,
                "12345678-9abc-def0-0123-456789abcdef");
    ParquetNode h = { .type = PARQUET_FIXED_LEN_BYTE_ARRAY, .type_length = 2, .logical = PARQUET_LOGICAL_FLOAT16 };
    check_value(&h, "\x00\x3C", 2, "1");
    check_value(&h, "\x00\xC1", 2, "-2.5");
    ParquetNode dec = { .type = PARQUET_FIXED_LEN_BYTE_ARRAY, .type_length = 5, .logical = PARQUET_LOGICAL_DECIMAL,
                        .scale = 3 };
    check_value(&dec, "\xFF\xFF\xFF\xFF\x85", 5, "-0.123");
    check_value(&dec, "\x00\x00\x01\x00\x00", 5, "65.536");

    /* INT96: nanoseconds of the day, then the Julian day of 2026-03-01 */
    ParquetNode i96 = { .type = PARQUET_INT96 };
    guchar ts[12] = { 0 };
    gint64 nanos = G_GINT64_CONSTANT(30600) * 1000000000;
    for (guint i = 0; i < 8; i++) ts[i] = (guchar)(nanos >> (8 * i));
    guint32 julian = 2440588 + 20513;
    for (guint i = 0; i < 4; i++) ts[8 + i] = (guchar)(julian >> (8 * i));
    check_value(&i96, ts, 12, "2026-03-01 08:30:00");
}

static void test_view(void) {
    gsize len;
    g_autofree guchar* data = load(TRIPS, &len);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    g_assert_true(parquet_view_append_bytes(doc, data, len, &error));
    g_assert_no_error(error);
    const gchar* out = doc->text->str;
    g_assert_true(g_str_has_prefix(out, "250 rows × 9 columns · 2 row groups · SNAPPY\n"));
    g_assert_nonnull(strstr(out, "│ id  │ city   │ fare  │ paid  │ pickup"));
    g_assert_nonnull(strstr(out, "First 100 of 250 rows."));
    g_assert_nonnull(strstr(out, "tags   │ list<string>       │ optional   │ PLAIN, RLE, RLE_DICTIONARY │ SNAPPY"));
    g_assert_nonnull(strstr(out, "Created by     parquet-cpp-arrow"));
    g_assert_nonnull(strstr(out, "source       │ LookinFiles fixtures"));
    g_assert_nonnull(strstr(out, "ARROW:schema │ Arrow schema ("));
    g_assert_null(strstr(out, "Column city"));  /* no column failed */

    g_autoptr(PreviewDoc) file_doc = preview_doc_new();
    g_assert_true(parquet_view_append_file(file_doc, TRIPS, NULL, &error));
    g_assert_cmpstr(file_doc->text->str, ==, out);
}

/* A footer encrypted with PARE: only that is said */
static void test_encrypted(void) {
    static const guchar data[] = "PARE\x01\x02\x03\x04\x05\x06\x07\x08\x04\0\0\0PARE";
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(parquet_view_append_bytes(doc, data, sizeof(data) - 1, NULL));
    g_assert_nonnull(strstr(doc->text->str, "Encrypted footer"));
}

static void test_invalid(void) {
    GError* error = NULL;
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_false(parquet_view_append_bytes(doc, (const guchar*)"hello world, not parquet", 24, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_assert_false(parquet_view_append_bytes(doc, NULL, 0, NULL));
    g_assert_false(parquet_view_append_file(doc, "tests/fixtures/parquet/missing.parquet", NULL, &error));
    g_clear_error(&error);

    /* Truncated anywhere: the footer is gone */
    gsize len;
    g_autofree guchar* data = load("tests/fixtures/parquet/codec-none.parquet", &len);
    for (gsize n = 0; n < len; n += 7) {
        g_autoptr(PreviewDoc) cut = preview_doc_new();
        g_assert_false(parquet_view_append_bytes(cut, data, n, NULL));
    }
    /* A damaged page: the column says so, the others are shown */
    gsize page = 4 + 2;  /* inside the first page header of the first column */
    g_autofree guchar* copy = g_memdup2(data, len);
    copy[page] = 0xFF;
    copy[page + 1] = 0xFF;
    g_autoptr(PreviewDoc) damaged = preview_doc_new();
    g_assert_true(parquet_view_append_bytes(damaged, copy, len, NULL));
    g_assert_nonnull(strstr(damaged->text->str, "Column id: corrupt page."));
    g_assert_nonnull(strstr(damaged->text->str, "│ Lyon"));
    /* Random damage never crashes */
    GRand* rand = g_rand_new_with_seed(7);
    for (guint i = 0; i < 300; i++) {
        memcpy(copy, data, len);
        for (guint k = 0; k < 4; k++) copy[g_rand_int_range(rand, 0, (gint32)len)] = (guchar)g_rand_int(rand);
        g_autoptr(PreviewDoc) d = preview_doc_new();
        parquet_view_append_bytes(d, copy, len, NULL);
    }
    g_rand_free(rand);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/parquet/meta", test_meta);
    g_test_add_func("/parquet/page_header", test_page_header);
    g_test_add_func("/parquet/rows", test_rows);
    g_test_add_func("/parquet/row_groups", test_row_groups);
    g_test_add_func("/parquet/codecs_and_encodings", test_codecs_and_encodings);
    g_test_add_func("/parquet/decompress", test_decompress);
    g_test_add_func("/parquet/format_value", test_format_value);
    g_test_add_func("/parquet/view", test_view);
    g_test_add_func("/parquet/encrypted", test_encrypted);
    g_test_add_func("/parquet/invalid", test_invalid);
    return g_test_run();
}
