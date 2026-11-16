/* SyncTeX places of a LaTeX source (see latex_synctex.h) */
#include "latex/latex_synctex.h"

#include <stdlib.h>
#include <string.h>

gchar* latex_synctex_file(const gchar* pdf) {
    if (pdf == NULL || !g_str_has_suffix(pdf, ".pdf")) return NULL;
    gchar* stem = g_strndup(pdf, strlen(pdf) - strlen(".pdf"));
    static const gchar* const exts[] = {".synctex.gz", ".synctex"};
    gchar* found = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(exts) && found == NULL; i++) {
        gchar* path = g_strconcat(stem, exts[i], NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            found = path;
        } else {
            g_free(path);
        }
    }
    g_free(stem);
    return found;
}

gboolean latex_synctex_parse_view(const gchar* output, gint* page, gdouble* y) {
    if (output == NULL) return FALSE;
    const gchar* begin = strstr(output, "SyncTeX result begin");
    if (begin == NULL) return FALSE;
    gint found_page = -1;
    gboolean found_y = FALSE;
    gchar** lines = g_strsplit(begin, "\n", -1);
    for (gchar** l = lines; *l != NULL; l++) {
        g_strchomp(*l);
        if (g_str_has_prefix(*l, "SyncTeX result end")) break;
        /* The first record only: the next one starts with another Output: */
        if (g_str_has_prefix(*l, "Output:") && found_page >= 0) break;
        if (g_str_has_prefix(*l, "Page:") && found_page < 0) {
            gchar* end = NULL;
            gint64 n = g_ascii_strtoll(*l + 5, &end, 10);
            if (end != *l + 5 && n >= 1) found_page = (gint)n - 1;
        } else if (g_str_has_prefix(*l, "y:") && found_page >= 0 && !found_y) {
            gchar* end = NULL;
            gdouble v = g_ascii_strtod(*l + 2, &end);
            if (end != *l + 2) {
                *y = v;
                found_y = TRUE;
            }
        }
    }
    g_strfreev(lines);
    if (found_page < 0 || !found_y) return FALSE;
    *page = found_page;
    return TRUE;
}
