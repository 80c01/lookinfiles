/* Reading one entry out of an archive (see archive_extract.h). */
#include "archive_extract.h"
#include "core/i18n.h"

#include <glib/gstdio.h>
#include <string.h>

#include "core/app_paths.h"
#include "zip_reader.h"

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

static gboolean has_suffix_ci(const gchar* name, const gchar* suffix) {
    gsize n = strlen(name);
    gsize s = strlen(suffix);
    return n > s && g_ascii_strcasecmp(name + n - s, suffix) == 0;
}

static gboolean is_zip_name(const gchar* name) {
    static const gchar* const exts[] = { ".zip", ".jar", ".war", ".ear", ".apk", ".aar",
                                         ".whl", ".nupkg", ".vsix", ".xpi", ".cbz", NULL };
    for (gint i = 0; exts[i] != NULL; i++) {
        if (has_suffix_ci(name, exts[i])) {
            return TRUE;
        }
    }
    return FALSE;
}

gboolean archive_extract_can_browse(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    if (is_zip_name(name)) {
        return TRUE;
    }
#ifdef HAVE_LIBARCHIVE
    static const gchar* const exts[] = { ".tar", ".tgz", ".tbz", ".tbz2", ".txz", ".tzst",
                                         ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst",
                                         ".tar.lz", ".tar.lzma", ".tar.lz4", ".tar.z",
                                         ".7z", ".rar", ".cbr", ".iso", ".cpio", ".cab",
                                         ".deb", ".rpm", NULL };
    for (gint i = 0; exts[i] != NULL; i++) {
        if (has_suffix_ci(name, exts[i])) {
            return TRUE;
        }
    }
#endif
    return FALSE;
}

gchar* archive_extract_clean_path(const gchar* entry) {
    if (entry == NULL) {
        return NULL;
    }
    gchar** parts = g_strsplit(entry, "/", -1);
    GPtrArray* kept = g_ptr_array_new();
    gboolean ok = TRUE;
    for (gint i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0' || strcmp(parts[i], ".") == 0) {
            continue;
        }
        if (strcmp(parts[i], "..") == 0) {
            ok = FALSE;
            break;
        }
        g_ptr_array_add(kept, parts[i]);
    }
    gchar* result = NULL;
    if (ok && kept->len > 0) {
        g_ptr_array_add(kept, NULL);
        result = g_strjoinv("/", (gchar**)kept->pdata);
    }
    g_ptr_array_unref(kept);
    g_strfreev(parts);
    return result;
}

gchar* archive_extract_cache_path(const gchar* archive, const gchar* entry) {
    gchar* clean = archive_extract_clean_path(entry);
    GStatBuf st;
    if (clean == NULL || g_stat(archive, &st) != 0) {
        g_free(clean);
        return NULL;
    }
    gchar* key = g_strdup_printf("%s|%" G_GINT64_FORMAT "|%" G_GINT64_FORMAT, archive,
                                 (gint64)st.st_size, (gint64)st.st_mtime);
    gchar* id = g_compute_checksum_for_string(G_CHECKSUM_SHA1, key, -1);
    gchar* cache = app_cache_dir();
    gchar* path = g_build_filename(cache, "archive", id, clean, NULL);
    g_free(cache);
    g_free(id);
    g_free(key);
    g_free(clean);
    return path;
}

/* Replace dest by a file holding data (written next to it, then renamed) */
static gboolean write_dest(const gchar* dest, const guchar* data, gsize len, GError** error) {
    gchar* dir = g_path_get_dirname(dest);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("cannot create %s"), dir);
        g_free(dir);
        return FALSE;
    }
    g_free(dir);
    return g_file_set_contents(dest, (const gchar*)data, (gssize)len, error);
}

static gboolean extract_zip(const gchar* archive, const gchar* entry, const gchar* dest,
                            guint64 max_size, GError** error) {
    g_autoptr(ZipReader) zip = zip_reader_open(archive, error);
    if (zip == NULL) {
        return FALSE;
    }
    if (!zip_reader_has(zip, entry)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("no such entry: %s"), entry);
        return FALSE;
    }
    gint64 size = zip_reader_get_size(zip, entry);
    if (size >= 0 && (guint64)size > max_size) {
        gchar* text = g_format_size((guint64)size);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("entry too large (%s)"), text);
        g_free(text);
        return FALSE;
    }
    GBytes* data = zip_reader_read(zip, entry, (gsize)max_size, error);
    if (data == NULL) {
        return FALSE;
    }
    gsize len = 0;
    const guchar* bytes = g_bytes_get_data(data, &len);
    gboolean ok = write_dest(dest, bytes != NULL ? bytes : (const guchar*)"", len, error);
    g_bytes_unref(data);
    return ok;
}

#ifdef HAVE_LIBARCHIVE
static gboolean extract_libarchive(const gchar* archive, const gchar* entry, const gchar* dest,
                                   guint64 max_size, GCancellable* cancellable, GError** error) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, archive, 64 * 1024) != ARCHIVE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s",
                    archive_error_string(a) != NULL ? archive_error_string(a) : _("unknown archive format"));
        archive_read_free(a);
        return FALSE;
    }
    gboolean ok = FALSE;
    gboolean found = FALSE;
    struct archive_entry* ae = NULL;
    while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
        if (g_cancellable_is_cancelled(cancellable)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED, _("cancelled"));
            break;
        }
        const gchar* name = archive_entry_pathname_utf8(ae);
        if (name == NULL) {
            name = archive_entry_pathname(ae);
        }
        gchar* valid = g_utf8_make_valid(name != NULL ? name : "", -1);
        gboolean match = (strcmp(valid, entry) == 0);
        g_free(valid);
        if (!match) {
            archive_read_data_skip(a);
            continue;
        }
        found = TRUE;
        if (archive_entry_filetype(ae) != AE_IFREG) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("not a regular file: %s"), entry);
            break;
        }
        if (archive_entry_size_is_set(ae) && (guint64)archive_entry_size(ae) > max_size) {
            gchar* text = g_format_size((guint64)archive_entry_size(ae));
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("entry too large (%s)"), text);
            g_free(text);
            break;
        }
        GByteArray* buf = g_byte_array_new();
        guchar chunk[64 * 1024];
        gboolean read_ok = TRUE;
        for (;;) {
            la_ssize_t n = archive_read_data(a, chunk, sizeof(chunk));
            if (n == 0) {
                break;
            }
            if (n < 0 || buf->len + (guint)n > max_size ||
                g_cancellable_is_cancelled(cancellable)) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                            n < 0 && archive_error_string(a) != NULL ? archive_error_string(a)
                                                                    : "cannot read the entry");
                read_ok = FALSE;
                break;
            }
            g_byte_array_append(buf, chunk, (guint)n);
        }
        if (read_ok) {
            ok = write_dest(dest, buf->data, buf->len, error);
        }
        g_byte_array_unref(buf);
        break;
    }
    if (!found && error != NULL && *error == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("no such entry: %s"), entry);
    }
    archive_read_free(a);
    return ok;
}
#endif

gboolean archive_extract_entry(const gchar* archive, const gchar* entry, const gchar* dest,
                               guint64 max_size, GCancellable* cancellable, GError** error) {
    guchar head[4] = { 0 };
    gsize got = 0;
    FILE* f = g_fopen(archive, "rb");
    if (f == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("cannot open %s"), archive);
        return FALSE;
    }
    got = fread(head, 1, sizeof(head), f);
    fclose(f);
    if (got == 4 && memcmp(head, "PK", 2) == 0 && (head[2] == 3 || head[2] == 5)) {
        return extract_zip(archive, entry, dest, max_size, error);
    }
#ifdef HAVE_LIBARCHIVE
    return extract_libarchive(archive, entry, dest, max_size, cancellable, error);
#else
    (void)cancellable;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("this format needs libarchive"));
    return FALSE;
#endif
}
