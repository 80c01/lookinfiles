/* OLE2 compound file reader (see ole_reader.h) */
#include "ole_reader.h"
#include <gio/gio.h>
#include <string.h>

#define FREESECT 0xFFFFFFFFu
#define ENDOFCHAIN 0xFFFFFFFEu
#define NOSTREAM 0xFFFFFFFFu
#define MAX_DIR_ENTRIES 65536
#define MAX_DEPTH 32

typedef struct {
    gchar* name;
    gchar* path;
    guint8 type;
    guint32 left, right, child;
    guint32 start;
    guint64 size;
    guint8 clsid[16];
    gint depth;
} DirEntry;

struct OleReader {
    GBytes* data;
    GMappedFile* map;
    const guint8* buf;
    gsize len;
    guint shift;            /* sector size = 1 << shift */
    gsize ssz;
    guint32 n_sectors;      /* sectors present in the file */
    guint mini_shift;
    guint32 mini_cutoff;
    guint32* fat;
    guint32 n_fat;
    guint32* minifat;
    guint32 n_minifat;
    DirEntry* dir;
    guint32 n_dir;
    GArray* order;          /* guint32 indexes of dir, tree order, root excluded */
    GBytes* ministream;     /* read on first use */
};

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static gboolean fail(GError** error, const gchar* what) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "damaged compound file: %s", what);
    return FALSE;
}

/* Pointer to sector s, or NULL when it is outside the file */
static const guint8* sector(const OleReader* ole, guint32 s) {
    if (s >= ole->n_sectors) return NULL;
    gsize off = ((gsize)s + 1) << ole->shift;
    if (off + ole->ssz > ole->len) return NULL;
    return ole->buf + off;
}

/* Bytes of a regular chain, at most limit bytes (NULL when damaged) */
static GByteArray* read_chain(const OleReader* ole, guint32 start, gsize limit, GError** error) {
    GByteArray* out = g_byte_array_new();
    guint8* seen = g_new0(guint8, ole->n_fat / 8 + 1);
    guint32 s = start;
    while (s != ENDOFCHAIN && out->len < limit) {
        const guint8* p = s < ole->n_fat ? sector(ole, s) : NULL;
        gboolean loop = p != NULL && (seen[s / 8] & (1u << (s % 8))) != 0;
        if (p == NULL || loop) {
            g_byte_array_free(out, TRUE);
            g_free(seen);
            fail(error, p == NULL ? "sector out of range" : "loop in a sector chain");
            return NULL;
        }
        seen[s / 8] |= (guint8)(1u << (s % 8));
        g_byte_array_append(out, p, (guint)MIN(ole->ssz, limit - out->len));
        s = ole->fat[s];
    }
    g_free(seen);
    return out;
}

static gboolean load_fat(OleReader* ole, const guint8* h, GError** error) {
    guint32 n_fat_sectors = u32(h + 0x2C);
    guint32 difat_start = u32(h + 0x44);
    guint32 n_difat = u32(h + 0x48);
    if (n_fat_sectors == 0 || n_fat_sectors > ole->n_sectors) return fail(error, "bad FAT size");
    guint32 per = (guint32)(ole->ssz / 4);
    guint32* fat_sectors = g_new(guint32, n_fat_sectors);
    guint32 k = 0;
    for (guint32 i = 0; i < 109 && k < n_fat_sectors; i++) fat_sectors[k++] = u32(h + 0x4C + i * 4);
    guint32 d = difat_start;
    for (guint32 i = 0; i < n_difat && k < n_fat_sectors; i++) {
        const guint8* p = sector(ole, d);
        if (p == NULL) {
            g_free(fat_sectors);
            return fail(error, "DIFAT sector out of range");
        }
        for (guint32 j = 0; j < per - 1 && k < n_fat_sectors; j++) fat_sectors[k++] = u32(p + j * 4);
        d = u32(p + (per - 1) * 4);
    }
    if (k < n_fat_sectors) {
        g_free(fat_sectors);
        return fail(error, "incomplete DIFAT");
    }
    ole->n_fat = n_fat_sectors * per;
    ole->fat = g_new(guint32, ole->n_fat);
    for (guint32 i = 0; i < n_fat_sectors; i++) {
        const guint8* p = sector(ole, fat_sectors[i]);
        if (p == NULL) {
            g_free(fat_sectors);
            return fail(error, "FAT sector out of range");
        }
        for (guint32 j = 0; j < per; j++) ole->fat[i * per + j] = u32(p + j * 4);
    }
    g_free(fat_sectors);
    return TRUE;
}

static gchar* utf16_name(const guint8* p, guint16 bytes) {
    if (bytes < 2 || bytes > 64) return g_strdup("");
    guint n = bytes / 2 - 1;
    gunichar2 w[32];
    for (guint i = 0; i < n; i++) w[i] = u16(p + i * 2);
    gchar* s = g_utf16_to_utf8(w, (glong)n, NULL, NULL, NULL);
    if (s == NULL) {
        GString* g = g_string_new(NULL);
        for (guint i = 0; i < n; i++) g_string_append_c(g, w[i] < 0x80 ? (gchar)w[i] : '?');
        s = g_string_free(g, FALSE);
    }
    return s;
}

static gboolean load_dir(OleReader* ole, guint32 start, GError** error) {
    GByteArray* raw = read_chain(ole, start, (gsize)MAX_DIR_ENTRIES * 128, error);
    if (raw == NULL) return FALSE;
    ole->n_dir = raw->len / 128;
    if (ole->n_dir == 0) {
        g_byte_array_free(raw, TRUE);
        return fail(error, "empty directory");
    }
    ole->dir = g_new0(DirEntry, ole->n_dir);
    for (guint32 i = 0; i < ole->n_dir; i++) {
        const guint8* e = raw->data + i * 128;
        DirEntry* d = &ole->dir[i];
        d->type = e[0x42];
        d->name = utf16_name(e, u16(e + 0x40));
        d->left = u32(e + 0x44);
        d->right = u32(e + 0x48);
        d->child = u32(e + 0x4C);
        memcpy(d->clsid, e + 0x50, 16);
        d->start = u32(e + 0x74);
        d->size = (guint64)u32(e + 0x78);
        if (ole->shift == 12) d->size |= (guint64)u32(e + 0x7C) << 32;
    }
    g_byte_array_free(raw, TRUE);
    if (ole->dir[0].type != OLE_ENTRY_ROOT) return fail(error, "no root entry");
    return TRUE;
}

static gint compare_names(gconstpointer a, gconstpointer b, gpointer data) {
    const OleReader* ole = data;
    return g_ascii_strcasecmp(ole->dir[*(const guint32*)a].name, ole->dir[*(const guint32*)b].name);
}

/* Children of a storage (the nodes of its sibling tree), sorted by name */
static GArray* children_of(const OleReader* ole, guint32 child, guint8* seen) {
    GArray* out = g_array_new(FALSE, FALSE, sizeof(guint32));
    GArray* stack = g_array_new(FALSE, FALSE, sizeof(guint32));
    if (child != NOSTREAM) g_array_append_val(stack, child);
    while (stack->len > 0) {
        guint32 i = g_array_index(stack, guint32, stack->len - 1);
        g_array_set_size(stack, stack->len - 1);
        if (i >= ole->n_dir || seen[i]) continue; /* out of range or loop */
        seen[i] = 1;
        const DirEntry* d = &ole->dir[i];
        if (d->type == OLE_ENTRY_STORAGE || d->type == OLE_ENTRY_STREAM) g_array_append_val(out, i);
        if (d->left != NOSTREAM) g_array_append_val(stack, d->left);
        if (d->right != NOSTREAM) g_array_append_val(stack, d->right);
    }
    g_array_free(stack, TRUE);
    g_array_sort_with_data(out, compare_names, (gpointer)ole);
    return out;
}

static void walk(OleReader* ole, guint32 child, const gchar* prefix, gint depth, guint8* seen) {
    if (depth > MAX_DEPTH) return;
    GArray* kids = children_of(ole, child, seen);
    for (guint k = 0; k < kids->len; k++) {
        guint32 i = g_array_index(kids, guint32, k);
        DirEntry* d = &ole->dir[i];
        d->path = prefix != NULL ? g_strdup_printf("%s/%s", prefix, d->name) : g_strdup(d->name);
        d->depth = depth;
        g_array_append_val(ole->order, i);
        if (d->type == OLE_ENTRY_STORAGE) walk(ole, d->child, d->path, depth + 1, seen);
    }
    g_array_free(kids, TRUE);
}

static OleReader* open_buffer(OleReader* ole, GError** error) {
    const guint8* h = ole->buf;
    if (ole->len < 512 || memcmp(h, OLE_MAGIC, 8) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a compound file");
        ole_reader_free(ole);
        return NULL;
    }
    guint shift = u16(h + 0x1E);
    if (u16(h + 0x1C) != 0xFFFE || (shift != 9 && shift != 12) || u16(h + 0x20) != 6) {
        fail(error, "unsupported header");
        ole_reader_free(ole);
        return NULL;
    }
    ole->shift = shift;
    ole->ssz = (gsize)1 << shift;
    ole->mini_shift = 6;
    ole->mini_cutoff = u32(h + 0x38);
    if (ole->mini_cutoff == 0 || ole->mini_cutoff > 0x10000) ole->mini_cutoff = 4096;
    ole->n_sectors = (guint32)MIN((ole->len - ole->ssz + ole->ssz - 1) / ole->ssz, (gsize)G_MAXUINT32 - 1);
    if (ole->len <= ole->ssz) ole->n_sectors = 0;
    if (!load_fat(ole, h, error) || !load_dir(ole, u32(h + 0x30), error)) {
        ole_reader_free(ole);
        return NULL;
    }
    /* Mini FAT */
    guint32 minifat_start = u32(h + 0x3C);
    if (minifat_start != ENDOFCHAIN && minifat_start != FREESECT) {
        GByteArray* raw = read_chain(ole, minifat_start, (gsize)u32(h + 0x40) * ole->ssz, error);
        if (raw == NULL) {
            ole_reader_free(ole);
            return NULL;
        }
        ole->n_minifat = raw->len / 4;
        ole->minifat = g_new(guint32, MAX(ole->n_minifat, 1));
        for (guint32 i = 0; i < ole->n_minifat; i++) ole->minifat[i] = u32(raw->data + i * 4);
        g_byte_array_free(raw, TRUE);
    }
    ole->order = g_array_new(FALSE, FALSE, sizeof(guint32));
    guint8* seen = g_new0(guint8, ole->n_dir);
    seen[0] = 1;
    walk(ole, ole->dir[0].child, NULL, 0, seen);
    g_free(seen);
    return ole;
}

OleReader* ole_reader_open(const gchar* path, GError** error) {
    GMappedFile* map = g_mapped_file_new(path, FALSE, error);
    if (map == NULL) return NULL;
    OleReader* ole = g_new0(OleReader, 1);
    ole->map = map;
    ole->buf = (const guint8*)g_mapped_file_get_contents(map);
    ole->len = g_mapped_file_get_length(map);
    return open_buffer(ole, error);
}

OleReader* ole_reader_new_from_bytes(GBytes* data, GError** error) {
    OleReader* ole = g_new0(OleReader, 1);
    ole->data = g_bytes_ref(data);
    gsize len = 0;
    ole->buf = g_bytes_get_data(data, &len);
    ole->len = len;
    return open_buffer(ole, error);
}

void ole_reader_free(OleReader* ole) {
    if (ole == NULL) return;
    for (guint32 i = 0; i < ole->n_dir; i++) {
        g_free(ole->dir[i].name);
        g_free(ole->dir[i].path);
    }
    g_free(ole->dir);
    g_free(ole->fat);
    g_free(ole->minifat);
    if (ole->order != NULL) g_array_free(ole->order, TRUE);
    if (ole->ministream != NULL) g_bytes_unref(ole->ministream);
    if (ole->map != NULL) g_mapped_file_unref(ole->map);
    if (ole->data != NULL) g_bytes_unref(ole->data);
    g_free(ole);
}

guint ole_reader_get_n_entries(const OleReader* ole) {
    return ole != NULL ? ole->order->len : 0;
}

gboolean ole_reader_get_info(const OleReader* ole, guint i, OleEntryInfo* info) {
    if (ole == NULL || i >= ole->order->len) return FALSE;
    const DirEntry* d = &ole->dir[g_array_index(ole->order, guint32, i)];
    info->name = d->name;
    info->path = d->path;
    info->type = (OleEntryType)d->type;
    info->size = d->type == OLE_ENTRY_STREAM ? d->size : 0;
    info->depth = d->depth;
    return TRUE;
}

gint ole_reader_find(const OleReader* ole, const gchar* path) {
    if (ole == NULL || path == NULL) return -1;
    for (guint i = 0; i < ole->order->len; i++) {
        if (g_ascii_strcasecmp(ole->dir[g_array_index(ole->order, guint32, i)].path, path) == 0) return (gint)i;
    }
    return -1;
}

gboolean ole_reader_has(const OleReader* ole, const gchar* path) {
    return ole_reader_find(ole, path) >= 0;
}

const guint8* ole_reader_get_root_clsid(const OleReader* ole) {
    if (ole == NULL) return NULL;
    for (guint i = 0; i < 16; i++) {
        if (ole->dir[0].clsid[i] != 0) return ole->dir[0].clsid;
    }
    return NULL;
}

static gboolean load_ministream(OleReader* ole, GError** error) {
    if (ole->ministream != NULL) return TRUE;
    const DirEntry* root = &ole->dir[0];
    GByteArray* raw = read_chain(ole, root->start, (gsize)MIN(root->size, (guint64)G_MAXUINT32), error);
    if (raw == NULL) return FALSE;
    ole->ministream = g_byte_array_free_to_bytes(raw);
    return TRUE;
}

GBytes* ole_reader_read_entry(OleReader* ole, guint i, gsize max_size, GError** error) {
    if (ole == NULL || i >= ole->order->len) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no such stream");
        return NULL;
    }
    const DirEntry* d = &ole->dir[g_array_index(ole->order, guint32, i)];
    if (d->type != OLE_ENTRY_STREAM) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, "%s is a storage", d->name);
        return NULL;
    }
    if (d->size > max_size) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "%s is too large (%" G_GUINT64_FORMAT " bytes)", d->name,
                    d->size);
        return NULL;
    }
    gsize size = (gsize)d->size;
    GByteArray* out;
    if (size < ole->mini_cutoff) {
        if (!load_ministream(ole, error)) return NULL;
        gsize mlen = 0;
        const guint8* mini = g_bytes_get_data(ole->ministream, &mlen);
        out = g_byte_array_sized_new((guint)size + 1);
        guint32 s = d->start;
        gsize msz = (gsize)1 << ole->mini_shift;
        guint8* seen = g_new0(guint8, ole->n_minifat / 8 + 1);
        while (out->len < size) {
            gsize off = (gsize)s << ole->mini_shift;
            if (s == ENDOFCHAIN || s >= ole->n_minifat || off + msz > mlen || (seen[s / 8] & (1u << (s % 8))) != 0) {
                g_byte_array_free(out, TRUE);
                g_free(seen);
                fail(error, "mini stream chain");
                return NULL;
            }
            seen[s / 8] |= (guint8)(1u << (s % 8));
            g_byte_array_append(out, mini + off, (guint)MIN(msz, size - out->len));
            s = ole->minifat[s];
        }
        g_free(seen);
    } else {
        out = read_chain(ole, d->start, size, error);
        if (out == NULL) return NULL;
        if (out->len < size) {
            g_byte_array_free(out, TRUE);
            fail(error, "stream shorter than its size");
            return NULL;
        }
    }
    guint8 nul = 0;
    g_byte_array_append(out, &nul, 1);
    GBytes* whole = g_byte_array_free_to_bytes(out);
    GBytes* bytes = g_bytes_new_from_bytes(whole, 0, size);
    g_bytes_unref(whole);
    return bytes;
}

GBytes* ole_reader_read(OleReader* ole, const gchar* path, gsize max_size, GError** error) {
    gint i = ole_reader_find(ole, path);
    if (i < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no stream %s", path != NULL ? path : "");
        return NULL;
    }
    return ole_reader_read_entry(ole, (guint)i, max_size, error);
}
