/* Vim regular expressions translated to GRegex (PCRE) */
#include "vim/vim_regex.h"
#include <string.h>

typedef enum { MAGIC_VERY_NO, MAGIC_NO, MAGIC_ON, MAGIC_VERY } MagicLevel;

/* Is c (escaped or not) an operator at this magic level? */
static gboolean is_special(gunichar c, gboolean escaped, MagicLevel level) {
    if (c < 128 && strchr("()|{}+?=@<>", (gchar)c) != NULL) {
        return level == MAGIC_VERY ? !escaped : escaped;
    }
    if (c < 128 && strchr(".*[~", (gchar)c) != NULL) {
        return level >= MAGIC_ON ? !escaped : escaped;
    }
    return FALSE;
}

static void append_literal(GString* out, gunichar c) {
    if (c < 128 && strchr("\\^$.|?*+()[]{}/", (gchar)c) != NULL) {
        g_string_append_c(out, '\\');
    }
    g_string_append_unichar(out, c);
}

/* Copy a [...] collection starting at p ('[' already consumed); returns the
 * position after ']' or NULL when unterminated */
static const gchar* copy_collection(const gchar* p, GString* out) {
    GString* cls = g_string_new("[");
    const gchar* q = p;
    if (*q == '^') {
        g_string_append_c(cls, '^');
        q++;
    }
    if (*q == ']') {
        g_string_append(cls, "\\]");
        q++;
    }
    while (*q != '\0' && *q != ']') {
        if (*q == '[' && q[1] == ':') {
            const gchar* end = strstr(q, ":]");
            if (end != NULL) {
                g_string_append_len(cls, q, end + 2 - q);
                q = end + 2;
                continue;
            }
        }
        if (*q == '\\' && q[1] != '\0') {
            gchar e = q[1];
            switch (e) {
                case 'n': g_string_append(cls, "\\n"); break;
                case 't': g_string_append(cls, "\\t"); break;
                case 'e': g_string_append(cls, "\\x1b"); break;
                case 'r': g_string_append(cls, "\\r"); break;
                case '\\': g_string_append(cls, "\\\\"); break;
                case ']': g_string_append(cls, "\\]"); break;
                case '^': g_string_append(cls, "\\^"); break;
                case '-': g_string_append(cls, "\\-"); break;
                case 'd': g_string_append(cls, "0-9"); break;
                default: g_string_append(cls, "\\\\"); g_string_append_c(cls, e); break;
            }
            q += 2;
            continue;
        }
        if (*q == '[') {
            g_string_append(cls, "\\[");
            q++;
            continue;
        }
        g_string_append_c(cls, *q);
        q++;
    }
    if (*q != ']') {
        g_string_free(cls, TRUE);
        return NULL;
    }
    g_string_append_c(cls, ']');
    g_string_append_len(out, cls->str, cls->len);
    g_string_free(cls, TRUE);
    return q + 1;
}

/* \{n,m} body after '{': returns the position after '}' */
static const gchar* copy_braces(const gchar* p, GString* out) {
    gboolean lazy = FALSE;
    if (*p == '-') {
        lazy = TRUE;
        p++;
    }
    const gchar* start = p;
    while (*p != '\0' && *p != '}' && !(*p == '\\' && p[1] == '}')) p++;
    gchar* body = g_strndup(start, p - start);
    if (*p == '\\') p++;
    if (*p == '}') p++;
    if (body[0] == '\0') {
        g_string_append_c(out, '*');
    } else if (body[0] == ',') {
        g_string_append_printf(out, "{0%s}", body);
    } else {
        g_string_append_printf(out, "{%s}", body);
    }
    if (lazy) g_string_append_c(out, '?');
    g_free(body);
    return p;
}

static const gchar* class_of(gchar c) {
    switch (c) {
        case 's': return "\\s";
        case 'S': return "\\S";
        case 'd': return "\\d";
        case 'D': return "\\D";
        case 'w': return "[0-9A-Za-z_]";
        case 'W': return "[^0-9A-Za-z_]";
        case 'a': return "[A-Za-z]";
        case 'A': return "[^A-Za-z]";
        case 'l': return "[a-z]";
        case 'L': return "[^a-z]";
        case 'u': return "[A-Z]";
        case 'U': return "[^A-Z]";
        case 'x': return "[0-9A-Fa-f]";
        case 'X': return "[^0-9A-Fa-f]";
        case 'o': return "[0-7]";
        case 'O': return "[^0-7]";
        case 'h': return "[A-Za-z_]";
        case 'H': return "[^A-Za-z_]";
        case 'k': case 'i': return "\\w";
        case 'K': case 'I': return "[^\\W\\d]";
        case 'f': return "[^\\s]";
        case 'p': return "\\S";
        default: return NULL;
    }
}

gchar* vim_regex_translate(const gchar* pattern, gboolean* caseless) {
    GString* out = g_string_new(NULL);
    MagicLevel level = MAGIC_ON;
    gint open_lookahead = 0;
    const gchar* p = pattern != NULL ? pattern : "";
    const gchar* begin = p;
    while (*p != '\0') {
        gboolean escaped = FALSE;
        if (*p == '\\' && p[1] != '\0') {
            escaped = TRUE;
            p++;
            gchar e = *p;
            /* Atoms that are always written with a backslash */
            switch (e) {
                case 'v': level = MAGIC_VERY; p++; continue;
                case 'm': level = MAGIC_ON; p++; continue;
                case 'M': level = MAGIC_NO; p++; continue;
                case 'V': level = MAGIC_VERY_NO; p++; continue;
                case 'c': if (caseless) *caseless = TRUE; p++; continue;
                case 'C': if (caseless) *caseless = FALSE; p++; continue;
                case 'n': g_string_append(out, "\\n"); p++; continue;
                case 't': g_string_append(out, "\\t"); p++; continue;
                case 'e': g_string_append(out, "\\x1b"); p++; continue;
                case 'r': g_string_append(out, "\\r"); p++; continue;
                case '\\': g_string_append(out, "\\\\"); p++; continue;
                case '/': g_string_append(out, "/"); p++; continue;
                case 'z':
                    if (p[1] == 's') {
                        g_string_append(out, "\\K");
                        p += 2;
                        continue;
                    }
                    if (p[1] == 'e') {
                        g_string_append(out, "(?=");
                        open_lookahead++;
                        p += 2;
                        continue;
                    }
                    break;
                case '_':
                    if (p[1] == '.') {
                        g_string_append(out, "[\\s\\S]");
                        p += 2;
                        continue;
                    }
                    if (p[1] != '\0' && class_of(p[1]) != NULL) {
                        g_string_append_printf(out, "(?:%s|\\n)", class_of(p[1]));
                        p += 2;
                        continue;
                    }
                    break;
                case '%':
                    if (p[1] == '(') {
                        g_string_append(out, "(?:");
                        p += 2;
                        continue;
                    }
                    break;
                default:
                    if (e >= '0' && e <= '9') {
                        g_string_append_printf(out, "\\%c", e);
                        p++;
                        continue;
                    }
                    if (class_of(e) != NULL) {
                        g_string_append(out, class_of(e));
                        p++;
                        continue;
                    }
                    break;
            }
        }
        gunichar c = g_utf8_get_char(p);
        p = g_utf8_next_char(p);
        if (!escaped && (c == '^' || c == '$')) {
            gboolean special = level != MAGIC_VERY_NO ||
                               (c == '^' && p - 1 == begin) || (c == '$' && *p == '\0');
            if (special) {
                g_string_append_c(out, (gchar)c);
            } else {
                append_literal(out, c);
            }
            continue;
        }
        if (!is_special(c, escaped, level)) {
            append_literal(out, c);
            continue;
        }
        switch (c) {
            case '(': g_string_append_c(out, '('); break;
            case ')': g_string_append_c(out, ')'); break;
            case '|': g_string_append_c(out, '|'); break;
            case '+': g_string_append_c(out, '+'); break;
            case '?': case '=': g_string_append_c(out, '?'); break;
            case '{': p = copy_braces(p, out); break;
            case '<': g_string_append(out, "\\b(?=\\w)"); break;
            case '>': g_string_append(out, "\\b(?<=\\w)"); break;
            case '.': g_string_append_c(out, '.'); break;
            case '*': g_string_append_c(out, '*'); break;
            case '[': {
                const gchar* after = copy_collection(p, out);
                if (after == NULL) {
                    g_string_append(out, "\\[");
                } else {
                    p = after;
                }
                break;
            }
            default: append_literal(out, c); break;
        }
    }
    while (open_lookahead-- > 0) g_string_append_c(out, ')');
    return g_string_free(out, FALSE);
}

static gboolean has_uppercase(const gchar* pattern) {
    for (const gchar* p = pattern; *p != '\0'; p = g_utf8_next_char(p)) {
        if (*p == '\\' && p[1] != '\0') {
            p++; /* \S, \W... are not letters of the pattern */
            continue;
        }
        if (g_unichar_isupper(g_utf8_get_char(p))) return TRUE;
    }
    return FALSE;
}

GRegex* vim_regex_compile(const gchar* pattern, gboolean ignorecase,
                          gboolean smartcase, GError** error) {
    gboolean caseless = ignorecase && !(smartcase && has_uppercase(pattern));
    gchar* pcre = vim_regex_translate(pattern, &caseless);
    GRegexCompileFlags flags = G_REGEX_MULTILINE;
    if (caseless) flags |= G_REGEX_CASELESS;
    GRegex* re = g_regex_new(pcre, flags, 0, error);
    g_free(pcre);
    return re;
}

/* ------------------------------------------------------------------------- */

typedef struct {
    GString* out;
    gchar once;   /* 'u' / 'l' for the next character */
    gchar until;  /* 'U' / 'L' until \e */
} Expander;

static void expand_append(Expander* x, const gchar* text, gssize len) {
    const gchar* end = text + (len < 0 ? (gssize)strlen(text) : len);
    for (const gchar* p = text; p < end; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (x->once == 'u') {
            c = g_unichar_toupper(c);
            x->once = 0;
        } else if (x->once == 'l') {
            c = g_unichar_tolower(c);
            x->once = 0;
        } else if (x->until == 'U') {
            c = g_unichar_toupper(c);
        } else if (x->until == 'L') {
            c = g_unichar_tolower(c);
        }
        g_string_append_unichar(x->out, c);
    }
}

static void expand_group(Expander* x, const GMatchInfo* match, gint n) {
    gint s = -1, e = -1;
    if (!g_match_info_fetch_pos(match, n, &s, &e) || s < 0) return;
    const gchar* subject = g_match_info_get_string(match);
    expand_append(x, subject + s, e - s);
}

gchar* vim_regex_expand(const gchar* replacement, const GMatchInfo* match) {
    Expander x = {g_string_new(NULL), 0, 0};
    for (const gchar* p = replacement != NULL ? replacement : ""; *p != '\0';) {
        if (*p == '&') {
            expand_group(&x, match, 0);
            p++;
            continue;
        }
        if (*p == '\\' && p[1] != '\0') {
            gchar e = p[1];
            p += 2;
            if (e >= '0' && e <= '9') {
                expand_group(&x, match, e - '0');
            } else if (e == 'r' || e == 'n') {
                expand_append(&x, "\n", 1);
            } else if (e == 't') {
                expand_append(&x, "\t", 1);
            } else if (e == 'u' || e == 'l') {
                x.once = e;
            } else if (e == 'U' || e == 'L') {
                x.until = e;
            } else if (e == 'e' || e == 'E') {
                x.until = 0;
                x.once = 0;
            } else {
                gchar lit[2] = {e, 0};
                expand_append(&x, lit, 1);
            }
            continue;
        }
        const gchar* next = g_utf8_next_char(p);
        expand_append(&x, p, next - p);
        p = next;
    }
    return g_string_free(x.out, FALSE);
}

/* ------------------------------------------------------------------------- */

static gchar* buffer_text(GtkTextBuffer* buf) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    /* get_slice keeps U+FFFC for embedded objects: char offsets stay aligned */
    return gtk_text_buffer_get_slice(buf, &s, &e, TRUE);
}

static void set_iters(GtkTextBuffer* buf, const gchar* text, gint bs, gint be,
                      GtkTextIter* ms, GtkTextIter* me) {
    glong cs = g_utf8_pointer_to_offset(text, text + bs);
    glong ce = cs + g_utf8_pointer_to_offset(text + bs, text + be);
    if (ms != NULL) gtk_text_buffer_get_iter_at_offset(buf, ms, (gint)cs);
    if (me != NULL) gtk_text_buffer_get_iter_at_offset(buf, me, (gint)ce);
}

/* First match starting at byte >= start_byte */
static gboolean match_from(GRegex* re, const gchar* text, gsize len, gsize start_byte,
                           gint* bs, gint* be) {
    GMatchInfo* mi = NULL;
    gboolean ok = start_byte <= len &&
                  g_regex_match_full(re, text, (gssize)len, (gint)start_byte, 0, &mi, NULL);
    if (ok) g_match_info_fetch_pos(mi, 0, bs, be);
    g_match_info_free(mi);
    return ok;
}

gboolean vim_regex_search(GtkTextBuffer* buf, GRegex* re, const GtkTextIter* from,
                          gboolean forward, gboolean wrap, gboolean* wrapped,
                          GtkTextIter* match_start, GtkTextIter* match_end) {
    if (wrapped != NULL) *wrapped = FALSE;
    if (re == NULL) return FALSE;
    gchar* text = buffer_text(buf);
    gsize len = strlen(text);
    gint from_off = gtk_text_iter_get_offset(from);
    gsize from_byte = (gsize)(g_utf8_offset_to_pointer(text, from_off) - text);
    gint bs = -1, be = -1;
    gboolean found = FALSE;
    if (forward) {
        gsize start = from_byte < len ? (gsize)(g_utf8_next_char(text + from_byte) - text) : len + 1;
        found = match_from(re, text, len, start, &bs, &be);
        if (!found && wrap) {
            found = match_from(re, text, len, 0, &bs, &be);
            if (found && wrapped != NULL) *wrapped = TRUE;
        }
    } else {
        GMatchInfo* mi = NULL;
        gint last_before_s = -1, last_before_e = -1, last_s = -1, last_e = -1;
        g_regex_match_full(re, text, (gssize)len, 0, 0, &mi, NULL);
        while (g_match_info_matches(mi)) {
            gint s, e;
            g_match_info_fetch_pos(mi, 0, &s, &e);
            if ((gsize)s < from_byte) {
                last_before_s = s;
                last_before_e = e;
            }
            last_s = s;
            last_e = e;
            g_match_info_next(mi, NULL);
        }
        g_match_info_free(mi);
        if (last_before_s >= 0) {
            found = TRUE;
            bs = last_before_s;
            be = last_before_e;
        } else if (wrap && last_s >= 0) {
            found = TRUE;
            bs = last_s;
            be = last_e;
            if (wrapped != NULL) *wrapped = TRUE;
        }
    }
    if (found) set_iters(buf, text, bs, be, match_start, match_end);
    g_free(text);
    return found;
}

gboolean vim_regex_search_at(GtkTextBuffer* buf, GRegex* re, const GtkTextIter* from,
                             GtkTextIter* match_start, GtkTextIter* match_end) {
    if (re == NULL) return FALSE;
    gchar* text = buffer_text(buf);
    gsize len = strlen(text);
    gsize from_byte = (gsize)(g_utf8_offset_to_pointer(text, gtk_text_iter_get_offset(from)) - text);
    gint bs = -1, be = -1;
    gboolean found = match_from(re, text, len, from_byte, &bs, &be);
    if (found) set_iters(buf, text, bs, be, match_start, match_end);
    g_free(text);
    return found;
}

gint vim_regex_count(GtkTextBuffer* buf, GRegex* re, const GtkTextIter* at, gint* index) {
    if (index != NULL) *index = 0;
    if (re == NULL) return 0;
    gchar* text = buffer_text(buf);
    gsize at_byte = at != NULL
        ? (gsize)(g_utf8_offset_to_pointer(text, gtk_text_iter_get_offset(at)) - text) : (gsize)-1;
    GMatchInfo* mi = NULL;
    gint n = 0;
    g_regex_match_full(re, text, -1, 0, 0, &mi, NULL);
    while (g_match_info_matches(mi)) {
        gint s, e;
        g_match_info_fetch_pos(mi, 0, &s, &e);
        n++;
        if ((gsize)s == at_byte && index != NULL) *index = n;
        g_match_info_next(mi, NULL);
    }
    g_match_info_free(mi);
    g_free(text);
    return n;
}

void vim_regex_tag_matches(GtkTextBuffer* buf, GRegex* re, GtkTextTag* tag, gint max_matches) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    gtk_text_buffer_remove_tag(buf, tag, &s, &e);
    if (re == NULL) return;
    gchar* text = buffer_text(buf);
    GMatchInfo* mi = NULL;
    gint last_byte = 0;
    glong last_char = 0;
    gint n = 0;
    g_regex_match_full(re, text, -1, 0, 0, &mi, NULL);
    while (g_match_info_matches(mi) && (max_matches <= 0 || n < max_matches)) {
        gint bs, be;
        g_match_info_fetch_pos(mi, 0, &bs, &be);
        if (be > bs) {
            glong cs = last_char + g_utf8_pointer_to_offset(text + last_byte, text + bs);
            glong ce = cs + g_utf8_pointer_to_offset(text + bs, text + be);
            last_byte = be;
            last_char = ce;
            GtkTextIter ms, me;
            gtk_text_buffer_get_iter_at_offset(buf, &ms, (gint)cs);
            gtk_text_buffer_get_iter_at_offset(buf, &me, (gint)ce);
            gtk_text_buffer_apply_tag(buf, tag, &ms, &me);
            n++;
        }
        g_match_info_next(mi, NULL);
    }
    g_match_info_free(mi);
    g_free(text);
}
