#include "core/wsl_distros.h"

#include <string.h>

void wsl_distro_free(gpointer data) {
    WslDistro* d = data;
    if (d == NULL) {
        return;
    }
    g_free(d->name);
    g_free(d);
}

/* bytes look like UTF-16LE: a byte order mark, or NULs in most odd places */
static gboolean looks_utf16le(const guchar* b, gsize len) {
    if (len >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        return TRUE;
    }
    gsize pairs = len / 2, zeros = 0;
    for (gsize i = 0; i < pairs; i++) {
        if (b[2 * i + 1] == 0) {
            zeros++;
        }
    }
    return pairs > 0 && zeros * 4 >= pairs * 3;
}

gchar* wsl_output_to_utf8(const gchar* bytes, gsize len) {
    if (bytes == NULL || len == 0) {
        return g_strdup("");
    }
    const guchar* b = (const guchar*)bytes;
    gchar* text = NULL;
    if (looks_utf16le(b, len)) {
        gsize skip = (len >= 2 && b[0] == 0xFF && b[1] == 0xFE) ? 2 : 0;
        glong n = (glong)((len - skip) / 2);
        gunichar2* units = g_new(gunichar2, n + 1);
        for (glong i = 0; i < n; i++) {
            units[i] = (gunichar2)(b[skip + 2 * i] | (b[skip + 2 * i + 1] << 8));
        }
        units[n] = 0;
        text = g_utf16_to_utf8(units, n, NULL, NULL, NULL);
        g_free(units);
    }
    if (text == NULL) {
        text = g_utf8_make_valid(bytes, (gssize)len);
    }
    /* NULs left (a broken UTF-16 tail) and the "\r" of "\r\n" */
    GString* out = g_string_new(NULL);
    for (const gchar* p = text; *p != '\0'; p++) {
        if (*p != '\r') {
            g_string_append_c(out, *p);
        }
    }
    g_free(text);
    return g_string_free(out, FALSE);
}

static gboolean hidden_distro(const gchar* name) {
    return g_str_has_prefix(name, "docker-desktop");
}

static gint compare_distros(gconstpointer pa, gconstpointer pb) {
    const WslDistro* a = *(const WslDistro* const*)pa;
    const WslDistro* b = *(const WslDistro* const*)pb;
    if (a->is_default != b->is_default) {
        return a->is_default ? -1 : 1;
    }
    return g_ascii_strcasecmp(a->name, b->name);
}

GPtrArray* wsl_distros_parse(const gchar* verbose) {
    GPtrArray* out = g_ptr_array_new_with_free_func(wsl_distro_free);
    if (verbose == NULL) {
        return out;
    }
    gchar** lines = g_strsplit(verbose, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = g_strstrip(lines[i]);
        gboolean is_default = FALSE;
        if (line[0] == '*') {
            is_default = TRUE;
            line = g_strchug(line + 1);
        }
        gchar** words = g_strsplit_set(line, " \t", -1);
        gchar* first = NULL;
        gchar* last = NULL;
        guint n = 0;
        for (gint w = 0; words[w] != NULL; w++) {
            if (words[w][0] == '\0') {
                continue;
            }
            if (first == NULL) {
                first = words[w];
            }
            last = words[w];
            n++;
        }
        /* A distribution: name, state (one or more words), version number;
         * the header's last word is not a number */
        if (n >= 3 && g_ascii_isdigit(last[0]) && last[1] == '\0' && !hidden_distro(first) &&
            wsl_distros_find(out, first) == NULL) {
            WslDistro* d = g_new0(WslDistro, 1);
            d->name = g_strdup(first);
            d->version = last[0] - '0';
            d->is_default = is_default;
            g_ptr_array_add(out, d);
        }
        g_strfreev(words);
    }
    g_strfreev(lines);
    g_ptr_array_sort(out, compare_distros);
    return out;
}

void wsl_distros_mark_running(GPtrArray* distros, const gchar* running) {
    if (distros == NULL || running == NULL) {
        return;
    }
    gchar** lines = g_strsplit(running, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        WslDistro* d = wsl_distros_find(distros, g_strstrip(lines[i]));
        if (d != NULL) {
            d->running = TRUE;
        }
    }
    g_strfreev(lines);
}

WslDistro* wsl_distros_find(GPtrArray* distros, const gchar* name) {
    for (guint i = 0; distros != NULL && name != NULL && i < distros->len; i++) {
        WslDistro* d = g_ptr_array_index(distros, i);
        if (strcmp(d->name, name) == 0) {
            return d;
        }
    }
    return NULL;
}

gchar* wsl_distro_path(const gchar* name) {
    return g_strconcat("\\\\wsl.localhost\\", name != NULL ? name : "", "\\", NULL);
}

static gboolean is_sep(gchar c) {
    return c == '\\' || c == '/';
}

gsize wsl_unc_root(const gchar* path, gchar** host, gchar** share) {
    if (host != NULL) *host = NULL;
    if (share != NULL) *share = NULL;
    if (path == NULL || !is_sep(path[0]) || !is_sep(path[1]) || path[2] == '\0' || is_sep(path[2])) {
        return 0;
    }
    const gchar* h = path + 2;
    const gchar* he = h;
    while (*he != '\0' && !is_sep(*he)) he++;
    if (*he == '\0' || he[1] == '\0' || is_sep(he[1])) {
        return 0; /* "\\host" alone: no share */
    }
    const gchar* s = he + 1;
    const gchar* se = s;
    while (*se != '\0' && !is_sep(*se)) se++;
    if (host != NULL) *host = g_strndup(h, he - h);
    if (share != NULL) *share = g_strndup(s, se - s);
    return (gsize)(se - path);
}

gchar* wsl_distro_of_path(const gchar* path) {
    gchar* host = NULL;
    gchar* share = NULL;
    if (wsl_unc_root(path, &host, &share) == 0) {
        return NULL;
    }
    gboolean wsl = g_ascii_strcasecmp(host, "wsl.localhost") == 0 || g_ascii_strcasecmp(host, "wsl$") == 0;
    g_free(host);
    if (!wsl) {
        g_free(share);
        return NULL;
    }
    return share;
}
