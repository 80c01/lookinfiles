/* Disk version tracking for open editor buffers, built on preview_cache's
 * FileStamp: distinguishes unread, missing and existing files so removal,
 * creation and atomic inode replacement are all detected cleanly. Pure GLib,
 * unit-tested. */
#ifndef EDITOR_STAMP_H
#define EDITOR_STAMP_H

#include "preview/preview_cache.h"

typedef struct {
    gboolean valid;  /* TRUE once read (even when the file does not exist) */
    gboolean exists; /* TRUE when the file existed on disk */
    FileStamp file;  /* valid when exists is TRUE */
} EditorStamp;

/* Read path's metadata into *out (zeroed first). Returns TRUE when the file
 * exists, FALSE when missing or path is NULL/empty (out->valid stays TRUE for
 * a non-empty path so a missing file is tracked). */
gboolean editor_stamp_read(const gchar* path, EditorStamp* out);

/* TRUE when two valid stamps describe the same file state on disk. */
gboolean editor_stamp_equal(const EditorStamp* a, const EditorStamp* b);

/* Read path's current metadata (into *current if non-NULL) and return TRUE if
 * it differs from recorded (modified, replaced, created or removed).
 * Returns FALSE when recorded is NULL/invalid or path is NULL/empty. */
gboolean editor_stamp_changed(const gchar* path, const EditorStamp* recorded, EditorStamp* current);

#endif /* EDITOR_STAMP_H */
