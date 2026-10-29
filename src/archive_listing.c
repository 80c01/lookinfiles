/* Archive listings for the preview (see archive_listing.h). */
#include "archive_listing.h"

#include <stdlib.h>
#include <string.h>

#include "zip_reader.h"

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#define TAR_BLOCK 512
/* A compressed tar is read for at most this long / this many bytes */
#define GZIP_TAR_MAX_US (120 * 1000)
#define GZIP_TAR_MAX_BYTES (256u * 1024 * 1024)

static gboolean has(const guchar* head, gsize len, gsize off, const gchar* magic, gsize mlen) {
    return len >= off + mlen && memcmp(head + off, magic, mlen) == 0;
}
#define HAS(off, lit) has(head, len, (off), (lit), sizeof(lit) - 1)

/* Header checksum of a tar block (ustar or old V7 tar) */
static gboolean tar_checksum_ok(const guchar* block) {
    gchar field[9];
    memcpy(field, block + 148, 8);
    field[8] = '\0';
    gchar* end = NULL;
    glong expected = strtol(field, &end, 8);
    if (end == field) {
        return FALSE;
    }
    glong sum = 0;
    for (gint i = 0; i < TAR_BLOCK; i++) {
        sum += (i >= 148 && i < 156) ? ' ' : block[i];
    }
    return sum == expected;
}

static gboolean is_tar(const guchar* head, gsize len) {
    return len >= TAR_BLOCK && (HAS(257, "ustar") || tar_checksum_ok(head));
}

const gchar* archive_listing_format_name(const guchar* head, gsize len) {
    if (HAS(0, "PK\x03\x04") || HAS(0, "PK\x05\x06")) return "ZIP archive";
    if (HAS(0, "\x1F\x8B")) return "gzip-compressed file";
    if (HAS(0, "BZh")) return "bzip2-compressed file";
    if (HAS(0, "\xFD" "7zXZ")) return "xz-compressed file";
    if (HAS(0, "\x28\xB5\x2F\xFD")) return "Zstandard-compressed file";
    if (HAS(0, "LZIP")) return "lzip-compressed file";
    if (HAS(0, "7z\xBC\xAF\x27\x1C")) return "7-Zip archive";
    if (HAS(0, "Rar!\x1A\x07")) return "RAR archive";
    if (HAS(0, "!<arch>\n")) return "ar archive";
    if (HAS(0, "\xED\xAB\xEE\xDB")) return "RPM package";
    if (HAS(0, "MSCF")) return "Cabinet archive";
    if (is_tar(head, len)) return "tar archive";
    return NULL;
}

gboolean archive_listing_can_list(const guchar* head, gsize len) {
#ifdef HAVE_LIBARCHIVE
    if (archive_listing_format_name(head, len) != NULL || HAS(0, "\xED\xAB\xEE\xDB") || HAS(0, "MSCF")) {
        return TRUE;
    }
#endif
    return HAS(0, "PK\x03\x04") || HAS(0, "PK\x05\x06") || HAS(0, "\x1F\x8B") ||
           HAS(0, "!<arch>\n") || is_tar(head, len);
}
#undef HAS

static void entry_clear(gpointer data) {
    ArchiveEntry* e = data;
    g_free(e->path);
    g_free(e->link);
}

static ArchiveListing* listing_new(const gchar* format) {
    ArchiveListing* l = g_new0(ArchiveListing, 1);
    l->format = format;
    l->entries = g_array_new(FALSE, TRUE, sizeof(ArchiveEntry));
    g_array_set_clear_func(l->entries, entry_clear);
    l->inner_size = -1;
    return l;
}

void archive_listing_free(ArchiveListing* listing) {
    if (listing == NULL) {
        return;
    }
    g_array_free(listing->entries, TRUE);
    g_free(listing->inner_name);
    g_free(listing->manifest);
    if (listing->inner_head != NULL) {
        g_bytes_unref(listing->inner_head);
    }
    g_free(listing);
}

/* Count an entry, and keep it while there is room (takes path and link) */
static void add_entry(ArchiveListing* l, guint max_entries, gchar* path, ArchiveEntryType type,
                      guint64 size, gint64 packed, gint64 mtime, gchar* link, gboolean encrypted) {
    l->n_entries++;
    if (type == ARCHIVE_ENTRY_DIR) {
        l->n_dirs++;
    } else {
        l->n_files++;
        l->total_size += size;
        if (packed >= 0) {
            l->total_packed += (guint64)packed;
        }
    }
    if (l->entries->len < max_entries) {
        ArchiveEntry e = { path, type, size, packed, mtime, link, encrypted };
        g_array_append_val(l->entries, e);
    } else {
        g_free(path);
        g_free(link);
    }
}

/* --- ZIP --- */

/* Main attributes of a JAR manifest (the lines before the first blank line) */
static gchar* read_manifest(ZipReader* zip) {
    GBytes* data = zip_reader_read(zip, "META-INF/MANIFEST.MF", 64 * 1024, NULL);
    if (data == NULL) {
        return NULL;
    }
    gchar* text = g_utf8_make_valid(g_bytes_get_data(data, NULL), (gssize)g_bytes_get_size(data));
    g_bytes_unref(data);
    GString* out = g_string_new(NULL);
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        g_strchomp(lines[i]);
        if (lines[i][0] == '\0') {
            break;
        }
        if (lines[i][0] == ' ' && out->len > 0) {
            g_string_append(out, lines[i] + 1); /* continuation line */
            continue;
        }
        if (out->len > 0) {
            g_string_append_c(out, '\n');
        }
        g_string_append(out, lines[i]);
    }
    g_strfreev(lines);
    g_free(text);
    if (out->len == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

static ArchiveListing* list_zip(const gchar* path, guint max_entries, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    ArchiveListing* l = listing_new("ZIP archive");
    guint n = zip_reader_get_n_entries(zip);
    for (guint i = 0; i < n; i++) {
        ZipEntryInfo info;
        if (!zip_reader_get_info(zip, i, &info)) {
            break;
        }
        add_entry(l, max_entries, g_strdup(info.name),
                  info.is_dir ? ARCHIVE_ENTRY_DIR : ARCHIVE_ENTRY_FILE, info.size,
                  (gint64)info.compressed_size, info.mtime, NULL, info.encrypted);
    }
    l->manifest = read_manifest(zip);
    zip_reader_free(zip);
    return l;
}

/* --- tar --- */

/* Octal (or GNU base-256) number field */
static guint64 tar_number(const guchar* field, gsize len) {
    if (field[0] & 0x80) {
        guint64 v = field[0] & 0x7F;
        for (gsize i = 1; i < len; i++) {
            v = (v << 8) | field[i];
        }
        return v;
    }
    guint64 v = 0;
    gsize i = 0;
    while (i < len && (field[i] == ' ' || field[i] == '\0')) {
        i++;
    }
    for (; i < len && field[i] >= '0' && field[i] <= '7'; i++) {
        v = v * 8 + (guint64)(field[i] - '0');
    }
    return v;
}

static gchar* tar_string(const guchar* field, gsize len) {
    gsize n = strnlen((const gchar*)field, len);
    return g_utf8_make_valid((const gchar*)field, (gssize)n);
}

/* Value of a pax extended header record ("27 path=dir/name\n") */
static gchar* pax_value(const gchar* data, gsize len, const gchar* key) {
    gsize pos = 0;
    gsize klen = strlen(key);
    while (pos < len) {
        gchar* end = NULL;
        gulong rec = strtoul(data + pos, &end, 10);
        if (end == data + pos || rec == 0 || pos + rec > len) {
            return NULL;
        }
        const gchar* kv = end + 1;
        const gchar* rec_end = data + pos + rec;
        if ((gsize)(rec_end - kv) > klen + 1 && memcmp(kv, key, klen) == 0 && kv[klen] == '=') {
            const gchar* v = kv + klen + 1;
            gsize vlen = (gsize)(rec_end - v);
            if (vlen > 0 && v[vlen - 1] == '\n') {
                vlen--;
            }
            return g_utf8_make_valid(v, (gssize)vlen);
        }
        pos += rec;
    }
    return NULL;
}

static gboolean read_block(GInputStream* in, guchar* buf, GCancellable* cancellable, GError** error) {
    gsize got = 0;
    if (!g_input_stream_read_all(in, buf, TAR_BLOCK, &got, cancellable, error)) {
        return FALSE;
    }
    return got == TAR_BLOCK;
}

/* Skip n bytes: seek on files, read through on decompressed streams (their
 * skip() loses data) */
static gboolean skip_bytes(GInputStream* in, guint64 n, GCancellable* cancellable) {
    if (G_IS_SEEKABLE(in) && g_seekable_can_seek(G_SEEKABLE(in))) {
        return g_seekable_seek(G_SEEKABLE(in), (goffset)n, G_SEEK_CUR, cancellable, NULL);
    }
    guchar buf[16384];
    while (n > 0) {
        gssize k = g_input_stream_read(in, buf, (gsize)MIN(n, (guint64)sizeof(buf)), cancellable, NULL);
        if (k <= 0) {
            return FALSE;
        }
        n -= (guint64)k;
    }
    return TRUE;
}

/* Read the content of a special entry (long name, pax header), bounded */
static gchar* read_entry_data(GInputStream* in, guint64 size, GCancellable* cancellable) {
    guint64 padded = (size + TAR_BLOCK - 1) / TAR_BLOCK * TAR_BLOCK;
    if (size > 1024 * 1024) {
        skip_bytes(in, padded, cancellable);
        return NULL;
    }
    gchar* data = g_malloc((gsize)padded + 1);
    gsize got = 0;
    if (!g_input_stream_read_all(in, data, (gsize)padded, &got, cancellable, NULL) || got != padded) {
        g_free(data);
        return NULL;
    }
    data[size] = '\0';
    return data;
}

static void list_tar_stream(ArchiveListing* l, GInputStream* in, guint max_entries,
                            gboolean bounded, GCancellable* cancellable) {
    guchar block[TAR_BLOCK];
    gchar* long_name = NULL;
    gchar* long_link = NULL;
    gchar* pax_path = NULL;
    gint64 start = g_get_monotonic_time();
    guint64 read = 0;
    gint zero_blocks = 0;
    while (read_block(in, block, cancellable, NULL)) {
        read += TAR_BLOCK;
        if (block[0] == '\0') {
            if (++zero_blocks == 2) {
                break; /* end of archive */
            }
            continue;
        }
        zero_blocks = 0;
        if (!tar_checksum_ok(block)) {
            break; /* damaged or not a tar header */
        }
        guint64 size = tar_number(block + 124, 12);
        guint64 padded = (size + TAR_BLOCK - 1) / TAR_BLOCK * TAR_BLOCK;
        gchar flag = (gchar)block[156];
        if (flag == 'L' || flag == 'K' || flag == 'x' || flag == 'g') {
            gchar* data = read_entry_data(in, size, cancellable);
            read += padded;
            if (flag == 'L') {
                g_free(long_name);
                long_name = data != NULL ? g_utf8_make_valid(data, -1) : NULL;
            } else if (flag == 'K') {
                g_free(long_link);
                long_link = data != NULL ? g_utf8_make_valid(data, -1) : NULL;
            } else if (flag == 'x' && data != NULL) {
                g_free(pax_path);
                pax_path = pax_value(data, (gsize)size, "path");
            }
            g_free(data);
            continue;
        }

        gchar* path;
        if (pax_path != NULL) {
            path = pax_path;
            pax_path = NULL;
        } else if (long_name != NULL) {
            path = long_name;
            long_name = NULL;
        } else {
            gchar* name = tar_string(block, 100);
            gchar* prefix = has(block, TAR_BLOCK, 257, "ustar", 5) ? tar_string(block + 345, 155) : g_strdup("");
            path = prefix[0] != '\0' ? g_strconcat(prefix, "/", name, NULL) : g_strdup(name);
            g_free(prefix);
            g_free(name);
        }
        gchar* link = long_link != NULL ? long_link : NULL;
        long_link = NULL;
        ArchiveEntryType type = ARCHIVE_ENTRY_FILE;
        if (flag == '5' || g_str_has_suffix(path, "/")) {
            type = ARCHIVE_ENTRY_DIR;
        } else if (flag == '2' || flag == '1') {
            type = flag == '2' ? ARCHIVE_ENTRY_SYMLINK : ARCHIVE_ENTRY_HARDLINK;
            if (link == NULL) {
                link = tar_string(block + 157, 100);
            }
        } else if (flag == '3' || flag == '4' || flag == '6') {
            type = ARCHIVE_ENTRY_OTHER;
        }
        gint64 mtime = (gint64)tar_number(block + 136, 12);
        add_entry(l, max_entries, path, type, type == ARCHIVE_ENTRY_FILE ? size : 0, -1, mtime,
                  link, FALSE);
        if (padded > 0 && !skip_bytes(in, padded, cancellable)) {
            break;
        }
        read += padded;
        if (bounded && (g_get_monotonic_time() - start > GZIP_TAR_MAX_US || read > GZIP_TAR_MAX_BYTES)) {
            l->incomplete = TRUE;
            break;
        }
        if (g_cancellable_is_cancelled(cancellable)) {
            break;
        }
    }
    g_free(long_name);
    g_free(long_link);
    g_free(pax_path);
}

/* --- gzip --- */

/* Original name from the gzip header (FNAME), or NULL */
static gchar* gzip_name(const guchar* head, gsize len) {
    if (len < 10 || !(head[3] & 0x08)) {
        return NULL;
    }
    gsize pos = 10;
    if (head[3] & 0x04) { /* FEXTRA */
        if (pos + 2 > len) {
            return NULL;
        }
        pos += 2 + (gsize)(head[pos] | (head[pos + 1] << 8));
    }
    if (pos >= len) {
        return NULL;
    }
    gsize n = strnlen((const gchar*)head + pos, len - pos);
    return pos + n < len ? g_utf8_make_valid((const gchar*)head + pos, (gssize)n) : NULL;
}

/* Original size from the gzip trailer (modulo 4 GiB) */
static gint64 gzip_size(GFileInputStream* file) {
    guchar tail[4];
    if (!g_seekable_seek(G_SEEKABLE(file), -4, G_SEEK_END, NULL, NULL)) {
        return -1;
    }
    gsize got = 0;
    if (!g_input_stream_read_all(G_INPUT_STREAM(file), tail, 4, &got, NULL, NULL) || got != 4) {
        return -1;
    }
    g_seekable_seek(G_SEEKABLE(file), 0, G_SEEK_SET, NULL, NULL);
    return (gint64)((guint32)tail[0] | ((guint32)tail[1] << 8) | ((guint32)tail[2] << 16) |
                    ((guint32)tail[3] << 24));
}

static ArchiveListing* list_gzip(GFileInputStream* file, const guchar* head, gsize head_len,
                                 guint max_entries, GCancellable* cancellable, GError** error) {
    gint64 inner_size = gzip_size(file);
    GZlibDecompressor* dec = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP);
    GInputStream* in = g_converter_input_stream_new(G_INPUT_STREAM(file), G_CONVERTER(dec));
    g_object_unref(dec);
    /* The file is read again from the start for a tar */
    g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(in), FALSE);
    guchar* first = g_malloc(ARCHIVE_INNER_HEAD);
    gsize got = 0;
    GError* err = NULL;
    g_input_stream_read_all(in, first, ARCHIVE_INNER_HEAD, &got, cancellable, &err);
    if (err != NULL && got == 0) {
        g_propagate_error(error, err);
        g_free(first);
        g_object_unref(in);
        return NULL;
    }
    g_clear_error(&err);

    ArchiveListing* l;
    if (got >= TAR_BLOCK && is_tar(first, got)) {
        l = listing_new("gzip-compressed tar archive");
        /* Restart from the beginning of the decompressed data */
        g_object_unref(in);
        g_seekable_seek(G_SEEKABLE(file), 0, G_SEEK_SET, NULL, NULL);
        dec = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP);
        in = g_converter_input_stream_new(G_INPUT_STREAM(file), G_CONVERTER(dec));
        g_object_unref(dec);
        list_tar_stream(l, in, max_entries, TRUE, cancellable);
        g_free(first);
    } else {
        l = listing_new("gzip-compressed file");
        l->single = TRUE;
        l->inner_name = gzip_name(head, head_len);
        l->inner_size = inner_size;
        l->inner_head = g_bytes_new_take(first, got);
    }
    g_object_unref(in);
    return l;
}

/* --- ar --- */

static ArchiveListing* list_ar(GInputStream* in, guint max_entries, GCancellable* cancellable) {
    ArchiveListing* l = listing_new("ar archive");
    skip_bytes(in, 8, cancellable);
    gchar* names = NULL; /* GNU long name table ("//") */
    gsize names_len = 0;
    guchar h[60];
    for (;;) {
        gsize got = 0;
        if (!g_input_stream_read_all(in, h, sizeof(h), &got, cancellable, NULL) || got != sizeof(h) ||
            h[58] != '`' || h[59] != '\n') {
            break;
        }
        gchar field[17];
        memcpy(field, h + 48, 10);
        field[10] = '\0';
        guint64 size = g_ascii_strtoull(field, NULL, 10);
        memcpy(field, h + 16, 12);
        field[12] = '\0';
        gint64 mtime = (gint64)g_ascii_strtoll(field, NULL, 10);
        memcpy(field, h, 16);
        field[16] = '\0';
        g_strchomp(field);
        guint64 data_size = size + (size & 1);
        gchar* name = NULL;
        if (strcmp(field, "/") == 0 || strcmp(field, "/SYM64/") == 0 || strcmp(field, "__.SYMDEF") == 0) {
            skip_bytes(in, data_size, cancellable); /* symbol table */
            continue;
        }
        if (strcmp(field, "//") == 0) {
            g_free(names);
            names = g_malloc((gsize)MIN(size, 4u * 1024 * 1024) + 1);
            got = 0;
            g_input_stream_read_all(in, names, (gsize)MIN(size, 4u * 1024 * 1024), &got, cancellable, NULL);
            names[got] = '\0';
            names_len = got;
            skip_bytes(in, data_size - got, cancellable);
            continue;
        }
        if (field[0] == '/' && g_ascii_isdigit(field[1]) && names != NULL) {
            gsize off = (gsize)g_ascii_strtoull(field + 1, NULL, 10);
            if (off < names_len) {
                gsize n = strcspn(names + off, "/\n");
                name = g_utf8_make_valid(names + off, (gssize)n);
            }
        } else if (g_str_has_prefix(field, "#1/")) {
            /* BSD: the name is at the start of the data */
            gsize n = (gsize)g_ascii_strtoull(field + 3, NULL, 10);
            gchar* buf = g_malloc0(n + 1);
            got = 0;
            g_input_stream_read_all(in, buf, n, &got, cancellable, NULL);
            name = g_utf8_make_valid(buf, (gssize)strnlen(buf, got));
            g_free(buf);
            size -= MIN(size, n);
            data_size -= MIN(data_size, n);
        }
        if (name == NULL) {
            gsize n = strlen(field);
            if (n > 1 && field[n - 1] == '/') {
                field[n - 1] = '\0';
            }
            name = g_utf8_make_valid(field, -1);
        }
        add_entry(l, max_entries, name, ARCHIVE_ENTRY_FILE, size, -1, mtime, NULL, FALSE);
        if (!skip_bytes(in, data_size, cancellable) && data_size > 0) {
            break;
        }
    }
    g_free(names);
    return l;
}

#ifdef HAVE_LIBARCHIVE

/* "xz", "bzip2"... of the outer compression, NULL if none */
static const gchar* filter_label(struct archive* a) {
    for (int i = 0; i < archive_filter_count(a); i++) {
        switch (archive_filter_code(a, i)) {
        case ARCHIVE_FILTER_GZIP: return "gzip";
        case ARCHIVE_FILTER_BZIP2: return "bzip2";
        case ARCHIVE_FILTER_XZ: return "xz";
        case ARCHIVE_FILTER_LZMA: return "LZMA";
        case ARCHIVE_FILTER_LZIP: return "lzip";
        case ARCHIVE_FILTER_ZSTD: return "Zstandard";
        case ARCHIVE_FILTER_LZ4: return "LZ4";
        case ARCHIVE_FILTER_COMPRESS: return "compress";
        default: break;
        }
    }
    return NULL;
}

static const gchar* format_label(struct archive* a) {
    switch (archive_format(a) & ARCHIVE_FORMAT_BASE_MASK) {
    case ARCHIVE_FORMAT_TAR: return "tar archive";
    case ARCHIVE_FORMAT_CPIO: return "cpio archive";
    case ARCHIVE_FORMAT_ISO9660: return "ISO image";
    case ARCHIVE_FORMAT_ZIP: return "ZIP archive";
    case ARCHIVE_FORMAT_AR: return "ar archive";
    case ARCHIVE_FORMAT_7ZIP: return "7-Zip archive";
    case ARCHIVE_FORMAT_CAB: return "Cabinet archive";
    case ARCHIVE_FORMAT_RAR: return "RAR archive";
    case ARCHIVE_FORMAT_RAR_V5: return "RAR archive";
    case ARCHIVE_FORMAT_LHA: return "LHA archive";
    case ARCHIVE_FORMAT_XAR: return "xar archive";
    case ARCHIVE_FORMAT_WARC: return "WARC archive";
    default: return "archive";
    }
}

/* Interned "xz-compressed tar archive" (the listing keeps a static string) */
static const gchar* compose_format(const gchar* filter, const gchar* format) {
    if (filter == NULL) {
        return g_intern_string(format);
    }
    gchar* name = g_strdup_printf("%s-compressed %s", filter, format);
    const gchar* interned = g_intern_string(name);
    g_free(name);
    return interned;
}

static struct archive* open_with(const gchar* path, gboolean raw) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    if (raw) {
        archive_read_support_format_raw(a);
    } else {
        archive_read_support_format_all(a);
    }
    if (archive_read_open_filename(a, path, 64 * 1024) != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }
    return a;
}

static ArchiveListing* list_libarchive(const gchar* path, guint max_entries, GCancellable* cancellable,
                                       GError** error) {
    struct archive* a = open_with(path, FALSE);
    struct archive_entry* entry = NULL;
    int rc = a != NULL ? archive_read_next_header(a, &entry) : ARCHIVE_FATAL;
    if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
        /* Not an archive format: maybe a single compressed file */
        if (a != NULL) {
            archive_read_free(a);
        }
        a = open_with(path, TRUE);
        if (a == NULL || archive_read_next_header(a, &entry) != ARCHIVE_OK || filter_label(a) == NULL) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s",
                        a != NULL && archive_error_string(a) != NULL ? archive_error_string(a)
                                                                     : "unknown archive format");
            if (a != NULL) {
                archive_read_free(a);
            }
            return NULL;
        }
        ArchiveListing* l = listing_new(compose_format(filter_label(a), "file"));
        l->single = TRUE;
        guchar* data = g_malloc(ARCHIVE_INNER_HEAD);
        gsize got = 0;
        while (got < ARCHIVE_INNER_HEAD) {
            la_ssize_t n = archive_read_data(a, data + got, ARCHIVE_INNER_HEAD - got);
            if (n <= 0) {
                break;
            }
            got += (gsize)n;
        }
        l->inner_head = g_bytes_new_take(data, got);
        archive_read_free(a);
        return l;
    }

    ArchiveListing* l = listing_new("archive");
    gint64 start = g_get_monotonic_time();
    while (rc == ARCHIVE_OK || rc == ARCHIVE_WARN) {
        const gchar* name = archive_entry_pathname_utf8(entry);
        if (name == NULL) {
            name = archive_entry_pathname(entry);
        }
        mode_t type = archive_entry_filetype(entry);
        ArchiveEntryType t = type == AE_IFDIR ? ARCHIVE_ENTRY_DIR
                             : type == AE_IFLNK ? ARCHIVE_ENTRY_SYMLINK
                             : type == AE_IFREG ? ARCHIVE_ENTRY_FILE : ARCHIVE_ENTRY_OTHER;
        if (archive_entry_hardlink(entry) != NULL) {
            t = ARCHIVE_ENTRY_HARDLINK;
        }
        const gchar* target = t == ARCHIVE_ENTRY_SYMLINK ? archive_entry_symlink(entry)
                              : t == ARCHIVE_ENTRY_HARDLINK ? archive_entry_hardlink(entry) : NULL;
        gchar* path_utf8 = g_utf8_make_valid(name != NULL ? name : "", -1);
        add_entry(l, max_entries, path_utf8, t, t == ARCHIVE_ENTRY_FILE ? (guint64)archive_entry_size(entry) : 0,
                  -1, archive_entry_mtime_is_set(entry) ? (gint64)archive_entry_mtime(entry) : -1,
                  target != NULL ? g_utf8_make_valid(target, -1) : NULL, archive_entry_is_encrypted(entry));
        if (g_get_monotonic_time() - start > GZIP_TAR_MAX_US || g_cancellable_is_cancelled(cancellable)) {
            l->incomplete = TRUE;
            break;
        }
        archive_read_data_skip(a);
        rc = archive_read_next_header(a, &entry);
    }
    l->format = compose_format(filter_label(a), format_label(a));
    archive_read_free(a);
    return l;
}

#endif /* HAVE_LIBARCHIVE */

ArchiveListing* archive_list(const gchar* path, guint max_entries, GCancellable* cancellable,
                             GError** error) {
    GFile* file = g_file_new_for_path(path);
    GFileInputStream* stream = g_file_read(file, cancellable, error);
    g_object_unref(file);
    if (stream == NULL) {
        return NULL;
    }
    guchar head[TAR_BLOCK];
    gsize len = 0;
    if (!g_input_stream_read_all(G_INPUT_STREAM(stream), head, sizeof(head), &len, cancellable, error)) {
        g_object_unref(stream);
        return NULL;
    }
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    ArchiveListing* l = NULL;
    if (len >= 4 && (memcmp(head, "PK\x03\x04", 4) == 0 || memcmp(head, "PK\x05\x06", 4) == 0)) {
        g_object_unref(stream);
        return list_zip(path, max_entries, error);
    }
    if (len >= 2 && head[0] == 0x1F && head[1] == 0x8B) {
        l = list_gzip(stream, head, len, max_entries, cancellable, error);
    } else if (len >= 8 && memcmp(head, "!<arch>\n", 8) == 0) {
        l = list_ar(G_INPUT_STREAM(stream), max_entries, cancellable);
    } else if (is_tar(head, len)) {
        l = listing_new("tar archive");
        list_tar_stream(l, G_INPUT_STREAM(stream), max_entries, FALSE, cancellable);
    } else {
#ifdef HAVE_LIBARCHIVE
        g_object_unref(stream);
        return list_libarchive(path, max_entries, cancellable, error);
#endif
        const gchar* format = archive_listing_format_name(head, len);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s",
                    format != NULL ? format : "unknown archive format");
    }
    g_object_unref(stream);
    return l;
}
