/* The environments of a LaTeX text (see latex_edit.h) */
#include "latex/latex_edit.h"

#include <string.h>

#define NAME_MAX_LEN 64

typedef struct {
    LatexEnvToken tok;
    gint partner; /* index of the other token, -1 when none */
} Token;

gboolean latex_edit_valid_name(const gchar* name, gssize len) {
    if (name == NULL) return FALSE;
    gsize n = len < 0 ? strlen(name) : (gsize)len;
    if (n == 0 || n > NAME_MAX_LEN) return FALSE;
    for (gsize i = 0; i < n; i++) {
        gchar c = name[i];
        if (!g_ascii_isalnum(c) && c != '*' && c != '@' && c != '-' && c != ':' && c != '_') return FALSE;
    }
    return TRUE;
}

static gboolean is_verbatim(const gchar* name, gsize n) {
    static const gchar* const NAMES[] = {
        "verbatim", "verbatim*", "Verbatim", "Verbatim*", "BVerbatim", "lstlisting", "minted", "comment",
        "filecontents", "filecontents*",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(NAMES); i++) {
        if (strlen(NAMES[i]) == n && memcmp(NAMES[i], name, n) == 0) return TRUE;
    }
    return FALSE;
}

/* A \begin{name} / \end{name} at p (its backslash), or FALSE */
static gboolean read_token(const gchar* text, gsize len, gsize p, LatexEnvToken* tok) {
    gsize q = p + 1;
    gboolean begin;
    if (len - q >= 5 && memcmp(text + q, "begin", 5) == 0) {
        begin = TRUE;
        q += 5;
    } else if (len - q >= 3 && memcmp(text + q, "end", 3) == 0) {
        begin = FALSE;
        q += 3;
    } else {
        return FALSE;
    }
    if (q < len && g_ascii_isalpha(text[q])) return FALSE; /* \beginx, \endgraf */
    while (q < len && (text[q] == ' ' || text[q] == '\t')) q++;
    if (q >= len || text[q] != '{') return FALSE;
    gsize name = q + 1;
    gsize close = name;
    while (close < len && close - name <= NAME_MAX_LEN && text[close] != '}' && text[close] != '{' &&
           text[close] != '\\' && text[close] != '\n' && text[close] != '%') {
        close++;
    }
    if (close >= len || text[close] != '}') return FALSE;
    tok->begin = begin;
    tok->start = p;
    tok->end = close + 1;
    tok->name_start = name;
    tok->name_end = close;
    return TRUE;
}

static gboolean same_name(const gchar* text, const LatexEnvToken* a, const LatexEnvToken* b) {
    gsize n = a->name_end - a->name_start;
    return n == b->name_end - b->name_start && memcmp(text + a->name_start, text + b->name_start, n) == 0;
}

/* Every token of text, paired */
static GArray* tokens_of(const gchar* text, gsize len) {
    GArray* out = g_array_new(FALSE, FALSE, sizeof(Token));
    GArray* open = g_array_new(FALSE, FALSE, sizeof(gint)); /* indexes of the \begin still open */
    gsize p = 0;
    while (p < len) {
        gchar c = text[p];
        if (c == '%') {
            const gchar* nl = memchr(text + p, '\n', len - p);
            p = nl != NULL ? (gsize)(nl - text) : len;
            continue;
        }
        if (c != '\\') {
            p++;
            continue;
        }
        Token t = {{0}, -1};
        if (!read_token(text, len, p, &t.tok)) {
            p += 2; /* \% \\ \{ ... */
            continue;
        }
        gint index = (gint)out->len;
        g_array_append_val(out, t);
        p = t.tok.end;
        if (t.tok.begin) {
            g_array_append_val(open, index);
            gsize n = t.tok.name_end - t.tok.name_start;
            if (is_verbatim(text + t.tok.name_start, n)) {
                /* Only its own \end counts inside */
                gchar* end = g_strdup_printf("\\end{%.*s}", (gint)n, text + t.tok.name_start);
                const gchar* found = g_strstr_len(text + p, (gssize)(len - p), end);
                g_free(end);
                p = found != NULL ? (gsize)(found - text) : len;
            }
            continue;
        }
        /* An \end closes the innermost open \begin of its name; the ones
         * opened after it are left open */
        for (gint i = (gint)open->len - 1; i >= 0; i--) {
            gint b = g_array_index(open, gint, i);
            Token* bt = &g_array_index(out, Token, b);
            if (same_name(text, &bt->tok, &t.tok)) {
                bt->partner = index;
                g_array_index(out, Token, index).partner = b;
                g_array_set_size(open, (guint)i);
                break;
            }
        }
    }
    g_array_free(open, TRUE);
    return out;
}

gboolean latex_edit_token_at(const gchar* text, gsize len, gsize offset, gboolean name_only, LatexEnvToken* token) {
    if (text == NULL || offset > len) return FALSE;
    /* The token is on the line of offset: read from the start of that line */
    gsize ls = offset;
    while (ls > 0 && text[ls - 1] != '\n') ls--;
    gsize le = offset;
    while (le < len && text[le] != '\n') le++;
    for (gsize p = ls; p < le; p++) {
        if (text[p] == '%' && (p == ls || text[p - 1] != '\\')) break;
        LatexEnvToken t;
        if (text[p] == '\\' && read_token(text, len, p, &t)) {
            gsize from = name_only ? t.name_start : t.start;
            gsize to = name_only ? t.name_end : t.end;
            if (offset >= from && offset <= to) {
                *token = t;
                return TRUE;
            }
            p = t.end - 1;
        } else if (text[p] == '\\') {
            p++;
        }
    }
    return FALSE;
}

gboolean latex_edit_partner(const gchar* text, gsize len, const LatexEnvToken* token, LatexEnvToken* partner) {
    if (text == NULL || token == NULL) return FALSE;
    GArray* toks = tokens_of(text, len);
    gboolean found = FALSE;
    for (guint i = 0; i < toks->len; i++) {
        const Token* t = &g_array_index(toks, Token, i);
        if (t->tok.start != token->start) continue;
        if (t->partner >= 0) {
            *partner = g_array_index(toks, Token, t->partner).tok;
            found = TRUE;
        }
        break;
    }
    g_array_free(toks, TRUE);
    return found;
}

gboolean latex_edit_enclosing(const gchar* text, gsize len, gsize offset, LatexEnvToken* begin, LatexEnvToken* end,
                              gboolean* closed) {
    if (text == NULL || offset > len) return FALSE;
    GArray* toks = tokens_of(text, len);
    gint best = -1;
    for (guint i = 0; i < toks->len; i++) {
        const Token* t = &g_array_index(toks, Token, i);
        if (t->tok.start >= offset) break;
        if (!t->tok.begin || offset < t->tok.end) continue;
        if (t->partner >= 0 && g_array_index(toks, Token, t->partner).tok.end < offset) continue;
        best = (gint)i; /* the last one that holds offset is the innermost */
    }
    if (best >= 0) {
        const Token* b = &g_array_index(toks, Token, best);
        *begin = b->tok;
        *closed = b->partner >= 0;
        if (b->partner >= 0) *end = g_array_index(toks, Token, b->partner).tok;
    }
    g_array_free(toks, TRUE);
    return best >= 0;
}

/* After an argument list "[..]{..}..." from p up to stop (white space too) */
static gboolean only_arguments(const gchar* text, gsize p, gsize stop) {
    gint depth = 0;
    gchar open = 0;
    for (; p < stop; p++) {
        gchar c = text[p];
        if (depth == 0) {
            if (c == ' ' || c == '\t' || c == '\r') continue;
            if (c != '[' && c != '{') return FALSE;
            open = c;
            depth = 1;
        } else if (c == '\\') {
            p++;
        } else if (c == open) {
            depth++;
        } else if (c == (open == '[' ? ']' : '}')) {
            depth--;
        }
    }
    return depth == 0;
}

gchar* latex_edit_env_to_close(const gchar* text, gsize len, gsize offset) {
    if (text == NULL || offset > len) return NULL;
    gsize ls = offset;
    while (ls > 0 && text[ls - 1] != '\n') ls--;
    GArray* toks = tokens_of(text, len);
    gchar* name = NULL;
    for (guint i = 0; i < toks->len; i++) {
        const Token* t = &g_array_index(toks, Token, i);
        if (t->tok.start < ls) continue;
        if (t->tok.end > offset) break;
        /* The last \begin of the line before offset, open, then arguments only */
        if (t->tok.begin && t->partner < 0 && only_arguments(text, t->tok.end, offset) &&
            (i + 1 >= toks->len || g_array_index(toks, Token, i + 1).tok.start >= offset)) {
            name = g_strndup(text + t->tok.name_start, t->tok.name_end - t->tok.name_start);
            if (!latex_edit_valid_name(name, -1)) g_clear_pointer(&name, g_free);
        }
    }
    g_array_free(toks, TRUE);
    return name;
}
