/* Put back a file moved to the trash (freedesktop.org trash specification). */
#include "trash_restore.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

gboolean trash_info_parse(const gchar* contents, const gchar* topdir,
                          gchar** out_path, gchar** out_date) {
    *out_path = NULL;
    *out_date = NULL;
    if (contents == NULL) {
        return FALSE;
    }
    GKeyFile* kf = g_key_file_new();
    gboolean ok = g_key_file_load_from_data(kf, contents, (gsize)-1, G_KEY_FILE_NONE, NULL);
    gchar* raw = ok ? g_key_file_get_string(kf, "Trash Info", "Path", NULL) : NULL;
    gchar* date = ok ? g_key_file_get_string(kf, "Trash Info", "DeletionDate", NULL) : NULL;
    g_key_file_free(kf);
    gchar* path = (raw != NULL) ? g_uri_unescape_string(raw, NULL) : NULL;
    g_free(raw);
    if (path == NULL || path[0] == '\0' || (!g_path_is_absolute(path) && topdir == NULL)) {
        g_free(path);
        g_free(date);
        return FALSE;
    }
    if (!g_path_is_absolute(path)) {
        gchar* abs = g_build_filename(topdir, path, NULL);
        g_free(path);
        path = abs;
    }
    *out_path = g_canonicalize_filename(path, NULL);
    *out_date = (date != NULL) ? date : g_strdup("");
    g_free(path);
    return TRUE;
}

gchar* trash_find_item(const gchar* trash_dir, const gchar* topdir,
                       const gchar* orig_path, gchar** out_info) {
    *out_info = NULL;
    gchar* info_dir = g_build_filename(trash_dir, "info", NULL);
    GDir* dir = g_dir_open(info_dir, 0, NULL);
    if (dir == NULL) {
        g_free(info_dir);
        return NULL;
    }
    gchar* want = g_canonicalize_filename(orig_path, NULL);
    gchar* best_name = NULL;
    gchar* best_date = NULL;
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_suffix(name, ".trashinfo")) {
            continue;
        }
        gchar* file = g_build_filename(info_dir, name, NULL);
        gchar* contents = NULL;
        gchar* path = NULL;
        gchar* date = NULL;
        if (g_file_get_contents(file, &contents, NULL, NULL) &&
            trash_info_parse(contents, topdir, &path, &date) &&
            g_strcmp0(path, want) == 0 &&
            (best_date == NULL || strcmp(date, best_date) >= 0)) {
            /* ISO 8601 dates compare as strings: keep the newest */
            g_free(best_name);
            g_free(best_date);
            best_name = g_strdup(name);
            best_date = date;
            date = NULL;
        }
        g_free(date);
        g_free(path);
        g_free(contents);
        g_free(file);
    }
    g_dir_close(dir);
    g_free(want);
    g_free(best_date);

    gchar* item = NULL;
    if (best_name != NULL) {
        gchar* base = g_strndup(best_name, strlen(best_name) - strlen(".trashinfo"));
        item = g_build_filename(trash_dir, "files", base, NULL);
        *out_info = g_build_filename(info_dir, best_name, NULL);
        g_free(base);
        g_free(best_name);
    }
    g_free(info_dir);
    return item;
}

/* Mount point holding path: the top directory whose parent is on another device */
static gchar* mount_point_of(const gchar* path) {
    gchar* dir = g_strdup(path);
    GStatBuf st;
    if (g_stat(dir, &st) != 0) {
        g_free(dir);
        return NULL;
    }
    for (;;) {
        gchar* parent = g_path_get_dirname(dir);
        GStatBuf pst;
        if (g_strcmp0(parent, dir) == 0 || g_stat(parent, &pst) != 0 || pst.st_dev != st.st_dev) {
            g_free(parent);
            return dir;
        }
        g_free(dir);
        dir = parent;
    }
}

/* Find the trashed copy of orig_path in each trash it may be in */
static gchar* find_in_trashes(const gchar* orig_path, gchar** out_info) {
    gchar* home_trash = g_build_filename(g_get_user_data_dir(), "Trash", NULL);
    gchar* item = trash_find_item(home_trash, NULL, orig_path, out_info);
    g_free(home_trash);
    if (item != NULL) {
        return item;
    }
    gchar* parent = g_path_get_dirname(orig_path);
    gchar* top = mount_point_of(parent);
    g_free(parent);
    if (top == NULL) {
        return NULL;
    }
    gchar* uid = g_strdup_printf("%u", (guint)getuid());
    gchar* dot_uid = g_strdup_printf(".Trash-%s", uid);
    gchar* trashes[] = {
        g_build_filename(top, dot_uid, NULL),
        g_build_filename(top, ".Trash", uid, NULL),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(trashes); i++) {
        if (item == NULL) {
            item = trash_find_item(trashes[i], top, orig_path, out_info);
        }
        g_free(trashes[i]);
    }
    g_free(dot_uid);
    g_free(uid);
    g_free(top);
    return item;
}

gboolean trash_restore(const gchar* orig_path, GError** error) {
    if (g_file_test(orig_path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "“%s” exists again", orig_path);
        return FALSE;
    }
    gchar* info = NULL;
    gchar* item = find_in_trashes(orig_path, &info);
    if (item == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "it is no longer in the trash");
        return FALSE;
    }
    gboolean ok = TRUE;
    if (g_rename(item, orig_path) != 0) {
        int saved = errno;
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved), "%s", g_strerror(saved));
        ok = FALSE;
    } else {
        g_unlink(info);
    }
    g_free(item);
    g_free(info);
    return ok;
}
