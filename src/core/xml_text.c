/* The visible text of an XML line (see xml_text.h) */
#include "core/xml_text.h"

#include <string.h>

/* The character of the entity at s ('&' included) and its length in end;
 * 0 when it is not one */
static gunichar entity_at(const gchar* s, const gchar** end) {
    static const struct {
        const gchar* name;
        gunichar c;
    } NAMED[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}};
    for (gsize i = 0; i < G_N_ELEMENTS(NAMED); i++) {
        gsize n = strlen(NAMED[i].name);
        if (strncmp(s, NAMED[i].name, n) == 0) {
            *end = s + n;
            return NAMED[i].c;
        }
    }
    if (s[1] != '#') return 0;
    gboolean hex = s[2] == 'x' || s[2] == 'X';
    const gchar* digits = s + (hex ? 3 : 2);
    gchar* stop = NULL;
    guint64 v = g_ascii_strtoull(digits, &stop, hex ? 16 : 10);
    if (stop == digits || *stop != ';' || v == 0 || !g_unichar_validate((gunichar)v)) return 0;
    *end = stop + 1;
    return (gunichar)v;
}

gchar* xml_text_line(const gchar* line, gint* column, gint* length) {
    if (line == NULL || column == NULL || length == NULL) return NULL;
    gint from = *column;
    gint to = *column + MAX(*length, 0);
    gint len = (gint)strlen(line);
    if (from < 0 || to > len) return NULL;
    /* A '>' before any '<' ends a tag begun on the previous line */
    const gchar* lt = strchr(line, '<');
    const gchar* gt = strchr(line, '>');
    gboolean in_tag = gt != NULL && (lt == NULL || gt < lt);
    GString* out = g_string_sized_new((gsize)len);
    gint new_from = -1;
    gint new_to = -1;
    const gchar* p = line;
    while (TRUE) {
        gint at = (gint)(p - line);
        if (at == from && !in_tag) new_from = (gint)out->len;
        if (at == to && !in_tag) new_to = (gint)out->len;
        if (*p == '\0') break;
        const gchar* next = p + 1;
        gboolean markup = TRUE;
        if (in_tag) {
            if (*p == '>') in_tag = FALSE;
        } else if (*p == '<') {
            in_tag = TRUE;
        } else if (*p == '&' && entity_at(p, &next) != 0) {
            g_string_append_unichar(out, entity_at(p, &next));
        } else {
            g_string_append_c(out, *p);
            markup = FALSE;
        }
        /* The match may not cover markup, nor begin or end inside it */
        if (markup && at < to && (gint)(next - line) > from) {
            g_string_free(out, TRUE);
            return NULL;
        }
        p = next;
    }
    if (new_from < 0 || new_to < 0) {
        g_string_free(out, TRUE);
        return NULL;
    }
    *column = new_from;
    *length = new_to - new_from;
    return g_string_free(out, FALSE);
}
