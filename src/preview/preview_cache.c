/* Cache of the last previews built (see preview_cache.h) */
#include "preview/preview_cache.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

typedef struct {
    gchar* key;
    FileStamp stamp;
    gpointer value;
    gsize bytes;
} Entry;

struct PreviewCache {
    GQueue order;        /* Entry*, most recently used first */
    GHashTable* by_key;  /* key -> GList* link in order */
    guint max_entries;
    gsize max_bytes;
    gsize bytes;
    GDestroyNotify free_value;
};

gboolean file_stamp_read(const gchar* path, FileStamp* stamp) {
    GStatBuf st;
    if (path == NULL || g_stat(path, &st) != 0) {
        return FALSE;
    }
    stamp->size = (gint64)st.st_size;
#if defined(__APPLE__)
    stamp->mtime_ns = (gint64)st.st_mtimespec.tv_sec * G_GINT64_CONSTANT(1000000000) +
                      st.st_mtimespec.tv_nsec;
#elif defined(G_OS_WIN32)
    stamp->mtime_ns = (gint64)st.st_mtime * G_GINT64_CONSTANT(1000000000);
#else
    stamp->mtime_ns = (gint64)st.st_mtim.tv_sec * G_GINT64_CONSTANT(1000000000) +
                      st.st_mtim.tv_nsec;
#endif
    stamp->inode = (guint64)st.st_ino;
    return TRUE;
}

gboolean file_stamp_equal(const FileStamp* a, const FileStamp* b) {
    return a->size == b->size && a->mtime_ns == b->mtime_ns && a->inode == b->inode;
}

static void entry_free(PreviewCache* cache, Entry* entry) {
    if (cache->free_value != NULL && entry->value != NULL) {
        cache->free_value(entry->value);
    }
    g_free(entry->key);
    g_free(entry);
}

/* Unlink and free the entry at link */
static void drop_link(PreviewCache* cache, GList* link) {
    Entry* entry = link->data;
    g_hash_table_remove(cache->by_key, entry->key);
    g_queue_delete_link(&cache->order, link);
    cache->bytes -= entry->bytes;
    entry_free(cache, entry);
}

PreviewCache* preview_cache_new(guint max_entries, gsize max_bytes, GDestroyNotify free_value) {
    PreviewCache* cache = g_new0(PreviewCache, 1);
    g_queue_init(&cache->order);
    cache->by_key = g_hash_table_new(g_str_hash, g_str_equal); /* keys owned by the entries */
    cache->max_entries = MAX(max_entries, 1);
    cache->max_bytes = max_bytes;
    cache->free_value = free_value;
    return cache;
}

void preview_cache_free(PreviewCache* cache) {
    if (cache == NULL) {
        return;
    }
    while (cache->order.head != NULL) {
        drop_link(cache, cache->order.head);
    }
    g_hash_table_destroy(cache->by_key);
    g_free(cache);
}

gpointer preview_cache_lookup(PreviewCache* cache, const gchar* key, FileStamp* stamp) {
    GList* link = (key != NULL) ? g_hash_table_lookup(cache->by_key, key) : NULL;
    if (link == NULL) {
        return NULL;
    }
    g_queue_unlink(&cache->order, link);
    g_queue_push_head_link(&cache->order, link);
    Entry* entry = link->data;
    if (stamp != NULL) {
        *stamp = entry->stamp;
    }
    return entry->value;
}

void preview_cache_insert(PreviewCache* cache, const gchar* key, const FileStamp* stamp,
                          gpointer value, gsize bytes) {
    preview_cache_remove(cache, key);
    if (bytes > cache->max_bytes) {
        if (cache->free_value != NULL && value != NULL) {
            cache->free_value(value);
        }
        return;
    }
    Entry* entry = g_new0(Entry, 1);
    entry->key = g_strdup(key);
    entry->stamp = *stamp;
    entry->value = value;
    entry->bytes = bytes;
    g_queue_push_head(&cache->order, entry);
    g_hash_table_insert(cache->by_key, entry->key, cache->order.head);
    cache->bytes += bytes;
    while (cache->order.length > cache->max_entries || cache->bytes > cache->max_bytes) {
        drop_link(cache, cache->order.tail);
    }
}

void preview_cache_remove(PreviewCache* cache, const gchar* key) {
    GList* link = (key != NULL) ? g_hash_table_lookup(cache->by_key, key) : NULL;
    if (link != NULL) {
        drop_link(cache, link);
    }
}

guint preview_cache_count(const PreviewCache* cache) {
    return cache->order.length;
}

gsize preview_cache_bytes(const PreviewCache* cache) {
    return cache->bytes;
}
