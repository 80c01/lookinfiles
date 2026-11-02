#include "ps_convert.h"
#include <string.h>
#include <sys/stat.h>
#include <utime.h>
#include <glib/gstdio.h>

#define CACHE_KEEP 16

gchar** ps_gs_argv(const gchar* gs, const gchar* input, const gchar* output,
                   gboolean eps, gint last_page) {
    GPtrArray* argv = g_ptr_array_new();
    g_ptr_array_add(argv, g_strdup(gs));
    g_ptr_array_add(argv, g_strdup("-q"));
    g_ptr_array_add(argv, g_strdup("-dSAFER"));
    g_ptr_array_add(argv, g_strdup("-dBATCH"));
    g_ptr_array_add(argv, g_strdup("-dNOPAUSE"));
    g_ptr_array_add(argv, g_strdup("-sDEVICE=pdfwrite"));
    if (eps) {
        g_ptr_array_add(argv, g_strdup("-dEPSCrop"));
    }
    if (last_page > 0) {
        g_ptr_array_add(argv, g_strdup_printf("-dLastPage=%d", last_page));
    }
    g_ptr_array_add(argv, g_strdup_printf("-sOutputFile=%s", output));
    g_ptr_array_add(argv, g_strdup(input));
    g_ptr_array_add(argv, NULL);
    return (gchar**)g_ptr_array_free(argv, FALSE);
}

gchar** ps_dvips_argv(const gchar* dvips, const gchar* input, const gchar* output, gint last_page) {
    GPtrArray* argv = g_ptr_array_new();
    g_ptr_array_add(argv, g_strdup(dvips));
    g_ptr_array_add(argv, g_strdup("-q"));
    g_ptr_array_add(argv, g_strdup("-R2"));
    if (last_page > 0) {
        g_ptr_array_add(argv, g_strdup_printf("-l=%d", last_page));
    }
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup(output));
    g_ptr_array_add(argv, g_strdup(input));
    g_ptr_array_add(argv, NULL);
    return (gchar**)g_ptr_array_free(argv, FALSE);
}

gboolean ps_convert_available(PsSource source) {
    gchar* gs = g_find_program_in_path("gs");
    gboolean ok = gs != NULL;
    g_free(gs);
    if (ok && source == PS_SOURCE_DVI) {
        gchar* dvips = g_find_program_in_path("dvips");
        ok = dvips != NULL;
        g_free(dvips);
    }
    return ok;
}

/* Remove the oldest conversions beyond CACHE_KEEP */
static void prune_cache(const gchar* dir) {
    GDir* d = g_dir_open(dir, 0, NULL);
    if (d == NULL) {
        return;
    }
    GPtrArray* files = g_ptr_array_new_with_free_func(g_free);
    const gchar* entry;
    while ((entry = g_dir_read_name(d)) != NULL) {
        g_ptr_array_add(files, g_build_filename(dir, entry, NULL));
    }
    g_dir_close(d);
    while (files->len > CACHE_KEEP) {
        guint oldest = 0;
        gint64 oldest_time = G_MAXINT64;
        for (guint i = 0; i < files->len; i++) {
            GStatBuf st;
            gint64 t = g_stat(g_ptr_array_index(files, i), &st) == 0 ? (gint64)st.st_mtime : 0;
            if (t < oldest_time) {
                oldest_time = t;
                oldest = i;
            }
        }
        g_remove(g_ptr_array_index(files, oldest));
        g_ptr_array_remove_index_fast(files, oldest);
    }
    g_ptr_array_unref(files);
}

static gboolean run_tool(gchar** argv, GCancellable* cancellable, GError** error) {
    GSubprocess* proc = g_subprocess_newv((const gchar* const*)argv,
                                          G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                          G_SUBPROCESS_FLAGS_STDERR_SILENCE, error);
    if (proc == NULL) {
        return FALSE;
    }
    gboolean ok = g_subprocess_wait_check(proc, cancellable, error);
    if (!ok) {
        g_subprocess_force_exit(proc);
    }
    g_object_unref(proc);
    return ok;
}

gchar* ps_convert_to_pdf(const gchar* path, PsSource source, gint last_page,
                         GCancellable* cancellable, GError** error) {
    GStatBuf st;
    if (g_stat(path, &st) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "cannot read %s", path);
        return NULL;
    }
    gchar* gs = g_find_program_in_path("gs");
    gchar* dvips = source == PS_SOURCE_DVI ? g_find_program_in_path("dvips") : NULL;
    if (gs == NULL || (source == PS_SOURCE_DVI && dvips == NULL)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s is not installed",
                    gs == NULL ? "ghostscript (gs)" : "dvips");
        g_free(gs);
        g_free(dvips);
        return NULL;
    }

    gchar* dir = g_build_filename(g_get_user_cache_dir(), "lookinfiles", "convert", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar* key = g_strdup_printf("%s|%lld|%lld|%d", path, (long long)st.st_size,
                                 (long long)st.st_mtime, last_page);
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA1, key, -1);
    gchar* name = g_strconcat(sum, ".pdf", NULL);
    gchar* out = g_build_filename(dir, name, NULL);
    g_free(key);
    g_free(sum);
    g_free(name);

    gchar* result = NULL;
    if (g_file_test(out, G_FILE_TEST_IS_REGULAR)) {
        g_utime(out, NULL); /* most recently used */
        result = out;
        out = NULL;
    } else {
        gchar* part = g_strconcat(out, ".part", NULL);
        gchar* ps_path = NULL;
        gchar* abs = g_canonicalize_filename(path, NULL);
        gboolean ok = TRUE;
        const gchar* gs_input = abs;
        if (source == PS_SOURCE_DVI) {
            ps_path = g_strconcat(out, ".ps", NULL);
            gchar** argv = ps_dvips_argv(dvips, abs, ps_path, last_page);
            ok = run_tool(argv, cancellable, error);
            g_strfreev(argv);
            gs_input = ps_path;
        }
        if (ok) {
            gchar* lower = g_ascii_strdown(abs, -1);
            gboolean eps = g_str_has_suffix(lower, ".eps") || g_str_has_suffix(lower, ".epsf") ||
                           g_str_has_suffix(lower, ".epsi");
            g_free(lower);
            gchar** argv = ps_gs_argv(gs, gs_input, part, eps, source == PS_SOURCE_DVI ? 0 : last_page);
            ok = run_tool(argv, cancellable, error);
            g_strfreev(argv);
        }
        if (ps_path != NULL) {
            g_remove(ps_path);
        }
        if (ok && g_rename(part, out) == 0) {
            prune_cache(dir);
            result = out;
            out = NULL;
        } else {
            if (ok && error != NULL && *error == NULL) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "cannot store the converted file");
            }
            g_remove(part);
        }
        g_free(part);
        g_free(ps_path);
        g_free(abs);
    }
    g_free(out);
    g_free(dir);
    g_free(gs);
    g_free(dvips);
    return result;
}
