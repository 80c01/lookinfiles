/* Disk version tracking for open editor buffers (see editor_stamp.h). */
#include "editor/editor_stamp.h"

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
    if (!file_stamp_read(path, &st)) {
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
