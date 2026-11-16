/* Put back a file moved to the trash (freedesktop.org trash, Windows Recycle Bin). */
#include "explorer/trash_restore.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#if !defined(G_OS_WIN32) && !defined(_WIN32)
#include <unistd.h>
#endif

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

/* ------------------------------------------------------------------------- */
/* Windows Recycle Bin                                                       */
/* ------------------------------------------------------------------------- */

#define WIN_INFO_HEADER 24   /* version, size, deletion time: 3 x 64 bits */
#define WIN_INFO_V1_CHARS 260 /* MAX_PATH */

static guint64 read_le64(const guint8* p) {
    guint64 v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

static guint32 read_le32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

gboolean trash_win_info_parse(const guint8* data, gsize len, gchar** out_path, guint64* out_time) {
    *out_path = NULL;
    *out_time = 0;
    if (data == NULL || len < WIN_INFO_HEADER) {
        return FALSE;
    }
    guint64 version = read_le64(data);
    const guint8* name = NULL;
    gsize chars = 0;
    if (version == 1) {
        name = data + WIN_INFO_HEADER;
        chars = MIN((len - WIN_INFO_HEADER) / 2, WIN_INFO_V1_CHARS);
    } else if (version == 2 && len >= WIN_INFO_HEADER + 4) {
        guint32 n = read_le32(data + WIN_INFO_HEADER);
        if (n > (len - WIN_INFO_HEADER - 4) / 2) {
            return FALSE;
        }
        name = data + WIN_INFO_HEADER + 4;
        chars = n;
    } else {
        return FALSE;
    }
    gunichar2* wide = g_new(gunichar2, chars + 1);
    gsize count = 0;
    while (count < chars) {
        gunichar2 c = (gunichar2)(name[2 * count] | (name[2 * count + 1] << 8));
        if (c == 0) {
            break;
        }
        wide[count++] = c;
    }
    gchar* path = count > 0 ? g_utf16_to_utf8(wide, (glong)count, NULL, NULL, NULL) : NULL;
    g_free(wide);
    if (path == NULL) {
        return FALSE;
    }
    *out_path = path;
    *out_time = read_le64(data + 16);
    return TRUE;
}

/* A Windows path for comparison: backslashes, no trailing one (but "C:\"),
 * case folded */
static gchar* win_path_key(const gchar* path) {
    gchar* copy = g_strdup(path);
    for (gchar* p = copy; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
    gsize n = strlen(copy);
    while (n > 3 && copy[n - 1] == '\\') {
        copy[--n] = '\0';
    }
    gchar* key = g_utf8_casefold(copy, -1);
    g_free(copy);
    return key;
}

gchar* trash_win_find_item(const gchar* bin_dir, const gchar* orig_path, gchar** out_info) {
    *out_info = NULL;
    GDir* dir = g_dir_open(bin_dir, 0, NULL);
    if (dir == NULL) {
        return NULL;
    }
    gchar* want = win_path_key(orig_path);
    gchar* best = NULL;
    guint64 best_time = 0;
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_prefix(name, "$I")) {
            continue;
        }
        gchar* file = g_build_filename(bin_dir, name, NULL);
        gchar* data = NULL;
        gsize len = 0;
        gchar* path = NULL;
        guint64 time = 0;
        if (g_file_get_contents(file, &data, &len, NULL) &&
            trash_win_info_parse((const guint8*)data, len, &path, &time) &&
            (best == NULL || time >= best_time)) {
            gchar* key = win_path_key(path);
            gchar* item_name = g_strconcat("$R", name + 2, NULL);
            gchar* item = g_build_filename(bin_dir, item_name, NULL);
            if (g_strcmp0(key, want) == 0 && g_file_test(item, G_FILE_TEST_EXISTS)) {
                g_free(best);
                best = g_strdup(name);
                best_time = time;
            }
            g_free(item);
            g_free(item_name);
            g_free(key);
        }
        g_free(path);
        g_free(data);
        g_free(file);
    }
    g_dir_close(dir);
    g_free(want);
    if (best == NULL) {
        return NULL;
    }
    gchar* item_name = g_strconcat("$R", best + 2, NULL);
    gchar* item = g_build_filename(bin_dir, item_name, NULL);
    *out_info = g_build_filename(bin_dir, best, NULL);
    g_free(item_name);
    g_free(best);
    return item;
}

#if defined(G_OS_WIN32) || defined(_WIN32)
/* The Recycle Bin of the drive of orig_path: every user directory of
 * <drive>:\$Recycle.Bin, those of other users failing to open */
static gchar* find_in_trashes(const gchar* orig_path, gchar** out_info) {
    *out_info = NULL;
    if (!g_ascii_isalpha(orig_path[0]) || orig_path[1] != ':') {
        return NULL; /* network paths have no Recycle Bin */
    }
    gchar* root = g_strdup_printf("%c:\\$Recycle.Bin", g_ascii_toupper(orig_path[0]));
    GDir* dir = g_dir_open(root, 0, NULL);
    gchar* item = NULL;
    const gchar* name;
    while (dir != NULL && item == NULL && (name = g_dir_read_name(dir)) != NULL) {
        gchar* bin = g_build_filename(root, name, NULL);
        item = trash_win_find_item(bin, orig_path, out_info);
        g_free(bin);
    }
    if (dir != NULL) {
        g_dir_close(dir);
    }
    g_free(root);
    return item;
}
#else
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
#endif

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
