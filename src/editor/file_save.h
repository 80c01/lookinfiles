/* Saving edited text to a file in place, as editors do: a symbolic link
 * keeps pointing to the file (the target is written), the file keeps its
 * permissions (scripts stay executable), and a read-only file is not
 * replaced. The write is atomic (temporary file renamed over the target).
 * No AppData, no widgets. */
#ifndef FILE_SAVE_H
#define FILE_SAVE_H

#include <glib.h>

/* Write len bytes of text (-1: up to the NUL) to path. FALSE with error set
 * (G_FILE_ERROR_ACCES for a read-only file) when nothing was written. */
gboolean file_save_text(const gchar* path, const gchar* text, gssize len, GError** error);

#endif /* FILE_SAVE_H */
