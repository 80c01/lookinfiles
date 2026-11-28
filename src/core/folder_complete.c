/* Folder field completion helpers (see folder_complete.h) */
#include "core/folder_complete.h"

#include <string.h>

gboolean folder_complete_split(const gchar* text, const gchar* home, gchar** dir, gchar** prefix) {
    if (text == NULL) return FALSE;
    gboolean tilde = text[0] == '~' && (text[1] == '\0' || text[1] == '/');
    if (text[0] != '/' && !tilde) return FALSE;
    if (tilde && text[1] == '\0') {
        /* "~": the home folder's entries */
        *dir = g_strdup(home);
        *prefix = g_strdup("");
        return TRUE;
    }
    const gchar* slash = strrchr(text, '/');
    gchar* head = g_strndup(text, (gsize)(slash - text));
    if (tilde) {
        gchar* full = g_strconcat(home, head + 1, NULL);
        g_free(head);
        head = full;
    }
    if (head[0] == '\0') {
        g_free(head);
        head = g_strdup("/");
    }
    *dir = head;
    *prefix = g_strdup(slash + 1);
    return TRUE;
}

gchar* folder_complete_join(const gchar* text, const gchar* name) {
    if (g_strcmp0(text, "~") == 0) return g_strconcat("~/", name, NULL);
    const gchar* slash = text != NULL ? strrchr(text, '/') : NULL;
    if (slash == NULL) return g_strdup(name);
    gchar* head = g_strndup(text, (gsize)(slash - text) + 1);
    gchar* out = g_strconcat(head, name, NULL);
    g_free(head);
    return out;
}

gint folder_complete_name_score(const gchar* name, const gchar* prefix) {
    if (name[0] == '.' && prefix[0] != '.') return -1;
    if (g_str_has_prefix(name, prefix)) return 2;
    gchar* n = g_utf8_casefold(name, -1);
    gchar* p = g_utf8_casefold(prefix, -1);
    gint score = g_str_has_prefix(n, p) ? 1 : strstr(n, p) != NULL ? 0 : -1;
    g_free(n);
    g_free(p);
    return score;
}

gboolean folder_complete_is_python(const gchar* name) {
    const gchar* rest = g_str_has_prefix(name, "python") ? name + 6 : g_str_has_prefix(name, "pypy") ? name + 4 : NULL;
    if (rest == NULL) return FALSE;
    while (g_ascii_isdigit(*rest) || *rest == '.') {
        if (g_ascii_strcasecmp(rest, ".exe") == 0) return TRUE;
        rest++;
    }
    return *rest == '\0' || g_ascii_strcasecmp(rest, ".exe") == 0;
}

gchar* folder_complete_tilde(const gchar* path, const gchar* home) {
    gsize n = home != NULL ? strlen(home) : 0;
    if (n > 1 && strncmp(path, home, n) == 0 && (path[n] == '\0' || path[n] == '/')) {
        return g_strconcat("~", path + n, NULL);
    }
    return g_strdup(path);
}
