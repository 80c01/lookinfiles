/* PNG encoding of an RGB picture (see png_encode.h) */
#include "formats/png_encode.h"

#include <gio/gio.h>
#include <string.h>

/* CRC-32 of PNG chunks (ISO 3309, as zlib's crc32) */
static guint32 crc_table[256];

static void crc_init(void) {
    static gsize done = 0;
    if (g_once_init_enter(&done)) {
        for (guint32 n = 0; n < 256; n++) {
            guint32 c = n;
            for (gint k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            crc_table[n] = c;
        }
        g_once_init_leave(&done, 1);
    }
}

static guint32 crc_update(guint32 crc, const guint8* buf, gsize len) {
    for (gsize i = 0; i < len; i++) {
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void put_be32(GByteArray* out, guint32 v) {
    guint8 b[4] = {(guint8)(v >> 24), (guint8)(v >> 16), (guint8)(v >> 8), (guint8)v};
    g_byte_array_append(out, b, 4);
}

/* One chunk: length, type, data, CRC of type and data */
static void put_chunk(GByteArray* out, const gchar* type, const guint8* data, gsize len) {
    put_be32(out, (guint32)len);
    gsize start = out->len;
    g_byte_array_append(out, (const guint8*)type, 4);
    if (len > 0) {
        g_byte_array_append(out, data, (guint)len);
    }
    guint32 crc = crc_update(0xFFFFFFFFu, out->data + start, len + 4) ^ 0xFFFFFFFFu;
    put_be32(out, crc);
}

/* zlib stream of the raw scanlines (filter byte 0, then R G B per pixel) */
static GByteArray* deflate_rows(const guint8* data, gint width, gint height, gint stride, gint level) {
    GConverter* conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, CLAMP(level, 0, 9)));
    GByteArray* z = g_byte_array_new();
    gsize row_len = 1 + (gsize)width * 3;
    guint8* row = g_malloc(row_len);
    guint8 buf[64 * 1024];
    gboolean ok = TRUE;
    for (gint y = 0; y <= height && ok; y++) {
        gboolean last = (y == height);
        gsize in_len = 0;
        if (!last) {
            const guint32* src = (const guint32*)(data + (gsize)y * stride);
            row[0] = 0;
            guint8* d = row + 1;
            for (gint x = 0; x < width; x++) {
                guint32 p = src[x];
                *d++ = (guint8)(p >> 16);
                *d++ = (guint8)(p >> 8);
                *d++ = (guint8)p;
            }
            in_len = row_len;
        }
        gsize pos = 0;
        while (TRUE) {
            gsize read = 0, written = 0;
            GError* error = NULL;
            GConverterResult res = g_converter_convert(conv, row + pos, in_len - pos, buf, sizeof(buf),
                                                       last ? G_CONVERTER_INPUT_AT_END : G_CONVERTER_NO_FLAGS,
                                                       &read, &written, &error);
            if (res == G_CONVERTER_ERROR) {
                g_clear_error(&error);
                ok = FALSE;
                break;
            }
            g_byte_array_append(z, buf, (guint)written);
            pos += read;
            /* zlib keeps what it could not write yet: the next row (or the
             * end) gets it out */
            if (res == G_CONVERTER_FINISHED || (!last && pos >= in_len)) {
                break;
            }
        }
    }
    g_free(row);
    g_object_unref(conv);
    if (!ok) {
        g_byte_array_free(z, TRUE);
        return NULL;
    }
    return z;
}

GBytes* png_encode_xrgb32(const guint8* data, gint width, gint height, gint stride, gint level) {
    if (data == NULL || width <= 0 || height <= 0 || stride < width * 4) {
        return NULL;
    }
    crc_init();
    GByteArray* z = deflate_rows(data, width, height, stride, level);
    if (z == NULL) {
        return NULL;
    }
    GByteArray* out = g_byte_array_sized_new(z->len + 64);
    static const guint8 signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    g_byte_array_append(out, signature, 8);
    guint8 ihdr[13];
    ihdr[0] = (guint8)(width >> 24);
    ihdr[1] = (guint8)(width >> 16);
    ihdr[2] = (guint8)(width >> 8);
    ihdr[3] = (guint8)width;
    ihdr[4] = (guint8)(height >> 24);
    ihdr[5] = (guint8)(height >> 16);
    ihdr[6] = (guint8)(height >> 8);
    ihdr[7] = (guint8)height;
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filtering (each row says its filter: none) */
    ihdr[12] = 0; /* not interlaced */
    put_chunk(out, "IHDR", ihdr, sizeof(ihdr));
    put_chunk(out, "IDAT", z->data, z->len);
    put_chunk(out, "IEND", NULL, 0);
    g_byte_array_free(z, TRUE);
    return g_byte_array_free_to_bytes(out);
}
