/* Saving edited text in place (see file_save.h) */
#include "editor/file_save.h"

#include <glib/gstdio.h>
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

gboolean file_save_text(const gchar* path, const gchar* text, gssize len, GError** error) {
    g_return_val_if_fail(path != NULL && text != NULL, FALSE);
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
