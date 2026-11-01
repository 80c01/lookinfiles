/* Path patterns (see path_globs.h) */
#include "path_globs.h"

#include <string.h>

typedef struct {
    GPatternSpec* spec;
    gboolean anchored; /* from the root: the path and its folders */
    gboolean dir_only; /* folders only (trailing slash) */
} Glob;

struct PathGlobs {
    GArray* globs; /* Glob */
};

PathGlobs* path_globs_new(const gchar* list) {
    if (list == NULL) return NULL;
    GArray* globs = g_array_new(FALSE, FALSE, sizeof(Glob));
    gchar** parts = g_strsplit(list, ",", -1);
    for (gchar** p = parts; *p != NULL; p++) {
        gchar* pat = g_strstrip(*p);
        Glob g = {NULL, FALSE, FALSE};
        gsize n = strlen(pat);
        while (n > 0 && pat[n - 1] == '/') {
            pat[--n] = '\0';
            g.dir_only = TRUE;
        }
        while (pat[0] == '/') {
            pat++;
            g.anchored = TRUE;
        }
        if (pat[0] == '\0') continue;
        if (strchr(pat, '/') != NULL) g.anchored = TRUE;
        g.spec = g_pattern_spec_new(pat);
        g_array_append_val(globs, g);
    }
    g_strfreev(parts);
    if (globs->len == 0) {
        g_array_unref(globs);
        return NULL;
    }
    PathGlobs* pg = g_new0(PathGlobs, 1);
    pg->globs = globs;
    return pg;
}

void path_globs_free(PathGlobs* globs) {
    if (globs == NULL) return;
    for (guint i = 0; i < globs->globs->len; i++) {
        g_pattern_spec_free(g_array_index(globs->globs, Glob, i).spec);
    }
    g_array_unref(globs->globs);
    g_free(globs);
}

static gboolean match_part(const Glob* g, const gchar* s, gsize len) {
    gchar* part = g_strndup(s, len);
    gboolean hit = g_pattern_spec_match_string(g->spec, part);
    g_free(part);
    return hit;
}

/* The candidates of rel, at each end of a name: from the root (anchored) or
 * the name alone; the file name itself unless dir_only */
static gboolean glob_match(const Glob* g, const gchar* rel) {
    const gchar* name = rel;
    for (const gchar* c = rel;; c++) {
        if (*c != '/' && *c != '\0') continue;
        gboolean is_file = *c == '\0';
        if (!(is_file && g->dir_only) && c > name) {
            if (g->anchored ? match_part(g, rel, (gsize)(c - rel)) : match_part(g, name, (gsize)(c - name))) {
                return TRUE;
            }
        }
        if (is_file) return FALSE;
        name = c + 1;
    }
}

gboolean path_globs_match(const PathGlobs* globs, const gchar* rel) {
    if (globs == NULL || rel == NULL) return FALSE;
    for (guint i = 0; i < globs->globs->len; i++) {
        if (glob_match(&g_array_index(globs->globs, Glob, i), rel)) return TRUE;
    }
    return FALSE;
}
