#ifndef PREVIEW_CACHE_H
#define PREVIEW_CACHE_H

/* Cache of the last previews built (most recently used first), keyed by a
 * string (file and preview settings). Each value keeps the stamp of the file
 * it was built from, so a reader can check it still matches the file. Bounded
 * by entry count and total size: the least recently used values are freed. */
#include <glib.h>

/* What identifies a file's version: size, modification time, inode */
typedef struct {
    gint64 size;
    gint64 mtime_ns;
    guint64 inode;
} FileStamp;

/* Stamp of the file at path (follows symlinks). FALSE if it cannot be read. */
gboolean file_stamp_read(const gchar* path, FileStamp* stamp);
gboolean file_stamp_equal(const FileStamp* a, const FileStamp* b);

typedef struct PreviewCache PreviewCache;

/* free_value frees the values (NULL: not freed) */
PreviewCache* preview_cache_new(guint max_entries, gsize max_bytes, GDestroyNotify free_value);
void preview_cache_free(PreviewCache* cache);

/* Value stored for key, or NULL; marks it most recently used. The value
 * stays owned by the cache: valid until the next insert or remove. */
gpointer preview_cache_lookup(PreviewCache* cache, const gchar* key, FileStamp* stamp);
/* Store value (taking ownership) for key, replacing any previous one; bytes
 * is its approximate size. A value larger than the whole cache is freed at once. */
void preview_cache_insert(PreviewCache* cache, const gchar* key, const FileStamp* stamp,
                          gpointer value, gsize bytes);
void preview_cache_remove(PreviewCache* cache, const gchar* key);

guint preview_cache_count(const PreviewCache* cache);
gsize preview_cache_bytes(const PreviewCache* cache);

#endif /* PREVIEW_CACHE_H */
