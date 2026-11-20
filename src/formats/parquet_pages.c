/* Apache Parquet pages: decompression, levels, values (see parquet_pages.h). */
#include "formats/parquet_pages.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <math.h>
#include <string.h>

#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

/* Level entries decoded from one page (a repeated column decodes them all) */
#define LEVELS_MAX (4 * 1024 * 1024)
/* Characters kept of a cell: the table cuts them shorter */
#define CELL_MAX_CHARS 200
/* A page header larger than this is not one */
#define HEADER_MAX (1024 * 1024)

static guint32 le32(const guchar* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static guint64 le64(const guchar* p) {
    return (guint64)le32(p) | ((guint64)le32(p + 4) << 32);
}

static gboolean corrupt(GError** error) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("corrupt page"));
    return FALSE;
}

/* --- Decompression --- */

static gboolean snappy_decompress(const guchar* in, gsize in_len, guchar* out, gsize out_len) {
    const guchar* ip = in;
    const guchar* end = in + in_len;
    guint64 ulen = 0;
    for (guint shift = 0;; shift += 7) {
        if (ip >= end || shift > 35) return FALSE;
        guchar b = *ip++;
        ulen |= (guint64)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
    }
    if (ulen != out_len) return FALSE;
    gsize op = 0;
    while (ip < end) {
        guchar tag = *ip++;
        gsize len, off;
        if ((tag & 3) == 0) {
            len = (tag >> 2) + 1;
            if (len > 60) {
                gsize nb = len - 60;
                if ((gsize)(end - ip) < nb) return FALSE;
                len = 0;
                for (gsize i = 0; i < nb; i++) len |= (gsize)ip[i] << (8 * i);
                len++;
                ip += nb;
            }
            if ((gsize)(end - ip) < len || out_len - op < len) return FALSE;
            memcpy(out + op, ip, len);
            ip += len;
            op += len;
            continue;
        }
        if ((tag & 3) == 1) {
            if (ip >= end) return FALSE;
            len = ((tag >> 2) & 7) + 4;
            off = ((gsize)(tag >> 5) << 8) | *ip++;
        } else if ((tag & 3) == 2) {
            if (end - ip < 2) return FALSE;
            len = (tag >> 2) + 1;
            off = (gsize)ip[0] | ((gsize)ip[1] << 8);
            ip += 2;
        } else {
            if (end - ip < 4) return FALSE;
            len = (tag >> 2) + 1;
            off = le32(ip);
            ip += 4;
        }
        if (off == 0 || off > op || out_len - op < len) return FALSE;
        for (gsize i = 0; i < len; i++, op++) out[op] = out[op - off];  /* copies may overlap */
    }
    return op == out_len;
}

/* One LZ4 block; *out_len: bytes written */
static gboolean lz4_block(const guchar* in, gsize in_len, guchar* out, gsize out_cap, gsize* out_len) {
    const guchar* ip = in;
    const guchar* end = in + in_len;
    gsize op = 0;
    while (ip < end) {
        guchar token = *ip++;
        gsize lit = token >> 4;
        if (lit == 15) {
            guchar b;
            do {
                if (ip >= end) return FALSE;
                b = *ip++;
                lit += b;
            } while (b == 255);
        }
        if ((gsize)(end - ip) < lit || out_cap - op < lit) return FALSE;
        memcpy(out + op, ip, lit);
        ip += lit;
        op += lit;
        if (ip == end) break;  /* the last sequence has only literals */
        if (end - ip < 2) return FALSE;
        gsize off = (gsize)ip[0] | ((gsize)ip[1] << 8);
        ip += 2;
        gsize match = token & 15;
        if (match == 15) {
            guchar b;
            do {
                if (ip >= end) return FALSE;
                b = *ip++;
                match += b;
            } while (b == 255);
        }
        match += 4;
        if (off == 0 || off > op || out_cap - op < match) return FALSE;
        for (gsize i = 0; i < match; i++, op++) out[op] = out[op - off];
    }
    *out_len = op;
    return TRUE;
}

/* The old LZ4 codec: Hadoop frames (big-endian sizes before each block),
 * or a bare block from writers that did not frame it */
static gboolean lz4_hadoop(const guchar* in, gsize in_len, guchar* out, gsize out_len) {
    const guchar* ip = in;
    gsize op = 0;
    while (in_len - (gsize)(ip - in) >= 8 && op < out_len) {
        guint32 ulen = ((guint32)ip[0] << 24) | ((guint32)ip[1] << 16) | ((guint32)ip[2] << 8) | ip[3];
        guint32 clen = ((guint32)ip[4] << 24) | ((guint32)ip[5] << 16) | ((guint32)ip[6] << 8) | ip[7];
        ip += 8;
        gsize got = 0;
        if (clen > in_len - (gsize)(ip - in) || ulen > out_len - op ||
            !lz4_block(ip, clen, out + op, ulen, &got) || got != ulen) {
            op = G_MAXSIZE;
            break;
        }
        ip += clen;
        op += got;
    }
    if (op == out_len && ip == in + in_len) return TRUE;
    gsize got = 0;
    return lz4_block(in, in_len, out, out_len, &got) && got == out_len;
}

static gboolean gzip_decompress(const guchar* in, gsize in_len, guchar* out, gsize out_len) {
    GConverter* conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP));
    gsize in_pos = 0, out_pos = 0;
    gboolean ok = FALSE;
    guchar spare;  /* output once the page is full: anything written there is too much */
    while (TRUE) {
        gsize read = 0, written = 0;
        gboolean full = out_pos == out_len;
        GConverterResult res = g_converter_convert(conv, in + in_pos, in_len - in_pos, full ? &spare : out + out_pos,
                                                   full ? 1 : out_len - out_pos, G_CONVERTER_INPUT_AT_END, &read,
                                                   &written, NULL);
        in_pos += read;
        if (full && written > 0) break;
        out_pos += written;
        if (res == G_CONVERTER_FINISHED) {
            ok = out_pos == out_len;
            break;
        }
        if (res == G_CONVERTER_ERROR || (read == 0 && written == 0)) break;
    }
    g_object_unref(conv);
    return ok;
}

gboolean parquet_codec_supported(gint codec) {
    switch (codec) {
    case PARQUET_UNCOMPRESSED: case PARQUET_SNAPPY: case PARQUET_GZIP: case PARQUET_LZ4: case PARQUET_LZ4_RAW:
        return TRUE;
#ifdef HAVE_ZSTD
    case PARQUET_ZSTD:
        return TRUE;
#endif
    default:
        return FALSE;
    }
}

gboolean parquet_decompress(gint codec, const guchar* in, gsize in_len, guchar* out, gsize out_len,
                            GError** error) {
    gboolean ok = FALSE;
    gsize got = 0;
    switch (codec) {
    case PARQUET_UNCOMPRESSED:
        ok = in_len == out_len;
        if (ok && out_len > 0) memcpy(out, in, out_len);
        break;
    case PARQUET_SNAPPY: ok = snappy_decompress(in, in_len, out, out_len); break;
    case PARQUET_GZIP: ok = gzip_decompress(in, in_len, out, out_len); break;
    case PARQUET_LZ4: ok = lz4_hadoop(in, in_len, out, out_len); break;
    case PARQUET_LZ4_RAW: ok = lz4_block(in, in_len, out, out_len, &got) && got == out_len; break;
#ifdef HAVE_ZSTD
    case PARQUET_ZSTD: {
        size_t n = ZSTD_decompress(out, out_len, in, in_len);
        ok = !ZSTD_isError(n) && n == out_len;
        break;
    }
#endif
    default:
        /* TRANSLATORS: %s is a compression codec, e.g. "BROTLI" */
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("%s compression is not supported"),
                    parquet_codec_name(codec));
        return FALSE;
    }
    if (!ok) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("corrupt %s data"), parquet_codec_name(codec));
    }
    return ok;
}

/* --- Bit-level decoding --- */

static guint bit_width(guint64 max) {
    guint bits = 0;
    while (max != 0) {
        bits++;
        max >>= 1;
    }
    return bits;
}

/* Value i of width bits packed LSB first; the caller checks the bounds */
static guint64 unpack(const guchar* p, guint64 i, guint width) {
    guint64 bit = i * width;
    guint64 v = 0;
    for (guint got = 0; got < width;) {
        guint shift = (guint)(bit & 7);
        guint take = MIN(8 - shift, width - got);
        v |= (guint64)((p[bit >> 3] >> shift) & ((1u << take) - 1)) << got;
        got += take;
        bit += take;
    }
    return v;
}

static gboolean read_uleb(const guchar** p, const guchar* end, guint64* v) {
    *v = 0;
    for (guint shift = 0; shift < 64; shift += 7) {
        if (*p >= end) return FALSE;
        guchar b = *(*p)++;
        *v |= (guint64)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return TRUE;
    }
    return FALSE;
}

static gboolean read_zigzag(const guchar** p, const guchar* end, gint64* v) {
    guint64 u;
    if (!read_uleb(p, end, &u)) return FALSE;
    *v = (gint64)(u >> 1) ^ -(gint64)(u & 1);
    return TRUE;
}

/* RLE / bit-packed hybrid: up to n values of width bits; the count decoded */
static gsize hybrid_decode(const guchar* p, gsize len, guint width, guint32* out, gsize n) {
    const guchar* end = p + len;
    gsize count = 0;
    if (width > 32) return 0;
    while (count < n && p < end) {
        guint64 header;
        if (!read_uleb(&p, end, &header)) break;
        if (header & 1) {
            guint64 values = (header >> 1) * 8;
            guint64 bytes = (header >> 1) * width;
            if (bytes > (guint64)(end - p)) {
                /* A last group cut short: keep the whole values it holds */
                values = width > 0 ? (guint64)(end - p) * 8 / width : values;
                bytes = (guint64)(end - p);
            }
            for (guint64 i = 0; i < values && count < n; i++) out[count++] = width > 0 ? (guint32)unpack(p, i, width) : 0;
            p += bytes;
        } else {
            guint64 run = header >> 1;
            guint nb = (width + 7) / 8;
            if ((gsize)(end - p) < nb) break;
            guint32 v = 0;
            for (guint i = 0; i < nb; i++) v |= (guint32)p[i] << (8 * i);
            p += nb;
            for (guint64 i = 0; i < run && count < n; i++) out[count++] = v;
        }
    }
    return count;
}

/* DELTA_BINARY_PACKED: the first max_out values in out, *total the values
 * of the section; the end of the section, NULL if it is corrupt */
static const guchar* delta_decode(const guchar* p, const guchar* end, gint64* out, gsize max_out, gsize* total) {
    guint64 block, minis, count;
    gint64 value;
    if (!read_uleb(&p, end, &block) || !read_uleb(&p, end, &minis) || !read_uleb(&p, end, &count) ||
        !read_zigzag(&p, end, &value) || block == 0 || minis == 0 || block % minis != 0 ||
        (block / minis) % 8 != 0 || count > (guint64)G_MAXINT32) {
        return NULL;
    }
    guint64 per_mini = block / minis;
    gsize got = 0;
    if (count > 0) {
        if (max_out > 0) out[0] = value;
        got = 1;
    }
    while (got < count) {
        gint64 min_delta;
        if (!read_zigzag(&p, end, &min_delta) || (guint64)(end - p) < minis) return NULL;
        const guchar* widths = p;
        p += minis;
        for (guint64 m = 0; m < minis && got < count; m++) {
            guint w = widths[m];
            if (w > 64) return NULL;
            guint64 bytes = per_mini * w / 8;
            guint64 n = MIN(per_mini, count - got);
            /* The last miniblock may be cut after its last value */
            if (bytes > (guint64)(end - p) && (n * w + 7) / 8 > (guint64)(end - p)) return NULL;
            for (guint64 j = 0; j < n; j++, got++) {
                if (got >= max_out) {
                    got += n - j;
                    break;
                }
                value = (gint64)((guint64)value + (guint64)min_delta + unpack(p, j, w));
                out[got] = value;
            }
            p += MIN(bytes, (guint64)(end - p));
        }
    }
    *total = (gsize)count;
    return p;
}

/* --- Values --- */

/* Decoded values: bytes in the PLAIN layout (see parquet_format_value) */
typedef struct {
    GByteArray* data;
    GArray* spans;   /* guint32 start, length pairs */
} Values;

static void values_init(Values* v) {
    v->data = g_byte_array_new();
    v->spans = g_array_new(FALSE, FALSE, sizeof(guint32));
}

static void values_clear(Values* v) {
    if (v->data != NULL) g_byte_array_unref(v->data);
    if (v->spans != NULL) g_array_unref(v->spans);
    v->data = NULL;
    v->spans = NULL;
}

static guint values_len(const Values* v) {
    return v->spans->len / 2;
}

static void values_add(Values* v, const guchar* bytes, gsize len) {
    guint32 span[2] = { v->data->len, (guint32)len };
    g_byte_array_append(v->data, bytes, (guint)len);
    g_array_append_vals(v->spans, span, 2);
}

static const guchar* values_get(const Values* v, guint i, gsize* len) {
    guint32 start = g_array_index(v->spans, guint32, 2 * i);
    *len = g_array_index(v->spans, guint32, 2 * i + 1);
    return v->data->data + start;
}

/* Bytes of a fixed-width value, 0 for BYTE_ARRAY */
static gsize type_width(const ParquetNode* node) {
    switch (node->type) {
    case PARQUET_BOOLEAN: return 1;
    case PARQUET_INT32: case PARQUET_FLOAT: return 4;
    case PARQUET_INT64: case PARQUET_DOUBLE: return 8;
    case PARQUET_INT96: return 12;
    case PARQUET_FIXED_LEN_BYTE_ARRAY: return node->type_length > 0 ? (gsize)node->type_length : 0;
    default: return 0;
    }
}

static gboolean decode_plain(const ParquetNode* node, const guchar* p, gsize len, gsize n, Values* out) {
    const guchar* end = p + len;
    if (node->type == PARQUET_BOOLEAN) {
        if (n > len * 8) return FALSE;
        for (gsize i = 0; i < n; i++) {
            guchar b = (p[i / 8] >> (i % 8)) & 1;
            values_add(out, &b, 1);
        }
        return TRUE;
    }
    if (node->type == PARQUET_BYTE_ARRAY) {
        for (gsize i = 0; i < n; i++) {
            if (end - p < 4) return FALSE;
            guint32 l = le32(p);
            p += 4;
            if (l > (gsize)(end - p)) return FALSE;
            values_add(out, p, l);
            p += l;
        }
        return TRUE;
    }
    gsize w = type_width(node);
    if (w == 0 || n > len / w) return FALSE;
    for (gsize i = 0; i < n; i++) values_add(out, p + i * w, w);
    return TRUE;
}

static gboolean decode_byte_stream_split(const ParquetNode* node, const guchar* p, gsize len, gsize n, Values* out) {
    gsize w = type_width(node);
    if (w == 0 || node->type == PARQUET_BOOLEAN || w > 32) return FALSE;
    gsize total = len / w;
    if (n > total) return FALSE;
    guchar value[32];
    for (gsize i = 0; i < n; i++) {
        for (gsize b = 0; b < w; b++) value[b] = p[b * total + i];
        values_add(out, value, w);
    }
    return TRUE;
}

static void add_int(Values* out, const ParquetNode* node, gint64 v) {
    guchar b[8];
    for (guint i = 0; i < 8; i++) b[i] = (guchar)((guint64)v >> (8 * i));
    values_add(out, b, node->type == PARQUET_INT32 ? 4 : 8);
}

static gboolean decode_delta(const ParquetNode* node, const guchar* p, gsize len, gsize n, Values* out) {
    if (node->type != PARQUET_INT32 && node->type != PARQUET_INT64) return FALSE;
    gint64* v = g_new(gint64, MAX(n, 1));
    gsize total = 0;
    gboolean ok = delta_decode(p, p + len, v, n, &total) != NULL && total >= n;
    for (gsize i = 0; ok && i < n; i++) add_int(out, node, v[i]);
    g_free(v);
    return ok;
}

static gboolean decode_delta_length(const guchar* p, gsize len, gsize n, Values* out) {
    const guchar* end = p + len;
    gint64* lens = g_new(gint64, MAX(n, 1));
    gsize total = 0;
    const guchar* data = delta_decode(p, end, lens, n, &total);
    gboolean ok = data != NULL && total >= n;
    for (gsize i = 0; ok && i < n; i++) {
        if (lens[i] < 0 || (guint64)lens[i] > (guint64)(end - data)) {
            ok = FALSE;
            break;
        }
        values_add(out, data, (gsize)lens[i]);
        data += lens[i];
    }
    g_free(lens);
    return ok;
}

static gboolean decode_delta_strings(const guchar* p, gsize len, gsize n, Values* out) {
    const guchar* end = p + len;
    gint64* prefix = g_new(gint64, MAX(n, 1));
    gint64* suffix = g_new(gint64, MAX(n, 1));
    gsize n_prefix = 0, n_suffix = 0;
    const guchar* q = delta_decode(p, end, prefix, n, &n_prefix);
    const guchar* data = q != NULL ? delta_decode(q, end, suffix, n, &n_suffix) : NULL;
    gboolean ok = data != NULL && n_prefix >= n && n_suffix >= n;
    GByteArray* prev = g_byte_array_new();
    for (gsize i = 0; ok && i < n; i++) {
        if (prefix[i] < 0 || (guint64)prefix[i] > prev->len || suffix[i] < 0 ||
            (guint64)suffix[i] > (guint64)(end - data)) {
            ok = FALSE;
            break;
        }
        g_byte_array_set_size(prev, (guint)prefix[i]);
        g_byte_array_append(prev, data, (guint)suffix[i]);
        data += suffix[i];
        values_add(out, prev->data, prev->len);
    }
    g_byte_array_unref(prev);
    g_free(prefix);
    g_free(suffix);
    return ok;
}

/* The values of a page section in an encoding (not the dictionary ones) */
static gboolean decode_values(const ParquetNode* node, gint encoding, const guchar* p, gsize len, gsize n,
                              Values* out, GError** error) {
    gboolean ok;
    switch (encoding) {
    case PARQUET_PLAIN: ok = decode_plain(node, p, len, n, out); break;
    case PARQUET_BYTE_STREAM_SPLIT: ok = decode_byte_stream_split(node, p, len, n, out); break;
    case PARQUET_DELTA_BINARY_PACKED: ok = decode_delta(node, p, len, n, out); break;
    case PARQUET_DELTA_LENGTH_BYTE_ARRAY: ok = decode_delta_length(p, len, n, out); break;
    case PARQUET_DELTA_BYTE_ARRAY: ok = decode_delta_strings(p, len, n, out); break;
    case PARQUET_RLE: {
        /* Booleans: a length, then a hybrid run of 1-bit values */
        if (node->type != PARQUET_BOOLEAN || len < 4 || le32(p) > len - 4) return corrupt(error);
        guint32* bits = g_new(guint32, MAX(n, 1));
        ok = hybrid_decode(p + 4, le32(p), 1, bits, n) == n;
        for (gsize i = 0; ok && i < n; i++) {
            guchar b = bits[i] != 0;
            values_add(out, &b, 1);
        }
        g_free(bits);
        break;
    }
    default:
        /* TRANSLATORS: %s is a Parquet encoding, e.g. "BIT_PACKED" */
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("%s encoding is not supported"),
                    parquet_encoding_name(encoding));
        return FALSE;
    }
    return ok ? TRUE : corrupt(error);
}

/* --- Formatting --- */

static gchar* format_decimal(guint64 magnitude, gboolean negative, gint scale) {
    gchar* digits = g_strdup_printf("%" G_GUINT64_FORMAT, magnitude);
    if (scale <= 0) {
        gchar* s = g_strdup_printf("%s%s", negative ? "-" : "", digits);
        g_free(digits);
        return s;
    }
    gsize nd = strlen(digits);
    GString* s = g_string_new(negative ? "-" : "");
    if (nd <= (gsize)scale) {
        g_string_append(s, "0.");
        for (gsize i = nd; i < (gsize)scale; i++) g_string_append_c(s, '0');
        g_string_append(s, digits);
    } else {
        g_string_append_len(s, digits, (gssize)(nd - (gsize)scale));
        g_string_append_c(s, '.');
        g_string_append(s, digits + nd - scale);
    }
    g_free(digits);
    return g_string_free(s, FALSE);
}

static gchar* format_signed_decimal(gint64 v, gint scale) {
    return format_decimal(v < 0 ? (guint64)0 - (guint64)v : (guint64)v, v < 0, scale);
}

static gchar* format_hex(const guchar* b, gsize len) {
    GString* s = g_string_new("0x");
    for (gsize i = 0; i < MIN(len, 32); i++) g_string_append_printf(s, "%02x", b[i]);
    if (len > 32) g_string_append(s, "…");
    return g_string_free(s, FALSE);
}

/* Units per second of a time unit */
static gint64 unit_scale(ParquetTimeUnit unit) {
    return unit == PARQUET_MILLIS ? 1000 : unit == PARQUET_MICROS ? 1000000 : 1000000000;
}

/* ".250", ".000250"... ("" for 0) */
static void append_fraction(GString* s, gint64 frac, ParquetTimeUnit unit) {
    if (frac == 0) return;
    gint digits = unit == PARQUET_MILLIS ? 3 : unit == PARQUET_MICROS ? 6 : 9;
    g_string_append_printf(s, ".%0*" G_GINT64_FORMAT, digits, frac);
}

static gchar* format_timestamp(gint64 v, ParquetTimeUnit unit) {
    gint64 scale = unit_scale(unit);
    gint64 secs = v / scale, frac = v % scale;
    if (frac < 0) {
        frac += scale;
        secs--;
    }
    GDateTime* dt = g_date_time_new_from_unix_utc(secs);
    if (dt == NULL) return g_strdup_printf("%" G_GINT64_FORMAT, v);
    gchar* text = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
    g_date_time_unref(dt);
    GString* s = g_string_new(text);
    g_free(text);
    append_fraction(s, frac, unit);
    return g_string_free(s, FALSE);
}

static gchar* format_time(gint64 v, ParquetTimeUnit unit) {
    gint64 scale = unit_scale(unit);
    if (v < 0) return g_strdup_printf("%" G_GINT64_FORMAT, v);
    gint64 secs = v / scale;
    GString* s = g_string_new(NULL);
    g_string_append_printf(s, "%02d:%02d:%02d", (gint)(secs / 3600), (gint)(secs / 60 % 60), (gint)(secs % 60));
    append_fraction(s, v % scale, unit);
    return g_string_free(s, FALSE);
}

static gchar* format_date(gint64 days) {
    GDateTime* dt = g_date_time_new_from_unix_utc(days * 86400);
    if (dt == NULL) return g_strdup_printf("%" G_GINT64_FORMAT, days);
    gchar* text = g_date_time_format(dt, "%Y-%m-%d");
    g_date_time_unref(dt);
    return text;
}

static gchar* format_double(gdouble d) {
    if (isnan(d)) return g_strdup("NaN");
    if (isinf(d)) return g_strdup(d > 0 ? "inf" : "-inf");
    /* The shortest form that reads back as the same number */
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    for (gint digits = 15; digits <= 17; digits++) {
        gchar format[8];
        g_snprintf(format, sizeof(format), "%%.%dg", digits);
        g_ascii_formatd(buf, sizeof(buf), format, d);
        if (g_ascii_strtod(buf, NULL) == d) break;
    }
    return g_strdup(buf);
}

static gchar* format_float(gfloat f) {
    if (isnan(f)) return g_strdup("NaN");
    if (isinf(f)) return g_strdup(f > 0 ? "inf" : "-inf");
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    for (gint digits = 6; digits <= 9; digits++) {
        gchar format[8];
        g_snprintf(format, sizeof(format), "%%.%dg", digits);
        g_ascii_formatd(buf, sizeof(buf), format, f);
        if ((gfloat)g_ascii_strtod(buf, NULL) == f) break;
    }
    return g_strdup(buf);
}

static gfloat half_to_float(guint16 h) {
    gint exp = (h >> 10) & 0x1F;
    gint mant = h & 0x3FF;
    gfloat v = exp == 0 ? ldexpf((gfloat)mant, -24)
             : exp == 31 ? (mant != 0 ? NAN : INFINITY)
             : ldexpf((gfloat)(mant | 0x400), exp - 25);
    return (h & 0x8000) ? -v : v;
}

/* Text of a string value: valid UTF-8, control characters as spaces */
static gchar* format_text(const guchar* b, gsize len) {
    gsize n = MIN(len, CELL_MAX_CHARS * 4);
    gchar* text = g_utf8_make_valid((const gchar*)b, (gssize)n);
    for (gchar* p = text; *p != '\0'; p++) {
        if ((guchar)*p < 0x20 || *p == 0x7F) *p = ' ';
    }
    if (g_utf8_strlen(text, -1) > CELL_MAX_CHARS || n < len) {
        gchar* cut = g_utf8_offset_to_pointer(text, MIN(g_utf8_strlen(text, -1), CELL_MAX_CHARS));
        *cut = '\0';
        gchar* s = g_strconcat(text, "…", NULL);
        g_free(text);
        return s;
    }
    return text;
}

/* A big-endian two's complement decimal of up to 8 significant bytes */
static gchar* format_be_decimal(const guchar* b, gsize len, gint scale) {
    gboolean negative = len > 0 && (b[0] & 0x80);
    gsize skip = 0;
    while (skip + 8 < len && b[skip] == (negative ? 0xFF : 0x00)) skip++;
    if (len - skip > 8) return format_hex(b, len);
    guint64 v = negative ? G_MAXUINT64 : 0;
    for (gsize i = skip; i < len; i++) v = (v << 8) | b[i];
    if (len == 0) v = 0;
    return format_signed_decimal((gint64)v, scale);
}

static gchar* format_bytes(const ParquetNode* node, const guchar* b, gsize len) {
    switch (node->logical) {
    case PARQUET_LOGICAL_DECIMAL: return format_be_decimal(b, len, node->scale);
    case PARQUET_LOGICAL_UUID:
        if (len == 16) {
            return g_strdup_printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                                   b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
                                   b[12], b[13], b[14], b[15]);
        }
        break;
    case PARQUET_LOGICAL_FLOAT16:
        if (len == 2) {
            gfloat f = half_to_float((guint16)(b[0] | (b[1] << 8)));
            if (isnan(f) || isinf(f)) return format_float(f);
            gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
            return g_strdup(g_ascii_formatd(buf, sizeof(buf), "%.4g", f));  /* a half has ~3.3 digits */
        }
        break;
    case PARQUET_LOGICAL_STRING: case PARQUET_LOGICAL_ENUM: case PARQUET_LOGICAL_JSON:
        return format_text(b, len);
    default:
        break;
    }
    /* Plain binary: text when it is text */
    if (node->logical == PARQUET_LOGICAL_NONE && len > 0 && memchr(b, 0, len) == NULL &&
        g_utf8_validate((const gchar*)b, (gssize)MIN(len, CELL_MAX_CHARS * 4), NULL)) {
        return format_text(b, len);
    }
    return format_hex(b, len);
}

static gchar* format_integer(const ParquetNode* node, gint64 v, gboolean is64) {
    switch (node->logical) {
    case PARQUET_LOGICAL_DATE: return format_date(v);
    case PARQUET_LOGICAL_DECIMAL: return format_signed_decimal(v, node->scale);
    case PARQUET_LOGICAL_TIME: return format_time(v, node->unit);
    case PARQUET_LOGICAL_TIMESTAMP: return format_timestamp(v, node->unit);
    case PARQUET_LOGICAL_INTEGER:
        if (!node->int_signed) {
            return g_strdup_printf("%" G_GUINT64_FORMAT, is64 ? (guint64)v : (guint64)(guint32)v);
        }
        break;
    default: break;
    }
    return g_strdup_printf("%" G_GINT64_FORMAT, v);
}

gchar* parquet_format_value(const ParquetNode* node, const guchar* b, gsize len) {
    switch (node->type) {
    case PARQUET_BOOLEAN:
        return g_strdup(len > 0 && b[0] ? "true" : "false");
    case PARQUET_INT32:
        if (len < 4) break;
        return format_integer(node, (gint32)le32(b), FALSE);
    case PARQUET_INT64:
        if (len < 8) break;
        return format_integer(node, (gint64)le64(b), TRUE);
    case PARQUET_INT96: {
        /* Impala timestamps: nanoseconds of the day, then the Julian day */
        if (len < 12) break;
        gint64 nanos = (gint64)le64(b);
        gint64 days = (gint64)le32(b + 8) - 2440588;
        if (days < -1000000 || days > 1000000 || nanos < 0 || nanos >= 86400 * G_GINT64_CONSTANT(1000000000)) {
            break;
        }
        return format_timestamp(days * 86400 * G_GINT64_CONSTANT(1000000000) + nanos, PARQUET_NANOS);
    }
    case PARQUET_FLOAT: {
        if (len < 4) break;
        guint32 bits = le32(b);
        gfloat f;
        memcpy(&f, &bits, 4);
        return format_float(f);
    }
    case PARQUET_DOUBLE: {
        if (len < 8) break;
        guint64 bits = le64(b);
        gdouble d;
        memcpy(&d, &bits, 8);
        return format_double(d);
    }
    case PARQUET_BYTE_ARRAY: case PARQUET_FIXED_LEN_BYTE_ARRAY:
        return format_bytes(node, b, len);
    default:
        break;
    }
    return format_hex(b, len);
}

/* --- Columns --- */

typedef struct {
    const ParquetMeta* meta;
    const ParquetLeaf* leaf;
    const ParquetNode* node;
    Values dict;
    gboolean has_dict;
    GPtrArray* cells;
    guint max_rows;
    /* A list being assembled (repeated columns) */
    GString* row;
    gboolean row_null;
    gint row_depth;          /* lists open */
    gboolean row_cut;        /* "…" added: the other elements are skipped */
} Column;

static gboolean column_full(const Column* c) {
    return c->cells->len >= c->max_rows;
}

static void finish_row(Column* c) {
    if (c->row == NULL) return;
    if (c->row_null) {
        g_string_free(c->row, TRUE);
        g_ptr_array_add(c->cells, NULL);
    } else {
        for (gint i = 0; i < c->row_depth && !c->row_cut; i++) g_string_append_c(c->row, ']');
        g_ptr_array_add(c->cells, g_string_free(c->row, FALSE));
    }
    c->row = NULL;
}

static void row_append(Column* c, const gchar* text) {
    if (c->row_cut) return;
    if (c->row->len >= CELL_MAX_CHARS) {
        g_string_append(c->row, "…");
        c->row_cut = TRUE;
        return;
    }
    g_string_append(c->row, text);
}

/* One level entry of a repeated column: rep says at which level it starts
 * a new element, def how deep it is defined */
static void emit_repeated(Column* c, guint32 rep, guint32 def, const gchar* value) {
    gint levels = MIN(c->leaf->max_rep, PARQUET_MAX_REP);
    if (rep == 0) {
        c->row = g_string_new(NULL);
        c->row_cut = FALSE;
        c->row_depth = 0;
        /* No list at all: a null one */
        c->row_null = (gint)def < c->leaf->rep_def[0] - 1;
        if (c->row_null) return;
    } else {
        while (c->row_depth > MIN((gint)rep, levels)) {
            row_append(c, "]");
            c->row_depth--;
        }
        row_append(c, ", ");
    }
    /* Open the lists the entry is in; an empty or null one ends it */
    while (c->row_depth < levels) {
        gint k = c->row_depth;
        if ((gint)def >= c->leaf->rep_def[k]) {
            row_append(c, "[");
            c->row_depth++;
            continue;
        }
        row_append(c, (gint)def >= c->leaf->rep_def[k] - 1 ? "[]" : "null");
        return;
    }
    row_append(c, c->leaf->max_rep > PARQUET_MAX_REP ? "…" : value != NULL ? value : "null");
}

/* The text of value k: from the dictionary (indices) or values */
static gchar* value_text(const Column* c, const Values* vals, const guint32* indices, guint k) {
    const Values* src = vals;
    guint i = k;
    if (indices != NULL) {
        src = &c->dict;
        i = indices[k];
        if (i >= values_len(src)) return g_strdup("?");
    }
    gsize len;
    const guchar* b = values_get(src, i, &len);
    return parquet_format_value(c->node, b, len);
}

/* Turn the level entries and values of a page into cells */
static void emit(Column* c, const guint32* rep, const guint32* def, gsize n_entries, const Values* vals,
                 const guint32* indices) {
    guint k = 0;
    for (gsize i = 0; i < n_entries; i++) {
        gboolean present = def == NULL || (gint)def[i] == c->leaf->max_def;
        if (c->leaf->max_rep == 0) {
            if (column_full(c)) return;
            g_ptr_array_add(c->cells, present ? value_text(c, vals, indices, k) : NULL);
            if (present) k++;
            continue;
        }
        if (rep[i] == 0) {
            finish_row(c);
            if (column_full(c)) return;
        }
        if (rep[i] == 0 || c->row != NULL) {  /* else a row started before the chunk */
            gchar* text = present && !c->row_cut ? value_text(c, vals, indices, k) : NULL;
            emit_repeated(c, rep[i], def != NULL ? def[i] : (guint32)c->leaf->max_def, text);
            g_free(text);
        }
        if (present) k++;
    }
}

/* Levels of a section: hybrid runs (v1 RLE levels have a length first) */
static gboolean read_levels(const guchar** p, const guchar* end, gboolean prefixed, gsize section, gint max,
                            gsize n, guint32** out, GError** error) {
    *out = NULL;
    if (max == 0) return TRUE;
    gsize len = section;
    if (prefixed) {
        if (end - *p < 4 || le32(*p) > (gsize)(end - *p) - 4) return corrupt(error);
        len = le32(*p);
        *p += 4;
    } else if (len > (gsize)(end - *p)) {
        return corrupt(error);
    }
    *out = g_new(guint32, MAX(n, 1));
    gsize got = hybrid_decode(*p, len, bit_width((guint64)max), *out, n);
    *p += len;
    if (got < n) return corrupt(error);
    for (gsize i = 0; i < n; i++) {
        if ((*out)[i] > (guint32)max) return corrupt(error);
    }
    return TRUE;
}

static gboolean data_page(Column* c, const ParquetPageHeader* h, const guchar* p, gsize len, GError** error) {
    const guchar* end = p + len;
    gboolean v2 = h->type == PARQUET_PAGE_DATA_V2;
    if (!v2 && ((c->leaf->max_rep > 0 && h->rep_encoding != PARQUET_RLE) ||
                (c->leaf->max_def > 0 && h->def_encoding != PARQUET_RLE))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("%s encoding is not supported"),
                    parquet_encoding_name(PARQUET_BIT_PACKED));
        return FALSE;
    }
    gsize n = (gsize)h->num_values;
    if (c->leaf->max_rep == 0) n = MIN(n, c->max_rows - c->cells->len);
    if (n > LEVELS_MAX) return corrupt(error);
    guint32* rep = NULL;
    guint32* def = NULL;
    gboolean ok = read_levels(&p, end, !v2, (gsize)h->rep_length, c->leaf->max_rep, n, &rep, error) &&
                  read_levels(&p, end, !v2, (gsize)h->def_length, c->leaf->max_def, n, &def, error);
    /* The values of the entries kept */
    gsize n_values = 0;
    for (gsize i = 0; ok && i < n; i++) n_values += def == NULL || (gint)def[i] == c->leaf->max_def;
    Values vals = { 0 };
    guint32* indices = NULL;
    gsize vlen = (gsize)(end - p);
    if (ok && (h->encoding == PARQUET_PLAIN_DICTIONARY || h->encoding == PARQUET_RLE_DICTIONARY)) {
        if (!c->has_dict) {
            ok = corrupt(error);
        } else if (n_values > 0) {
            indices = g_new(guint32, n_values);
            ok = vlen >= 1 && hybrid_decode(p + 1, vlen - 1, p[0], indices, n_values) == n_values;
            if (!ok) corrupt(error);
        }
    } else if (ok) {
        values_init(&vals);
        ok = decode_values(c->node, h->encoding, p, vlen, n_values, &vals, error);
    }
    if (ok) emit(c, rep, def, n, &vals, indices);
    values_clear(&vals);
    g_free(indices);
    g_free(rep);
    g_free(def);
    return ok;
}

static gboolean dictionary_page(Column* c, const ParquetPageHeader* h, const guchar* p, gsize len,
                                GError** error) {
    if (h->encoding != PARQUET_PLAIN && h->encoding != PARQUET_PLAIN_DICTIONARY) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("%s encoding is not supported"),
                    parquet_encoding_name(h->encoding));
        return FALSE;
    }
    values_clear(&c->dict);
    values_init(&c->dict);
    c->has_dict = decode_plain(c->node, p, len, (gsize)h->num_values, &c->dict);
    return c->has_dict ? TRUE : corrupt(error);
}

/* Read the pages of a chunk until the column has its rows */
static gboolean read_chunk(Column* c, const ParquetChunk* chunk, ParquetReadFunc read, gpointer source,
                           guint64 file_size, GError** error) {
    if (chunk->external) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("the column is in another file"));
        return FALSE;
    }
    gint64 start = chunk->data_page_offset;
    if (chunk->dictionary_page_offset > 0 && chunk->dictionary_page_offset < start) {
        start = chunk->dictionary_page_offset;
    }
    if (start < 4 || chunk->compressed_size <= 0 || (guint64)start >= file_size) return corrupt(error);
    guint64 end = MIN((guint64)start + (guint64)chunk->compressed_size, file_size);
    guint64 off = (guint64)start;
    gsize window = 64 * 1024;
    guchar* head = g_malloc(window);
    gboolean ok = TRUE;
    c->has_dict = FALSE;
    while (ok && off < end && !column_full(c)) {
        gsize n = (gsize)MIN((guint64)window, end - off);
        ParquetPageHeader h;
        gsize hlen = 0;
        if (!read(source, off, head, n)) {
            ok = corrupt(error);
            break;
        }
        hlen = parquet_page_header_parse(head, n, &h);
        if (hlen == 0 && n == window && window < HEADER_MAX) {
            window = HEADER_MAX;  /* large statistics: try again with more */
            head = g_realloc(head, window);
            continue;
        }
        if (hlen == 0 || h.compressed_size > PARQUET_PAGE_MAX || h.uncompressed_size > PARQUET_PAGE_MAX ||
            (guint64)h.compressed_size > end - off - hlen) {
            ok = corrupt(error);
            break;
        }
        off += hlen;
        if (h.type != PARQUET_PAGE_DATA && h.type != PARQUET_PAGE_DATA_V2 && h.type != PARQUET_PAGE_DICTIONARY) {
            off += (guint64)h.compressed_size;  /* index pages */
            continue;
        }
        guchar* raw = g_malloc(MAX(h.compressed_size, 1));
        if (!read(source, off, raw, (gsize)h.compressed_size)) {
            g_free(raw);
            ok = corrupt(error);
            break;
        }
        off += (guint64)h.compressed_size;
        /* v2 levels come first, never compressed */
        gsize levels = h.type == PARQUET_PAGE_DATA_V2 ? (gsize)h.rep_length + (gsize)h.def_length : 0;
        if (levels > (gsize)h.compressed_size || levels > (gsize)h.uncompressed_size) {
            g_free(raw);
            ok = corrupt(error);
            break;
        }
        guchar* page = raw;
        gboolean compressed = chunk->codec != PARQUET_UNCOMPRESSED &&
                              (h.type != PARQUET_PAGE_DATA_V2 || h.is_compressed);
        if (compressed) {
            page = g_malloc(MAX(h.uncompressed_size, 1));
            memcpy(page, raw, levels);
            ok = parquet_decompress(chunk->codec, raw + levels, (gsize)h.compressed_size - levels, page + levels,
                                    (gsize)h.uncompressed_size - levels, error);
            g_free(raw);
        }
        gsize page_len = compressed ? (gsize)h.uncompressed_size : (gsize)h.compressed_size;
        if (ok && h.type == PARQUET_PAGE_DICTIONARY) ok = dictionary_page(c, &h, page, page_len, error);
        else if (ok) ok = data_page(c, &h, page, page_len, error);
        g_free(page);
    }
    g_free(head);
    finish_row(c);
    return ok;
}

gboolean parquet_column_read(const ParquetMeta* meta, guint leaf, ParquetReadFunc read, gpointer source,
                             guint64 file_size, guint max_rows, GPtrArray* cells, GError** error) {
    g_return_val_if_fail(leaf < meta->n_leaves, FALSE);
    Column c = { .meta = meta, .leaf = &meta->leaves[leaf], .node = &meta->nodes[meta->leaves[leaf].node],
                 .cells = cells, .max_rows = cells->len + max_rows };
    if (c.node->type == PARQUET_FIXED_LEN_BYTE_ARRAY && c.node->type_length <= 0) return corrupt(error);
    gboolean ok = TRUE;
    for (guint g = 0; ok && g < meta->n_row_groups && !column_full(&c); g++) {
        const ParquetRowGroup* rg = &meta->row_groups[g];
        if (rg->num_rows == 0) continue;
        if (rg->chunks == NULL) {
            ok = corrupt(error);
            break;
        }
        guint before = cells->len;
        ok = read_chunk(&c, &rg->chunks[leaf], read, source, file_size, error);
        /* A chunk with fewer rows than its row group: keep the columns aligned */
        while (ok && cells->len - before < (guint)MIN(rg->num_rows, (gint64)G_MAXUINT) && !column_full(&c)) {
            g_ptr_array_add(cells, NULL);
        }
    }
    values_clear(&c.dict);
    return ok;
}
