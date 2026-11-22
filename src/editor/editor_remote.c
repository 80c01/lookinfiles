/* Remote files opened in the editor (see editor_remote.h). */
#include "editor/editor_remote.h"
#include "editor/editor_private.h"
#include "core/vfs.h"

#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gchar* contents; /* NUL-terminated */
    gsize len;
    guint64 size; /* the file's */
    gboolean failed;
} RemoteOpen;

static void remote_open_free(gpointer data) {
    RemoteOpen* ro = data;
    g_free(ro->name);
    g_free(ro->full);
    g_free(ro->contents);
    g_free(ro);
}

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    RemoteOpen* ro = data;
    GError* error = NULL;
    GBytes* bytes = vfs_read(ro->full, (guint64)EDIT_MAX_BYTES + 1, &ro->size, cancel, &error);
    if (bytes != NULL) {
        gsize n = 0;
        const gchar* d = g_bytes_get_data(bytes, &n);
        ro->contents = g_malloc(n + 1);
        if (n > 0) memcpy(ro->contents, d, n);
        ro->contents[n] = '\0';
        ro->len = n;
        g_bytes_unref(bytes);
    } else if (vfs_error_is_connection(error)) { /* the mount, as a local file */
        GStatBuf st;
        if (g_stat(ro->full, &st) == 0) ro->size = (guint64)st.st_size;
        ro->failed = ro->size > EDIT_MAX_BYTES || !g_file_get_contents(ro->full, &ro->contents, &ro->len, NULL);
    } else {
        ro->failed = TRUE;
    }
    g_clear_error(&error);
    g_task_return_boolean(task, TRUE);
}

static void on_read(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    RemoteOpen* ro = g_task_get_task_data(G_TASK(res));
    AppData* app = ro->app;
    for (GList* l = app->editors; l != NULL; l = l->next) { /* opened meanwhile: show that tab */
        if (g_strcmp0(((EditorTab*)l->data)->path, ro->full) == 0) {
            editor_open_text_file(app, ro->name, ro->full);
            return;
        }
    }
    gchar* msg = NULL;
    if (ro->size > EDIT_MAX_BYTES) {
        msg = g_strdup_printf("[ %s is too large to edit (%" G_GUINT64_FORMAT " bytes, limit %d) ]", ro->name,
                              ro->size, EDIT_MAX_BYTES);
    } else if (ro->failed || ro->contents == NULL) {
        msg = g_strdup_printf("[ Cannot read: %s ]", ro->name);
    }
    if (msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }
    editor_open_contents(app, ro->name, ro->full, g_steal_pointer(&ro->contents), ro->len);
}

gboolean editor_remote_get_contents(const gchar* path, gchar** contents, gsize* len, GError** error) {
    if (vfs_is_remote(path)) {
        GError* err = NULL;
        GBytes* bytes = vfs_read(path, G_MAXUINT64, NULL, NULL, &err);
        if (bytes != NULL) {
            gsize n = 0;
            const gchar* d = g_bytes_get_data(bytes, &n);
            *contents = g_malloc(n + 1);
            if (n > 0) memcpy(*contents, d, n);
            (*contents)[n] = '\0';
            if (len != NULL) *len = n;
            g_bytes_unref(bytes);
            return TRUE;
        }
        if (!vfs_error_is_connection(err)) {
            g_propagate_error(error, err);
            return FALSE;
        }
        g_error_free(err);
    }
    return g_file_get_contents(path, contents, len, error);
}

void editor_remote_open(AppData* app, const gchar* name, const gchar* full) {
    RemoteOpen* ro = g_new0(RemoteOpen, 1);
    ro->app = app;
    ro->name = g_strdup(name);
    ro->full = g_strdup(full);
    GTask* task = g_task_new(NULL, NULL, on_read, NULL);
    g_task_set_task_data(task, ro, remote_open_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
}
