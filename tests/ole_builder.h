/* Test helper: builds small OLE2 compound files in memory (version 3, 512
 * byte sectors): streams under the root or in storages ("Dir/Stream"),
 * short ones in the mini stream. The sibling trees are degenerate (right
 * links only), which readers must accept. Header-only, for tests/ only. */
#ifndef TEST_OLE_BUILDER_H
#define TEST_OLE_BUILDER_H

#include <glib.h>
#include <string.h>

#define OB_END 0xFFFFFFFEu
#define OB_FREE 0xFFFFFFFFu
#define OB_FATSECT 0xFFFFFFFDu
#define OB_NONE 0xFFFFFFFFu

typedef struct {
    gchar* name;
    gint parent;       /* index of the parent entry (0: root) */
    guint8 type;       /* 1 storage, 2 stream, 5 root */
    GBytes* data;
    guint32 start;
    guint32 child, right;
} ObEntry;

typedef struct {
    GPtrArray* entries; /* ObEntry* */
} OleBuilder;

static inline OleBuilder* ole_builder_new(void) {
    OleBuilder* ob = g_new0(OleBuilder, 1);
    ob->entries = g_ptr_array_new();
    ObEntry* root = g_new0(ObEntry, 1);
    root->name = g_strdup("Root Entry");
    root->type = 5;
    root->parent = -1;
    g_ptr_array_add(ob->entries, root);
    return ob;
}

static inline gint ob_find(OleBuilder* ob, const gchar* name, gint parent) {
    for (guint i = 1; i < ob->entries->len; i++) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        if (e->parent == parent && strcmp(e->name, name) == 0) return (gint)i;
    }
    return -1;
}

/* Add a stream at path ("Stream" or "Storage/Stream"), creating storages */
static inline void ole_builder_add(OleBuilder* ob, const gchar* path, const void* data, gsize len) {
    gchar** parts = g_strsplit(path, "/", -1);
    gint parent = 0;
    guint n = g_strv_length(parts);
    for (guint i = 0; i + 1 < n; i++) {
        gint s = ob_find(ob, parts[i], parent);
        if (s < 0) {
            ObEntry* st = g_new0(ObEntry, 1);
            st->name = g_strdup(parts[i]);
            st->type = 1;
            st->parent = parent;
            g_ptr_array_add(ob->entries, st);
            s = (gint)ob->entries->len - 1;
        }
        parent = s;
    }
    ObEntry* e = g_new0(ObEntry, 1);
    e->name = g_strdup(parts[n - 1]);
    e->type = 2;
    e->parent = parent;
    e->data = g_bytes_new(data, len);
    g_ptr_array_add(ob->entries, e);
    g_strfreev(parts);
}

static inline void ob_put16(guint8* p, guint v) {
    p[0] = (guint8)v;
    p[1] = (guint8)(v >> 8);
}

static inline void ob_put32(guint8* p, guint32 v) {
    p[0] = (guint8)v;
    p[1] = (guint8)(v >> 8);
    p[2] = (guint8)(v >> 16);
    p[3] = (guint8)(v >> 24);
}

/* The compound file (frees the builder) */
static inline GBytes* ole_builder_finish(OleBuilder* ob) {
    guint n_entries = ob->entries->len;
    /* Sibling chains (right links) and first children */
    for (guint i = 0; i < n_entries; i++) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        e->child = e->right = OB_NONE;
    }
    for (guint i = n_entries; i-- > 1;) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        ObEntry* p = g_ptr_array_index(ob->entries, (guint)e->parent);
        e->right = p->child;
        p->child = i;
    }
    /* Mini stream (streams < 4096 bytes) and regular streams */
    GByteArray* mini = g_byte_array_new();
    GArray* minifat = g_array_new(FALSE, FALSE, sizeof(guint32));
    guint n_regular_sectors = 0;
    for (guint i = 1; i < n_entries; i++) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        if (e->type != 2) continue;
        gsize len = g_bytes_get_size(e->data);
        if (len < 4096) {
            guint first = mini->len / 64;
            guint count = (guint)((len + 63) / 64);
            e->start = len > 0 ? first : OB_END;
            g_byte_array_append(mini, g_bytes_get_data(e->data, NULL), (guint)len);
            while (mini->len % 64 != 0) g_byte_array_append(mini, (const guint8*)"", 1);
            for (guint k = 0; k < count; k++) {
                guint32 next = k + 1 < count ? first + k + 1 : OB_END;
                g_array_append_val(minifat, next);
            }
        } else {
            n_regular_sectors += (guint)((len + 511) / 512);
        }
    }
    guint n_dir = (n_entries * 128 + 511) / 512;
    guint n_minifat = (minifat->len * 4 + 511) / 512;
    guint n_mini = (mini->len + 511) / 512;
    guint content = n_dir + n_minifat + n_mini + n_regular_sectors;
    guint n_fat = 1;
    while (n_fat * 128 < n_fat + content) n_fat++;
    guint total = n_fat + content;
    guint32* fat = g_new(guint32, n_fat * 128);
    for (guint i = 0; i < n_fat * 128; i++) fat[i] = OB_FREE;
    for (guint i = 0; i < n_fat; i++) fat[i] = OB_FATSECT;
    guint next = n_fat;
    /* Chains of consecutive sectors */
#define OB_CHAIN(first, count)                                                  \
    do {                                                                        \
        for (guint k_ = 0; k_ < (count); k_++)                                  \
            fat[(first) + k_] = k_ + 1 < (count) ? (first) + k_ + 1 : OB_END;   \
    } while (0)
    guint dir_start = next;
    OB_CHAIN(dir_start, n_dir);
    next += n_dir;
    guint minifat_start = n_minifat > 0 ? next : OB_END;
    OB_CHAIN(next, n_minifat);
    next += n_minifat;
    guint mini_start = n_mini > 0 ? next : OB_END;
    OB_CHAIN(next, n_mini);
    next += n_mini;
    ObEntry* root = g_ptr_array_index(ob->entries, 0);
    root->start = mini_start;
    for (guint i = 1; i < n_entries; i++) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        gsize len = e->type == 2 ? g_bytes_get_size(e->data) : 0;
        if (e->type != 2 || len < 4096) continue;
        guint count = (guint)((len + 511) / 512);
        e->start = next;
        OB_CHAIN(next, count);
        next += count;
    }
#undef OB_CHAIN
    GByteArray* out = g_byte_array_new();
    g_byte_array_set_size(out, (total + 1) * 512);
    memset(out->data, 0, out->len);
    guint8* h = out->data;
    memcpy(h, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
    ob_put16(h + 0x18, 0x3E);
    ob_put16(h + 0x1A, 3);
    ob_put16(h + 0x1C, 0xFFFE);
    ob_put16(h + 0x1E, 9);
    ob_put16(h + 0x20, 6);
    ob_put32(h + 0x2C, n_fat);
    ob_put32(h + 0x30, dir_start);
    ob_put32(h + 0x38, 4096);
    ob_put32(h + 0x3C, minifat_start);
    ob_put32(h + 0x40, n_minifat);
    ob_put32(h + 0x44, OB_END);
    ob_put32(h + 0x48, 0);
    for (guint i = 0; i < 109; i++) ob_put32(h + 0x4C + i * 4, i < n_fat ? i : OB_FREE);
#define OB_SECTOR(s) (out->data + ((gsize)(s) + 1) * 512)
    for (guint i = 0; i < n_fat * 128; i++) ob_put32(OB_SECTOR(i / 128) + (i % 128) * 4, fat[i]);
    for (guint i = 0; i < n_entries; i++) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        guint8* d = OB_SECTOR(dir_start) + i * 128;
        glong n16 = 0;
        gunichar2* w = g_utf8_to_utf16(e->name, -1, NULL, &n16, NULL);
        for (glong k = 0; k < n16 && k < 31; k++) ob_put16(d + k * 2, w[k]);
        g_free(w);
        ob_put16(d + 0x40, (guint)((MIN(n16, 31) + 1) * 2));
        d[0x42] = e->type;
        d[0x43] = 1; /* black */
        ob_put32(d + 0x44, OB_NONE);
        ob_put32(d + 0x48, e->right);
        ob_put32(d + 0x4C, e->child);
        ob_put32(d + 0x74, e->type == 1 ? 0 : e->start);
        ob_put32(d + 0x78, e->type == 2 ? (guint32)g_bytes_get_size(e->data) : e->type == 5 ? mini->len : 0);
    }
    for (guint i = 0; i < minifat->len; i++) {
        ob_put32(OB_SECTOR(minifat_start + i / 128) + (i % 128) * 4, g_array_index(minifat, guint32, i));
    }
    for (guint i = minifat->len; i < n_minifat * 128; i++) {
        ob_put32(OB_SECTOR(minifat_start + i / 128) + (i % 128) * 4, OB_FREE);
    }
    if (n_mini > 0) memcpy(OB_SECTOR(mini_start), mini->data, mini->len);
    for (guint i = 1; i < n_entries; i++) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        gsize len = e->type == 2 ? g_bytes_get_size(e->data) : 0;
        if (e->type == 2 && len >= 4096) memcpy(OB_SECTOR(e->start), g_bytes_get_data(e->data, NULL), len);
    }
#undef OB_SECTOR
    g_free(fat);
    g_array_free(minifat, TRUE);
    g_byte_array_free(mini, TRUE);
    for (guint i = 0; i < n_entries; i++) {
        ObEntry* e = g_ptr_array_index(ob->entries, i);
        g_free(e->name);
        if (e->data != NULL) g_bytes_unref(e->data);
        g_free(e);
    }
    g_ptr_array_free(ob->entries, TRUE);
    g_free(ob);
    return g_byte_array_free_to_bytes(out);
}

#endif /* TEST_OLE_BUILDER_H */
