/* sshfs mount points of the remote machines: location, owner files, unmount
 * and the startup cleanup of stale mounts. No widgets (tests/test_remote_mounts.c). */
#include "explorer/remote_mounts.h"
#include "core/app_paths.h"

#include <glib/gstdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define MOUNTS_FILE "/proc/self/mounts"

gchar* remote_mounts_root(void) {
    gchar* runtime = app_runtime_dir();
    if (runtime == NULL) {
        return NULL;
    }
    gchar* root = g_build_filename(runtime, "remotes", NULL);
    g_free(runtime);
    if (g_mkdir_with_parents(root, 0700) != 0) {
        g_clear_pointer(&root, g_free);
    }
    return root;
}

gchar* remote_mounts_dir(const gchar* machine_id) {
    if (machine_id == NULL || machine_id[0] == '\0') {
        return NULL;
    }
    gchar* root = remote_mounts_root();
    if (root == NULL) {
        return NULL;
    }
    gchar* dir = g_build_filename(root, machine_id, NULL);
    g_free(root);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_clear_pointer(&dir, g_free);
    }
    return dir;
}

/* A field of /proc/self/mounts with its octal escapes (\040 for a space) decoded */
static gchar* decode_field(const gchar* field) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = field; *p != '\0'; p++) {
        if (p[0] == '\\' && p[1] >= '0' && p[1] <= '3' && p[2] >= '0' && p[2] <= '7' &&
            p[3] >= '0' && p[3] <= '7') {
            g_string_append_c(out, (gchar)(((p[1] - '0') << 6) | ((p[2] - '0') << 3) | (p[3] - '0')));
            p += 3;
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

GPtrArray* remote_mounts_parse(const gchar* mounts_text, const gchar* root) {
    GPtrArray* found = g_ptr_array_new_with_free_func(g_free);
    if (mounts_text == NULL || root == NULL || root[0] == '\0') {
        return found;
    }
    gchar* prefix = g_str_has_suffix(root, G_DIR_SEPARATOR_S) ? g_strdup(root)
                                                              : g_strconcat(root, G_DIR_SEPARATOR_S, NULL);
    gchar** lines = g_strsplit(mounts_text, "\n", -1);
    for (gchar** l = lines; *l != NULL; l++) {
        gchar** fields = g_strsplit_set(*l, " \t", 4);
        if (g_strv_length(fields) >= 3 && g_strcmp0(fields[2], "fuse.sshfs") == 0) {
            gchar* point = decode_field(fields[1]);
            if (g_str_has_prefix(point, prefix) && point[strlen(prefix)] != '\0') {
                g_ptr_array_add(found, point);
                point = NULL;
            }
            g_free(point);
        }
        g_strfreev(fields);
    }
    g_strfreev(lines);
    g_free(prefix);
    return found;
}

/* sshfs mount points under root, now */
static GPtrArray* mounted_under(const gchar* root) {
    gchar* text = NULL;
    g_file_get_contents(MOUNTS_FILE, &text, NULL, NULL);
    GPtrArray* found = remote_mounts_parse(text, root);
    g_free(text);
    return found;
}

gboolean remote_mounts_is_mounted(const gchar* path) {
    if (path == NULL) {
        return FALSE;
    }
    gchar* parent = g_path_get_dirname(path);
    GPtrArray* found = mounted_under(parent);
    gboolean mounted = FALSE;
    for (guint i = 0; i < found->len && !mounted; i++) {
        mounted = g_strcmp0(g_ptr_array_index(found, i), path) == 0;
    }
    g_ptr_array_unref(found);
    g_free(parent);
    return mounted;
}

static gchar* owner_file(const gchar* path) {
    return g_strconcat(path, ".owner", NULL);
}

void remote_mounts_claim(const gchar* path) {
#ifdef G_OS_UNIX
    if (path == NULL) {
        return;
    }
    gchar* file = owner_file(path);
    gchar* pid = g_strdup_printf("%d\n", (int)getpid());
    g_file_set_contents(file, pid, -1, NULL);
    g_free(pid);
    g_free(file);
#else
    (void)path;
#endif
}

gint remote_mounts_owner(const gchar* path) {
    if (path == NULL) {
        return 0;
    }
    gchar* file = owner_file(path);
    gchar* text = NULL;
    gint pid = 0;
    if (g_file_get_contents(file, &text, NULL, NULL)) {
        gint64 v = g_ascii_strtoll(text, NULL, 10);
        pid = (v > 0 && v <= G_MAXINT) ? (gint)v : 0;
    }
    g_free(text);
    g_free(file);
    return pid;
}

/* Run an unmount command quietly; TRUE when it succeeded */
static gboolean run_quiet(const gchar* const* argv) {
    gint status = 0;
    gboolean ok = g_spawn_sync(NULL, (gchar**)argv, NULL,
                               G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, NULL, NULL, &status, NULL);
    return ok && g_spawn_check_wait_status(status, NULL);
}

gboolean remote_mounts_unmount(const gchar* path) {
    if (path == NULL) {
        return TRUE;
    }
    if (remote_mounts_is_mounted(path)) {
        const gchar* tries[][4] = {
            { "fusermount3", "-u", path, NULL },
            { "fusermount", "-u", path, NULL },
            { "umount", path, NULL, NULL },
            /* Busy (a shell inside it): detach now, the kernel frees it later */
            { "fusermount3", "-uz", path, NULL },
            { "fusermount", "-uz", path, NULL },
        };
        for (gsize i = 0; i < G_N_ELEMENTS(tries) && remote_mounts_is_mounted(path); i++) {
            run_quiet(tries[i]);
        }
        if (remote_mounts_is_mounted(path)) {
            return FALSE;
        }
    }
    gchar* file = owner_file(path);
    g_remove(file);
    g_free(file);
    g_rmdir(path); /* only when empty: never deletes anything else */
    return TRUE;
}

#ifdef G_OS_UNIX
static gboolean process_alive(gint pid) {
    return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM);
}
#endif

/* Unmount the mounts under root; with owners, keep those of a running instance */
static guint cleanup_root(const gchar* root, gboolean owners) {
    guint count = 0;
    GPtrArray* found = mounted_under(root);
    for (guint i = 0; i < found->len; i++) {
        const gchar* point = g_ptr_array_index(found, i);
#ifdef G_OS_UNIX
        gint pid = owners ? remote_mounts_owner(point) : 0;
        if (pid != 0 && pid != (gint)getpid() && process_alive(pid)) {
            continue;
        }
#else
        (void)owners;
#endif
        if (remote_mounts_unmount(point)) {
            count++;
        }
    }
    g_ptr_array_unref(found);
    return count;
}

guint remote_mounts_cleanup_stale(void) {
    guint count = 0;
    gchar* root = remote_mounts_root();
    if (root != NULL) {
        count += cleanup_root(root, TRUE);
    }
    g_free(root);
    gchar* cache = app_cache_dir();
    gchar* legacy = g_build_filename(cache, "remotes", NULL);
    count += cleanup_root(legacy, FALSE);
    g_free(legacy);
    g_free(cache);
    return count;
}
