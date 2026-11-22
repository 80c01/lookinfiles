/* Saving edited text in place (see file_save.h) */
#include "editor/file_save.h"
#include "core/vfs.h"

#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#ifndef W_OK
#define W_OK 2
#endif

/* The file a chain of symbolic links ends at (path itself when it is not a
 * link); gives up after a few hops (a loop then fails at the write) */
static gchar* resolve_links(const gchar* path) {
    gchar* current = g_strdup(path);
    for (gint hops = 0; hops < 32; hops++) {
        gchar* target = g_file_read_link(current, NULL);
        if (target == NULL) {
            break;
        }
        if (!g_path_is_absolute(target)) {
            gchar* dir = g_path_get_dirname(current);
            gchar* full = g_build_filename(dir, target, NULL);
            g_free(dir);
            g_free(target);
            target = full;
        }
        g_free(current);
        current = target;
    }
    return current;
}

/* A remote machine's file over SFTP (never the local folder it is seen at) */
static gboolean save_remote(const gchar* path, const gchar* text, gssize len, GError** error) {
    GBytes* bytes = g_bytes_new(text, len >= 0 ? (gsize)len : strlen(text));
    GError* err = NULL;
    gboolean ok = vfs_write(path, bytes, 0644, &err); /* links followed, mode kept, atomic */
    g_bytes_unref(bytes);
    if (ok) {
        g_clear_error(&err);
    } else if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES, "%s is read-only", path);
        g_error_free(err);
    } else {
        g_propagate_error(error, err);
    }
    return ok;
}

gboolean file_save_text(const gchar* path, const gchar* text, gssize len, GError** error) {
    g_return_val_if_fail(path != NULL && text != NULL, FALSE);
    if (vfs_is_remote(path)) {
        return save_remote(path, text, len, error);
    }
    gchar* target = resolve_links(path);
    int mode = 0666;
    GStatBuf st;
    if (g_stat(target, &st) == 0) {
        if (g_access(target, W_OK) != 0) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES, "%s is read-only", target);
            g_free(target);
            return FALSE;
        }
        mode = (int)(st.st_mode & 07777);
    }
    gboolean ok = g_file_set_contents_full(target, text, len, G_FILE_SET_CONTENTS_CONSISTENT, mode, error);
    g_free(target);
    return ok;
}
