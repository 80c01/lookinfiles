/* The bytes of a file for a preview, local or remote (see preview_source.h). */
#include "preview/preview_source.h"
#include "core/app_types.h"
#include "core/vfs.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

struct PreviewSource {
    FILE* fp;     /* a local file (or the mount) */
    GBytes* mem;  /* a remote file's bytes */
    gsize pos;
};

gboolean preview_source_stamp(const gchar* path, FileStamp* stamp) {
    if (path != NULL && vfs_is_remote(path)) {
        VfsEntry e;
        GError* error = NULL;
        if (vfs_stat(path, &e, NULL, &error)) {
            stamp->size = e.size;
            stamp->mtime_ns = e.mtime * G_GINT64_CONSTANT(1000000000);
            stamp->inode = 0;
            return TRUE;
        }
        gboolean answered = !vfs_error_is_connection(error);
        g_error_free(error);
        if (answered) return FALSE; /* no such file */
    }
    return file_stamp_read(path, stamp);
}

PreviewSource* preview_source_open(const gchar* path, gboolean head, GCancellable* cancel) {
    if (path == NULL) return NULL;
    if (vfs_is_remote(path)) {
        GError* error = NULL;
        GBytes* bytes = vfs_read(path, head ? PREVIEW_BYTES : G_MAXUINT64, NULL, cancel, &error);
        if (bytes != NULL) {
            PreviewSource* src = g_new0(PreviewSource, 1);
            src->mem = bytes;
            return src;
        }
        gboolean answered = !vfs_error_is_connection(error);
        g_error_free(error);
        if (answered) return NULL;
    }
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) return NULL;
    PreviewSource* src = g_new0(PreviewSource, 1);
    src->fp = fp;
    return src;
}

gsize preview_source_read(PreviewSource* src, void* buf, gsize n) {
    if (src->fp != NULL) return fread(buf, 1, n, src->fp);
    gsize len = 0;
    const guint8* data = g_bytes_get_data(src->mem, &len);
    gsize k = MIN(n, len - src->pos);
    if (k > 0) memcpy(buf, data + src->pos, k);
    src->pos += k;
    return k;
}

void preview_source_close(PreviewSource* src) {
    if (src == NULL) return;
    if (src->fp != NULL) fclose(src->fp);
    if (src->mem != NULL) g_bytes_unref(src->mem);
    g_free(src);
}
