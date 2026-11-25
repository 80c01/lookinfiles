#ifndef WSL_DISTROS_H
#define WSL_DISTROS_H

/* The WSL distributions of this computer (Windows) for the Machines view:
 * the output of `wsl.exe -l -v` and `wsl.exe -l --running -q` read (UTF-16
 * as wsl.exe writes it, or UTF-8 with WSL_UTF8=1), the folder a
 * distribution's files are seen at (\\wsl.localhost\<name>\) and the parts
 * of such UNC paths. Pure (no I/O), unit-tested on every system. */
#include <glib.h>

typedef struct {
    gchar* name;        /* "Ubuntu-24.04" */
    gint version;       /* 1 or 2, 0 when unknown */
    gboolean is_default; /* the "*" of wsl -l -v */
    gboolean running;
} WslDistro;

void wsl_distro_free(gpointer distro);

/* Text of wsl.exe's output: UTF-16LE (with or without a byte order mark)
 * decoded, else the bytes as UTF-8 (made valid); "\r" removed. Caller frees. */
gchar* wsl_output_to_utf8(const gchar* bytes, gsize len);

/* The distributions of a `wsl -l -v` table (its header and the
 * columns' words are translated by Windows: only the first word, the "*"
 * and the last number are read), without Docker Desktop's own
 * (docker-desktop, docker-desktop-data): WslDistro, wsl_distro_free; the
 * default first, then by name. Never NULL. */
GPtrArray* wsl_distros_parse(const gchar* verbose);

/* Mark running the distributions named in `wsl -l --running -q` output */
void wsl_distros_mark_running(GPtrArray* distros, const gchar* running);

/* The distribution of distros named name, NULL when none (borrowed) */
WslDistro* wsl_distros_find(GPtrArray* distros, const gchar* name);

/* Folder of a distribution's files: "\\wsl.localhost\<name>\". Caller frees. */
gchar* wsl_distro_path(const gchar* name);

/* Length of the "\\host\share" start of a UNC path (either slash), 0 when
 * path is not one; *host and *share (may be NULL) get those parts. Caller
 * frees them. */
gsize wsl_unc_root(const gchar* path, gchar** host, gchar** share);

/* The distribution path is in ("\\wsl.localhost\Ubuntu\home" or
 * "\\wsl$\Ubuntu"), NULL for another path. Caller frees. */
gchar* wsl_distro_of_path(const gchar* path);

#endif /* WSL_DISTROS_H */
