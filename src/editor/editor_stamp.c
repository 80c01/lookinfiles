/* Disk version tracking for open editor buffers (see editor_stamp.h). */
#include "editor/editor_stamp.h"
#include "core/vfs.h"
#include "core/vfs_watch.h"

#include <string.h>

gboolean editor_stamp_read(const gchar* path, EditorStamp* out) {
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (path == NULL || path[0] == '\0') {
        return FALSE;
    }
    if (out != NULL) {
        out->valid = TRUE;
    }
    FileStamp st;
    if (vfs_is_remote(path)) {
        /* The last reading over SFTP (no wait; vfs_watch.h), else the mount's,
         * in the same terms: seconds, no inode */
        gboolean exists = FALSE;
        gint64 size = 0;
        gint64 mtime = 0;
        if (vfs_watch_stamp(path, &exists, &size, &mtime)) {
            st.size = size;
            st.mtime_ns = mtime * G_GINT64_CONSTANT(1000000000);
        } else if ((exists = file_stamp_read(path, &st))) {
            st.mtime_ns -= st.mtime_ns % G_GINT64_CONSTANT(1000000000);
        }
        st.inode = 0;
        if (!exists) {
            return FALSE;
        }
    } else if (!file_stamp_read(path, &st)) {
        return FALSE;
    }
    if (out != NULL) {
        out->exists = TRUE;
        out->file = st;
    }
    return TRUE;
}

gboolean editor_stamp_equal(const EditorStamp* a, const EditorStamp* b) {
    if (a == NULL || b == NULL || !a->valid || !b->valid) {
        return FALSE;
    }
    if (a->exists != b->exists) {
        return FALSE;
    }
    if (!a->exists) {
        return TRUE;
    }
    return file_stamp_equal(&a->file, &b->file);
}

gboolean editor_stamp_changed(const gchar* path, const EditorStamp* recorded, EditorStamp* current) {
    EditorStamp now;
    editor_stamp_read(path, &now);
    if (current != NULL) {
        *current = now;
    }
    if (recorded == NULL || !recorded->valid || !now.valid) {
        return FALSE;
    }
    return !editor_stamp_equal(recorded, &now);
}
