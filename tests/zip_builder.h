/* Test helper: builds small ZIP archives in memory (stored or deflated
 * entries, optionally in ZIP64 form). Header-only, for tests/ only. */
#ifndef TEST_ZIP_BUILDER_H
#define TEST_ZIP_BUILDER_H

#include <gio/gio.h>
#include <string.h>

typedef struct {
    GByteArray* data;
    GByteArray* central;
    guint n_entries;
    gboolean zip64;
} ZipBuilder;

static inline void zb_u16(GByteArray* a, guint v) {
    guint8 b[2] = { (guint8)v, (guint8)(v >> 8) };
    g_byte_array_append(a, b, 2);
}

static inline void zb_u32(GByteArray* a, guint32 v) {
    guint8 b[4] = { (guint8)v, (guint8)(v >> 8), (guint8)(v >> 16), (guint8)(v >> 24) };
    g_byte_array_append(a, b, 4);
}

static inline void zb_u64(GByteArray* a, guint64 v) {
    zb_u32(a, (guint32)v);
    zb_u32(a, (guint32)(v >> 32));
}

static inline ZipBuilder* zip_builder_new(gboolean zip64) {
    ZipBuilder* zb = g_new0(ZipBuilder, 1);
    zb->data = g_byte_array_new();
    zb->central = g_byte_array_new();
    zb->zip64 = zip64;
    return zb;
}

static inline GByteArray* zb_deflate(const guchar* in, gsize len) {
    GConverter* conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW, 6));
    GByteArray* out = g_byte_array_new();
    guchar buf[4096];
    gsize pos = 0;
    for (;;) {
        gsize r = 0, w = 0;
        GConverterResult res = g_converter_convert(conv, in + pos, len - pos, buf, sizeof buf,
                                                   G_CONVERTER_INPUT_AT_END, &r, &w, NULL);
        g_assert_true(res != G_CONVERTER_ERROR);
        pos += r;
        g_byte_array_append(out, buf, (guint)w);
        if (res == G_CONVERTER_FINISHED) {
            break;
        }
    }
    g_object_unref(conv);
    return out;
}

/* Add an entry; flags lets a test set the encryption bit */
static inline void zip_builder_add_full(ZipBuilder* zb, const gchar* name, const void* content,
                                        gsize len, gboolean deflate, guint flags) {
    GByteArray* comp = deflate ? zb_deflate(content, len) : NULL;
    const guint8* payload = comp != NULL ? comp->data : content;
    gsize comp_len = comp != NULL ? comp->len : len;
    guint32 offset = zb->data->len;
    guint method = deflate ? 8 : 0;
    gsize name_len = strlen(name);

    /* Local header (sizes always given directly: readers use the central directory) */
    zb_u32(zb->data, 0x04034b50);
    zb_u16(zb->data, 20);
    zb_u16(zb->data, flags);
    zb_u16(zb->data, method);
    zb_u32(zb->data, 0); /* time, date */
    zb_u32(zb->data, 0); /* crc (not checked) */
    zb_u32(zb->data, (guint32)comp_len);
    zb_u32(zb->data, (guint32)len);
    zb_u16(zb->data, (guint)name_len);
    zb_u16(zb->data, 0);
    g_byte_array_append(zb->data, (const guint8*)name, (guint)name_len);
    g_byte_array_append(zb->data, payload, (guint)comp_len);

    /* Central directory header */
    zb_u32(zb->central, 0x02014b50);
    zb_u16(zb->central, 45);
    zb_u16(zb->central, 20);
    zb_u16(zb->central, flags);
    zb_u16(zb->central, method);
    zb_u32(zb->central, 0);
    zb_u32(zb->central, 0);
    zb_u32(zb->central, zb->zip64 ? 0xFFFFFFFF : (guint32)comp_len);
    zb_u32(zb->central, zb->zip64 ? 0xFFFFFFFF : (guint32)len);
    zb_u16(zb->central, (guint)name_len);
    zb_u16(zb->central, zb->zip64 ? 28 : 0);
    zb_u16(zb->central, 0); /* comment */
    zb_u16(zb->central, 0); /* disk */
    zb_u16(zb->central, 0); /* internal attributes */
    zb_u32(zb->central, 0); /* external attributes */
    zb_u32(zb->central, zb->zip64 ? 0xFFFFFFFF : offset);
    g_byte_array_append(zb->central, (const guint8*)name, (guint)name_len);
    if (zb->zip64) {
        zb_u16(zb->central, 0x0001);
        zb_u16(zb->central, 24);
        zb_u64(zb->central, len);
        zb_u64(zb->central, comp_len);
        zb_u64(zb->central, offset);
    }
    zb->n_entries++;
    if (comp != NULL) {
        g_byte_array_free(comp, TRUE);
    }
}

static inline void zip_builder_add(ZipBuilder* zb, const gchar* name, const gchar* text, gboolean deflate) {
    zip_builder_add_full(zb, name, text, strlen(text), deflate, 0);
}

/* Finish the archive and free the builder */
static inline GBytes* zip_builder_finish(ZipBuilder* zb) {
    guint32 cd_offset = zb->data->len;
    guint32 cd_size = zb->central->len;
    g_byte_array_append(zb->data, zb->central->data, zb->central->len);
    if (zb->zip64) {
        guint32 rec_offset = zb->data->len;
        zb_u32(zb->data, 0x06064b50);
        zb_u64(zb->data, 44);
        zb_u16(zb->data, 45);
        zb_u16(zb->data, 45);
        zb_u32(zb->data, 0);
        zb_u32(zb->data, 0);
        zb_u64(zb->data, zb->n_entries);
        zb_u64(zb->data, zb->n_entries);
        zb_u64(zb->data, cd_size);
        zb_u64(zb->data, cd_offset);
        zb_u32(zb->data, 0x07064b50);
        zb_u32(zb->data, 0);
        zb_u64(zb->data, rec_offset);
        zb_u32(zb->data, 1);
    }
    zb_u32(zb->data, 0x06054b50);
    zb_u16(zb->data, 0);
    zb_u16(zb->data, 0);
    zb_u16(zb->data, zb->zip64 ? 0xFFFF : zb->n_entries);
    zb_u16(zb->data, zb->zip64 ? 0xFFFF : zb->n_entries);
    zb_u32(zb->data, zb->zip64 ? 0xFFFFFFFF : cd_size);
    zb_u32(zb->data, zb->zip64 ? 0xFFFFFFFF : cd_offset);
    zb_u16(zb->data, 0);
    g_byte_array_free(zb->central, TRUE);
    GBytes* bytes = g_byte_array_free_to_bytes(zb->data);
    g_free(zb);
    return bytes;
}

#endif /* TEST_ZIP_BUILDER_H */
