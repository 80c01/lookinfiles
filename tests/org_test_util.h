/* Helpers for the Org-mode unit tests: a text with a cursor marker "‸" is
 * turned into lines + OrgPos, and back. */
#ifndef ORG_TEST_UTIL_H
#define ORG_TEST_UTIL_H

#include "org_doc.h"

#include <string.h>

#define CARET "\xe2\x80\xb8" /* ‸ */

static inline GPtrArray* doc_new(const gchar* text, OrgPos* pos) {
    const gchar* c = strstr(text, CARET);
    gchar* plain;
    pos->line = 0;
    pos->col = 0;
    if (c != NULL) {
        GString* s = g_string_new_len(text, c - text);
        for (const gchar* p = text; p < c; p++) {
            if (*p == '\n') pos->line++;
        }
        const gchar* ls = g_strrstr_len(text, c - text, "\n");
        ls = ls != NULL ? ls + 1 : text;
        pos->col = (gint)g_utf8_strlen(ls, c - ls);
        g_string_append(s, c + strlen(CARET));
        plain = g_string_free(s, FALSE);
    } else {
        plain = g_strdup(text);
    }
    GPtrArray* lines = org_lines_new(plain);
    g_free(plain);
    return lines;
}

/* Lines joined with the caret put back at pos; frees lines */
static inline gchar* doc_text(GPtrArray* lines, const OrgPos* pos) {
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < lines->len; i++) {
        const gchar* l = g_ptr_array_index(lines, i);
        if (i > 0) g_string_append_c(s, '\n');
        if ((gint)i == pos->line) {
            gint b = org_col_to_byte(l, pos->col);
            g_string_append_len(s, l, b);
            g_string_append(s, CARET);
            g_string_append(s, l + b);
        } else {
            g_string_append(s, l);
        }
    }
    g_ptr_array_unref(lines);
    return g_string_free(s, FALSE);
}

#endif /* ORG_TEST_UTIL_H */
