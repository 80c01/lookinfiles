#ifndef ORG_DOC_PRIVATE_H
#define ORG_DOC_PRIVATE_H

/* Line helpers shared by org_doc.c and org_list.c */
#include "org_doc.h"

static inline gint org_doc_n_lines(GPtrArray* lines) {
    return (gint)lines->len;
}

/* Lines that hold content: a final empty line (the text ended with a
 * newline) never belongs to a subtree or an item. */
static inline gint org_doc_limit(GPtrArray* lines) {
    gint n = org_doc_n_lines(lines);
    if (n > 1 && org_line(lines, n - 1)[0] == '\0') return n - 1;
    return n;
}

static inline void org_doc_set_line(GPtrArray* lines, gint i, gchar* text) {
    g_free(g_ptr_array_index(lines, i));
    g_ptr_array_index(lines, i) = text;
}

static inline void org_doc_insert_line(GPtrArray* lines, gint i, gchar* text) {
    g_ptr_array_insert(lines, i, text);
}

static inline void org_doc_swap_blocks(GPtrArray* lines, gint start, gint mid, gint end) {
    gint n = end - start;
    gpointer* tmp = g_new(gpointer, n);
    gint k = 0;
    for (gint i = mid; i < end; i++) tmp[k++] = g_ptr_array_index(lines, i);
    for (gint i = start; i < mid; i++) tmp[k++] = g_ptr_array_index(lines, i);
    for (gint i = 0; i < n; i++) g_ptr_array_index(lines, start + i) = tmp[i];
    g_free(tmp);
}

static inline gboolean org_doc_is_blank(const gchar* s) {
    for (; *s != '\0'; s++) {
        if (*s != ' ' && *s != '\t') return FALSE;
    }
    return TRUE;
}

/* Leading whitespace width (tab = 8 columns) */
static inline gint org_doc_indent_of(const gchar* s) {
    gint n = 0;
    for (; *s == ' ' || *s == '\t'; s++) n += (*s == '\t') ? 8 : 1;
    return n;
}

/* Byte length of the leading whitespace */
static inline gint org_doc_indent_bytes(const gchar* s) {
    gint n = 0;
    while (s[n] == ' ' || s[n] == '\t') n++;
    return n;
}

static inline void org_doc_clamp_pos(GPtrArray* lines, OrgPos* pos) {
    if (pos->line < 0) pos->line = 0;
    if (pos->line >= org_doc_n_lines(lines)) pos->line = org_doc_n_lines(lines) - 1;
    gint w = org_utf8_width(org_line(lines, pos->line));
    if (pos->col < 0) pos->col = 0;
    if (pos->col > w) pos->col = w;
}

static inline void org_doc_set_msg(gchar** msg, const gchar* text) {
    if (msg != NULL) {
        g_free(*msg);
        *msg = g_strdup(text);
    }
}

#endif /* ORG_DOC_PRIVATE_H */
