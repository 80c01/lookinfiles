/* Minimal ZIP archive reader: central directory + stored / deflated entries */
#include "zip_reader.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

#define EOCD_SIZE 22               /* end of central directory record */
#define EOCD_MAX_COMMENT 0xFFFF
#define ZIP64_LOCATOR_SIZE 20
#define ZIP64_EOCD_SIZE 56
#define CENTRAL_HEADER_SIZE 46
#define LOCAL_HEADER_SIZE 30
#define MAX_CENTRAL_DIR (64u * 1024 * 1024)

#define METHOD_STORED 0
#define METHOD_DEFLATED 8
#define FLAG_ENCRYPTED 0x0001

typedef struct {
    gchar* name;
    guint16 flags;
    guint16 method;
    guint64 comp_size;
    guint64 size;
    guint64 local_offset;
    guint16 dos_time;
    guint16 dos_date;
} ZipEntry;

struct ZipReader {
    GInputStream* stream;  /* seekable */
    guint64 archive_size;
    GPtrArray* entries;    /* ZipEntry*, in central directory order */
    GHashTable* by_name;   /* name -> ZipEntry* (first one wins) */
};

static guint16 get_u16(const guchar* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 get_u32(const guchar* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static guint64 get_u64(const guchar* p) {
    return (guint64)get_u32(p) | ((guint64)get_u32(p + 4) << 32);
}

static void zip_entry_free(gpointer data) {
    ZipEntry* e = data;
    g_free(e->name);
    g_free(e);
}

static gboolean set_invalid(GError** error, const gchar* what) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Not a valid ZIP archive: %s"), what);
    return FALSE;
}

/* Read exactly len bytes at offset */
static gboolean read_at(ZipReader* zip, guint64 offset, void* buf, gsize len, GError** error) {
    if (offset > zip->archive_size || len > zip->archive_size - offset) {
        return set_invalid(error, _("data past the end of the file"));
    }
    if (!g_seekable_seek(G_SEEKABLE(zip->stream), (goffset)offset, G_SEEK_SET, NULL, error)) {
        return FALSE;
    }
    gsize got = 0;
    if (!g_input_stream_read_all(zip->stream, buf, len, &got, NULL, error)) {
        return FALSE;
    }
    if (got != len) {
        return set_invalid(error, _("truncated file"));
    }
    return TRUE;
}

/* Offset and size of the central directory, and its number of entries */
static gboolean find_central_dir(ZipReader* zip, guint64* cd_offset, guint64* cd_size,
                                 guint64* n_entries, GError** error) {
    gsize tail_len = (gsize)MIN(zip->archive_size, (guint64)(EOCD_SIZE + EOCD_MAX_COMMENT));
    if (tail_len < EOCD_SIZE) {
        return set_invalid(error, _("file too short"));
    }
    guint64 tail_start = zip->archive_size - tail_len;
    guchar* tail = g_malloc(tail_len);
    if (!read_at(zip, tail_start, tail, tail_len, error)) {
        g_free(tail);
        return FALSE;
    }

    /* The record is at the end, followed by a comment of the length it gives */
    gssize found = -1;
    for (gssize i = (gssize)(tail_len - EOCD_SIZE); i >= 0; i--) {
        if (memcmp(tail + i, "PK\x05\x06", 4) == 0) {
            if ((gsize)i + EOCD_SIZE + get_u16(tail + i + 20) == tail_len) {
                found = i;
                break;
            }
            if (found < 0) {
                found = i; /* tolerate trailing junk if nothing better */
            }
        }
    }
    if (found < 0) {
        g_free(tail);
        return set_invalid(error, _("no end of central directory"));
    }
    const guchar* eocd = tail + found;
    guint64 eocd_pos = tail_start + (guint64)found;
    *n_entries = get_u16(eocd + 10);
    *cd_size = get_u32(eocd + 12);
    *cd_offset = get_u32(eocd + 16);
    g_free(tail);

    if (*n_entries == 0xFFFF || *cd_size == 0xFFFFFFFF || *cd_offset == 0xFFFFFFFF) {
        guchar loc[ZIP64_LOCATOR_SIZE];
        guchar rec[ZIP64_EOCD_SIZE];
        if (eocd_pos < ZIP64_LOCATOR_SIZE ||
            !read_at(zip, eocd_pos - ZIP64_LOCATOR_SIZE, loc, sizeof loc, NULL) ||
            memcmp(loc, "PK\x06\x07", 4) != 0) {
            return set_invalid(error, _("missing ZIP64 locator"));
        }
        if (!read_at(zip, get_u64(loc + 8), rec, sizeof rec, error)) {
            return FALSE;
        }
        if (memcmp(rec, "PK\x06\x06", 4) != 0) {
            return set_invalid(error, _("bad ZIP64 end of central directory"));
        }
        *n_entries = get_u64(rec + 32);
        *cd_size = get_u64(rec + 40);
        *cd_offset = get_u64(rec + 48);
    }

    if (*cd_offset > zip->archive_size || *cd_size > zip->archive_size - *cd_offset) {
        return set_invalid(error, _("central directory past the end of the file"));
    }
    if (*cd_size > MAX_CENTRAL_DIR || *n_entries > *cd_size / CENTRAL_HEADER_SIZE) {
        return set_invalid(error, _("central directory too large"));
    }
    return TRUE;
}

/* ZIP64 extended information extra field: 64-bit values for the fields set to 0xFFFFFFFF */
static void apply_zip64_extra(ZipEntry* e, const guchar* extra, gsize len,
                              gboolean need_size, gboolean need_comp, gboolean need_offset) {
    gsize i = 0;
    while (i + 4 <= len) {
        guint16 id = get_u16(extra + i);
        guint16 dlen = get_u16(extra + i + 2);
        const guchar* d = extra + i + 4;
        if ((gsize)dlen > len - i - 4) {
            return;
        }
        if (id == 0x0001) {
            gsize k = 0;
            if (need_size && k + 8 <= dlen) { e->size = get_u64(d + k); k += 8; }
            if (need_comp && k + 8 <= dlen) { e->comp_size = get_u64(d + k); k += 8; }
            if (need_offset && k + 8 <= dlen) { e->local_offset = get_u64(d + k); }
            return;
        }
        i += 4 + dlen;
    }
}

static gboolean read_central_dir(ZipReader* zip, GError** error) {
    guint64 cd_offset = 0, cd_size = 0, n_entries = 0;
    if (!find_central_dir(zip, &cd_offset, &cd_size, &n_entries, error)) {
        return FALSE;
    }
    guchar* cd = g_malloc(cd_size + 1);
    if (!read_at(zip, cd_offset, cd, (gsize)cd_size, error)) {
        g_free(cd);
        return FALSE;
    }

    gsize pos = 0;
    for (guint64 n = 0; n < n_entries; n++) {
        if (pos + CENTRAL_HEADER_SIZE > cd_size || memcmp(cd + pos, "PK\x01\x02", 4) != 0) {
            g_free(cd);
            return set_invalid(error, _("damaged central directory"));
        }
        const guchar* h = cd + pos;
        gsize name_len = get_u16(h + 28);
        gsize extra_len = get_u16(h + 30);
        gsize comment_len = get_u16(h + 32);
        if (pos + CENTRAL_HEADER_SIZE + name_len + extra_len + comment_len > cd_size) {
            g_free(cd);
            return set_invalid(error, _("damaged central directory"));
        }

        ZipEntry* e = g_new0(ZipEntry, 1);
        e->flags = get_u16(h + 8);
        e->method = get_u16(h + 10);
        e->comp_size = get_u32(h + 20);
        e->size = get_u32(h + 24);
        e->local_offset = get_u32(h + 42);
        e->dos_time = get_u16(h + 12);
        e->dos_date = get_u16(h + 14);
        const gchar* name = (const gchar*)h + CENTRAL_HEADER_SIZE;
        e->name = g_utf8_make_valid(name, (gssize)name_len);
        apply_zip64_extra(e, h + CENTRAL_HEADER_SIZE + name_len, extra_len,
                          e->size == 0xFFFFFFFF, e->comp_size == 0xFFFFFFFF,
                          e->local_offset == 0xFFFFFFFF);

        g_ptr_array_add(zip->entries, e);
        if (!g_hash_table_contains(zip->by_name, e->name)) {
            g_hash_table_insert(zip->by_name, e->name, e);
        }
        pos += CENTRAL_HEADER_SIZE + name_len + extra_len + comment_len;
    }
    g_free(cd);
    return TRUE;
}

/* Takes ownership of stream */
static ZipReader* zip_reader_new_from_stream(GInputStream* stream, GError** error) {
    ZipReader* zip = g_new0(ZipReader, 1);
    zip->stream = stream;
    zip->entries = g_ptr_array_new_with_free_func(zip_entry_free);
    zip->by_name = g_hash_table_new(g_str_hash, g_str_equal);

    if (!G_IS_SEEKABLE(stream) || !g_seekable_can_seek(G_SEEKABLE(stream)) ||
        !g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, error)) {
        if (error != NULL && *error == NULL) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Stream is not seekable"));
        }
        zip_reader_free(zip);
        return NULL;
    }
    zip->archive_size = (guint64)g_seekable_tell(G_SEEKABLE(stream));

    if (!read_central_dir(zip, error)) {
        zip_reader_free(zip);
        return NULL;
    }
    return zip;
}

ZipReader* zip_reader_open(const gchar* path, GError** error) {
    g_return_val_if_fail(path != NULL, NULL);
    GFile* file = g_file_new_for_path(path);
    GFileInputStream* stream = g_file_read(file, NULL, error);
    g_object_unref(file);
    if (stream == NULL) {
        return NULL;
    }
    return zip_reader_new_from_stream(G_INPUT_STREAM(stream), error);
}

ZipReader* zip_reader_new_from_bytes(GBytes* data, GError** error) {
    g_return_val_if_fail(data != NULL, NULL);
    return zip_reader_new_from_stream(g_memory_input_stream_new_from_bytes(data), error);
}

void zip_reader_free(ZipReader* zip) {
    if (zip == NULL) {
        return;
    }
    g_clear_object(&zip->stream);
    g_hash_table_destroy(zip->by_name);
    g_ptr_array_free(zip->entries, TRUE);
    g_free(zip);
}

guint zip_reader_get_n_entries(const ZipReader* zip) {
    return zip->entries->len;
}

const gchar* zip_reader_get_name(const ZipReader* zip, guint i) {
    g_return_val_if_fail(i < zip->entries->len, NULL);
    return ((const ZipEntry*)g_ptr_array_index(zip->entries, i))->name;
}

gboolean zip_reader_get_info(const ZipReader* zip, guint i, ZipEntryInfo* info) {
    if (i >= zip->entries->len) {
        return FALSE;
    }
    const ZipEntry* e = g_ptr_array_index(zip->entries, i);
    info->name = e->name;
    info->size = e->size;
    info->compressed_size = e->comp_size;
    info->method = e->method;
    info->encrypted = (e->flags & FLAG_ENCRYPTED) != 0;
    info->is_dir = g_str_has_suffix(e->name, "/");
    /* DOS date and time: local time, 2-second precision */
    guint year = 1980 + (e->dos_date >> 9);
    guint month = (e->dos_date >> 5) & 0x0F;
    guint day = e->dos_date & 0x1F;
    guint hour = e->dos_time >> 11;
    guint minute = (e->dos_time >> 5) & 0x3F;
    info->mtime = -1;
    if (month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour < 24 && minute < 60) {
        GDateTime* dt = g_date_time_new_utc((gint)year, (gint)month, (gint)day, (gint)hour,
                                            (gint)minute, (e->dos_time & 0x1F) * 2.0);
        if (dt != NULL) {
            info->mtime = g_date_time_to_unix(dt);
            g_date_time_unref(dt);
        }
    }
    return TRUE;
}

gboolean zip_reader_has(const ZipReader* zip, const gchar* name) {
    return g_hash_table_contains(zip->by_name, name);
}

gint64 zip_reader_get_size(const ZipReader* zip, const gchar* name) {
    const ZipEntry* e = g_hash_table_lookup(zip->by_name, name);
    return e != NULL ? (gint64)e->size : -1;
}

/* Raw DEFLATE data -> exactly size bytes in out */
static gboolean inflate_raw(const guchar* in, gsize in_len, guchar* out, gsize size,
                            const gchar* name, GError** error) {
    GConverter* conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW));
    gsize in_pos = 0, out_pos = 0;
    gboolean ok = FALSE;
    for (;;) {
        gsize nread = 0, nwritten = 0;
        GError* err = NULL;
        /* One spare output byte so that data longer than announced is detected */
        GConverterResult res = g_converter_convert(conv, in + in_pos, in_len - in_pos,
                                                   out + out_pos, size + 1 - out_pos,
                                                   G_CONVERTER_INPUT_AT_END,
                                                   &nread, &nwritten, &err);
        in_pos += nread;
        out_pos += nwritten;
        if (res == G_CONVERTER_ERROR) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("Damaged ZIP entry %1$s: %2$s"), name, err->message);
            g_error_free(err);
            break;
        }
        if (out_pos > size) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("Damaged ZIP entry %s: more data than announced"), name);
            break;
        }
        if (res == G_CONVERTER_FINISHED) {
            ok = (out_pos == size);
            if (!ok) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            _("Damaged ZIP entry %s: less data than announced"), name);
            }
            break;
        }
        if (nread == 0 && nwritten == 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("Damaged ZIP entry %s: truncated data"), name);
            break;
        }
    }
    g_object_unref(conv);
    return ok;
}

GBytes* zip_reader_read(ZipReader* zip, const gchar* name, gsize max_size, GError** error) {
    g_return_val_if_fail(zip != NULL && name != NULL, NULL);
    const ZipEntry* e = g_hash_table_lookup(zip->by_name, name);
    if (e == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("No entry %s in the archive"), name);
        return NULL;
    }
    if (e->flags & FLAG_ENCRYPTED) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("ZIP entry %s is encrypted"), name);
        return NULL;
    }
    if (e->method != METHOD_STORED && e->method != METHOD_DEFLATED) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    _("ZIP entry %1$s uses an unsupported compression method (%2$u)"), name, e->method);
        return NULL;
    }
    if (e->size > max_size) {
        gchar* size = g_format_size(e->size);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE, _("ZIP entry %1$s is too large (%2$s)"), name, size);
        g_free(size);
        return NULL;
    }
    /* DEFLATE never grows data by more than a few bytes per 16 KB block */
    if ((e->method == METHOD_STORED && e->comp_size != e->size) ||
        (e->method == METHOD_DEFLATED && e->comp_size > e->size + e->size / 64 + 1024)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    _("Damaged ZIP entry %s: inconsistent sizes"), name);
        return NULL;
    }

    guchar local[LOCAL_HEADER_SIZE];
    if (!read_at(zip, e->local_offset, local, sizeof local, error)) {
        return NULL;
    }
    if (memcmp(local, "PK\x03\x04", 4) != 0) {
        set_invalid(error, _("bad local file header"));
        return NULL;
    }
    guint64 data_offset = e->local_offset + LOCAL_HEADER_SIZE + get_u16(local + 26) + get_u16(local + 28);

    gsize size = (gsize)e->size;
    guchar* out = g_malloc(size + 2); /* room for the overflow check and the NUL */
    if (e->method == METHOD_STORED) {
        if (!read_at(zip, data_offset, out, size, error)) {
            g_free(out);
            return NULL;
        }
    } else {
        gsize comp_size = (gsize)e->comp_size;
        guchar* in = g_malloc(comp_size + 1);
        gboolean ok = read_at(zip, data_offset, in, comp_size, error) &&
                      inflate_raw(in, comp_size, out, size, name, error);
        g_free(in);
        if (!ok) {
            g_free(out);
            return NULL;
        }
    }
    out[size] = '\0';
    /* The NUL stays readable past the end of the GBytes data */
    return g_bytes_new_take(out, size);
}
