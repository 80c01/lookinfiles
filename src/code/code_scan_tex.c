/* The built-in scanner of LaTeX and BibTeX (see code_scan_tex.h) */
#include "code/code_scan_tex.h"

#include <string.h>

/* The longest argument read ({title}, {key}): an unclosed brace stops there */
#define GROUP_MAX 4096
/* Heading level of a beamer frame (below \subparagraph) */
#define FRAME_LEVEL 8

typedef struct {
    const gchar* text;
    const gchar* end;
    const gchar* p;
    gint line;              /* of p, 0-based */
    const gchar* line_start;
    const gchar* only;      /* symbols named so only, or NULL */
    gsize only_len;
    GPtrArray* symbols;     /* CodeSymbol*, or NULL */
    GPtrArray* outline;     /* CodeOutlineEntry*, or NULL */
    gboolean frame_untitled; /* in a frame that has no title yet */
} Scan;

/* A place in the text: its line and the start of that line */
typedef struct {
    const gchar* at;
    gint line;
    const gchar* line_start;
} Place;

static void move_to(Scan* s, const gchar* to) {
    if (to > s->end) to = s->end;
    for (const gchar* q = s->p; q < to; q++) {
        if (*q == '\n') {
            s->line++;
            s->line_start = q + 1;
        }
    }
    s->p = to;
}

static Place here(const Scan* s) {
    Place pl = {s->p, s->line, s->line_start};
    return pl;
}

static gboolean is_letter(gchar c) {
    return g_ascii_isalpha(c) || c == '@';
}

/* Spaces and tabs, and new lines too when lines is TRUE */
static void skip_spaces(Scan* s, gboolean lines) {
    const gchar* q = s->p;
    while (q < s->end && (*q == ' ' || *q == '\t' || *q == '\r' || (lines && *q == '\n'))) q++;
    move_to(s, q);
}

/* The end of a comment: the new line ending it (or the end of the text) */
static const gchar* comment_end(const Scan* s, const gchar* q) {
    const gchar* nl = memchr(q, '\n', (gsize)(s->end - q));
    return nl != NULL ? nl : s->end;
}

/* The closing bracket matching the one at q (braces, then close), over
 * escapes and comments; NULL when there is none within GROUP_MAX bytes */
static const gchar* matching(const Scan* s, const gchar* q, gchar close) {
    gint depth = 0;
    const gchar* lim = s->end - q > GROUP_MAX ? q + GROUP_MAX : s->end;
    for (q++; q < lim; q++) {
        if (*q == '\\') {
            q++;
        } else if (*q == '%') {
            q = comment_end(s, q);
        } else if (*q == '{') {
            depth++;
        } else if (*q == '}' && depth > 0) {
            depth--;
        } else if (*q == close && depth == 0) {
            return q;
        } else if (*q == '}') {
            return NULL; /* the group around closes first */
        }
    }
    return NULL;
}

/* An optional argument [..] or a beamer overlay <..> at p, skipped */
static gboolean skip_optional(Scan* s, gchar open, gchar close) {
    if (s->p >= s->end || *s->p != open) return FALSE;
    const gchar* q = matching(s, s->p, close);
    if (q == NULL) return FALSE;
    move_to(s, q + 1);
    return TRUE;
}

static void skip_star(Scan* s) {
    if (s->p < s->end && *s->p == '*') move_to(s, s->p + 1);
}

/* A group {..} at p (after spaces, over new lines when lines is TRUE):
 * its content [*start, *stop), *place where it starts; p after it */
static gboolean read_group(Scan* s, gboolean lines, Place* place, const gchar** stop) {
    skip_spaces(s, lines);
    if (s->p >= s->end || *s->p != '{') return FALSE;
    const gchar* close = matching(s, s->p, '}');
    if (close == NULL) return FALSE;
    move_to(s, s->p + 1);
    *place = here(s);
    *stop = close;
    move_to(s, close + 1);
    return TRUE;
}

/* A command name, "\name" or "{\name}" at p: the name without the
 * backslash */
static gboolean read_csname(Scan* s, Place* place, const gchar** stop) {
    skip_spaces(s, TRUE);
    gboolean braced = s->p < s->end && *s->p == '{';
    if (braced) {
        move_to(s, s->p + 1);
        skip_spaces(s, TRUE);
    }
    if (s->p + 1 >= s->end || *s->p != '\\') return FALSE;
    const gchar* q = s->p + 1;
    const gchar* name = q;
    while (q < s->end && is_letter(*q)) q++;
    if (q == name) q++; /* a one-character name: \, */
    if (q > s->end) return FALSE;
    move_to(s, name);
    *place = here(s);
    *stop = q;
    move_to(s, q);
    if (braced) {
        skip_spaces(s, TRUE);
        if (s->p < s->end && *s->p == '}') move_to(s, s->p + 1);
    }
    return TRUE;
}

/* [start, stop) trimmed; FALSE when nothing is left or it holds a macro
 * parameter (#1: a heading or label inside a definition) */
static gboolean trimmed(const gchar** start, const gchar** stop) {
    while (*start < *stop && g_ascii_isspace(**start)) (*start)++;
    while (*stop > *start && g_ascii_isspace((*stop)[-1])) (*stop)--;
    return *stop > *start && memchr(*start, '#', (gsize)(*stop - *start)) == NULL;
}

static void add_symbol(Scan* s, const Place* place, const gchar* stop, CodeSymbolKind kind) {
    const gchar* start = place->at;
    if (!trimmed(&start, &stop)) return;
    gsize n = (gsize)(stop - start);
    if (memchr(start, '\n', n) != NULL) return; /* a key is on one line */
    if (s->only != NULL && (n != s->only_len || memcmp(start, s->only, n) != 0)) return;
    const gchar* le = memchr(place->line_start, '\n', (gsize)(s->end - place->line_start));
    if (le == NULL) le = s->end;
    gint column = (gint)(start - place->line_start);
    if (s->symbols != NULL) {
        g_ptr_array_add(s->symbols, code_symbol_new(start, n, kind, FALSE, place->line, column, place->line_start,
                                                    (gsize)(le - place->line_start)));
    }
    if (s->outline != NULL) {
        gchar* name = g_utf8_make_valid(start, (gssize)n);
        g_ptr_array_add(s->outline, code_outline_entry_new(name, NULL, NULL, kind, place->line, column));
        g_free(name);
    }
}

static void add_heading(Scan* s, const Place* place, const gchar* stop, gint level) {
    const gchar* start = place->at;
    if (s->outline == NULL || !trimmed(&start, &stop)) return;
    gchar* title = g_strndup(start, (gsize)(stop - start));
    CodeOutlineEntry* e = code_outline_heading_new(title, level, place->line);
    g_free(title);
    if (e == NULL) return;
    e->column = (gint)(start - place->line_start);
    g_ptr_array_add(s->outline, e);
}

static gboolean name_is(const gchar* name, gsize n, const gchar* const* names) {
    for (gsize i = 0; names[i] != NULL; i++) {
        if (strlen(names[i]) == n && memcmp(names[i], name, n) == 0) return TRUE;
    }
    return FALSE;
}

static gint section_level(const gchar* name, gsize n) {
    static const struct {
        const gchar* name;
        gint level;
    } SECTIONS[] = {
        {"part", 1},          {"chapter", 2},       {"addchap", 2}, {"section", 3},
        {"addsec", 3},        {"subsection", 4},    {"subsubsection", 5},
        {"paragraph", 6},     {"subparagraph", 7},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(SECTIONS); i++) {
        if (strlen(SECTIONS[i].name) == n && memcmp(SECTIONS[i].name, name, n) == 0) return SECTIONS[i].level;
    }
    return 0;
}

static const gchar* const MACRO_DEFS[] = {
    "newcommand", "renewcommand", "providecommand", "DeclareRobustCommand", "NewDocumentCommand",
    "RenewDocumentCommand", "ProvideDocumentCommand", "DeclareDocumentCommand", "DeclareMathOperator",
    "newrobustcmd", "def", "gdef", "edef", "xdef", "let", NULL,
};
static const gchar* const ENV_DEFS[] = {
    "newenvironment", "renewenvironment", "NewDocumentEnvironment", "RenewDocumentEnvironment", "newtheorem", NULL,
};
static const gchar* const VERBATIM[] = {
    "verbatim", "verbatim*", "Verbatim", "Verbatim*", "BVerbatim", "lstlisting", "minted", "comment",
    "filecontents", "filecontents*", NULL,
};

/* \begin{name} of a verbatim environment: skip to after its \end{name} */
static void skip_verbatim(Scan* s, const gchar* name, gsize n) {
    gchar* end = g_strdup_printf("\\end{%.*s}", (gint)n, name);
    const gchar* q = g_strstr_len(s->p, s->end - s->p, end);
    move_to(s, q != NULL ? q + strlen(end) : s->end);
    g_free(end);
}

/* \verb|text|: skip the text (on its line) */
static void skip_verb(Scan* s) {
    skip_star(s);
    if (s->p >= s->end || g_ascii_isspace(*s->p) || is_letter(*s->p)) return;
    gchar delim = *s->p;
    const gchar* q = s->p + 1;
    while (q < s->end && *q != delim && *q != '\n') q++;
    move_to(s, q < s->end && *q == delim ? q + 1 : q);
}

static void frame_begun(Scan* s) {
    /* \begin{frame}<overlay>[options]{Title}{Subtitle}, the title on the line */
    for (;;) {
        skip_spaces(s, FALSE);
        if (!skip_optional(s, '<', '>') && !skip_optional(s, '[', ']')) break;
    }
    Place place;
    const gchar* stop;
    if (read_group(s, FALSE, &place, &stop)) {
        add_heading(s, &place, stop, FRAME_LEVEL);
        s->frame_untitled = FALSE;
    } else {
        s->frame_untitled = TRUE;
    }
}

/* The command name [start, stop) just read, p after it */
static void command(Scan* s, const gchar* name, gsize n) {
    Place place;
    const gchar* stop;
    gint level = section_level(name, n);
    if (level > 0) {
        skip_star(s);
        skip_spaces(s, TRUE);
        skip_optional(s, '[', ']');
        if (read_group(s, TRUE, &place, &stop)) add_heading(s, &place, stop, level);
    } else if (n == 5 && memcmp(name, "label", 5) == 0) {
        if (read_group(s, TRUE, &place, &stop)) add_symbol(s, &place, stop, CODE_SYM_LABEL);
    } else if (n == 7 && memcmp(name, "bibitem", 7) == 0) {
        skip_spaces(s, TRUE);
        skip_optional(s, '[', ']');
        if (read_group(s, TRUE, &place, &stop)) add_symbol(s, &place, stop, CODE_SYM_REFERENCE);
    } else if (name_is(name, n, MACRO_DEFS)) {
        skip_star(s);
        if (read_csname(s, &place, &stop)) add_symbol(s, &place, stop, CODE_SYM_MACRO);
    } else if (name_is(name, n, ENV_DEFS)) {
        skip_star(s);
        if (read_group(s, TRUE, &place, &stop)) add_symbol(s, &place, stop, CODE_SYM_TYPE);
    } else if (n == 5 && memcmp(name, "begin", 5) == 0) {
        if (!read_group(s, TRUE, &place, &stop)) return;
        gsize env_len = (gsize)(stop - place.at);
        if (name_is(place.at, env_len, VERBATIM)) {
            skip_verbatim(s, place.at, env_len);
        } else if (env_len == 5 && memcmp(place.at, "frame", 5) == 0) {
            frame_begun(s);
        }
    } else if (n == 3 && memcmp(name, "end", 3) == 0) {
        if (read_group(s, TRUE, &place, &stop) && stop - place.at == 5 && memcmp(place.at, "frame", 5) == 0) {
            s->frame_untitled = FALSE;
        }
    } else if (n == 10 && memcmp(name, "frametitle", 10) == 0) {
        skip_spaces(s, TRUE);
        skip_optional(s, '<', '>');
        skip_optional(s, '[', ']');
        if (read_group(s, TRUE, &place, &stop) && s->frame_untitled) {
            add_heading(s, &place, stop, FRAME_LEVEL);
            s->frame_untitled = FALSE;
        }
    } else if (n == 4 && memcmp(name, "verb", 4) == 0) {
        skip_verb(s);
    }
}

/* "@article{key," at p (first on its line) */
static void bib_entry(Scan* s) {
    const gchar* q = s->p + 1;
    const gchar* type = q;
    while (q < s->end && g_ascii_isalpha(*q)) q++;
    gsize n = (gsize)(q - type);
    move_to(s, q);
    if (n == 0 || (n == 6 && g_ascii_strncasecmp(type, "string", 6) == 0) ||
        (n == 7 && g_ascii_strncasecmp(type, "comment", 7) == 0) ||
        (n == 8 && g_ascii_strncasecmp(type, "preamble", 8) == 0)) {
        return;
    }
    skip_spaces(s, TRUE);
    if (s->p >= s->end || (*s->p != '{' && *s->p != '(')) return;
    move_to(s, s->p + 1);
    skip_spaces(s, TRUE);
    Place place = here(s);
    q = s->p;
    while (q < s->end && *q != ',' && *q != '}' && *q != ')' && !g_ascii_isspace(*q)) q++;
    if (q > s->p) add_symbol(s, &place, q, CODE_SYM_REFERENCE);
    move_to(s, q);
}

static void scan(Scan* s) {
    gboolean line_head = TRUE; /* only white space since the start of the line */
    while (s->p < s->end) {
        gchar c = *s->p;
        if (c == '%') {
            move_to(s, comment_end(s, s->p));
        } else if (c == '\n') {
            move_to(s, s->p + 1);
            line_head = TRUE;
            continue;
        } else if (c == '@' && line_head) {
            bib_entry(s);
        } else if (c == '\\') {
            const gchar* name = s->p + 1;
            const gchar* q = name;
            while (q < s->end && is_letter(*q)) q++;
            if (q == name) {
                move_to(s, q < s->end && *q != '\n' ? q + 1 : q); /* \% \{ \\ ... */
            } else {
                move_to(s, q);
                command(s, name, (gsize)(q - name));
            }
        } else {
            if (c != ' ' && c != '\t' && c != '\r') line_head = FALSE;
            move_to(s, s->p + 1);
            continue;
        }
        line_head = FALSE;
    }
}

static void scan_init(Scan* s, const gchar* text, gsize len) {
    memset(s, 0, sizeof(*s));
    s->text = text;
    s->end = text + len;
    s->p = text;
    s->line_start = text;
}

GPtrArray* code_scan_tex(const gchar* text, gsize len, const gchar* only_name) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_symbol_free);
    if (text == NULL) return out;
    Scan s;
    scan_init(&s, text, len);
    s.only = only_name;
    s.only_len = only_name != NULL ? strlen(only_name) : 0;
    s.symbols = out;
    scan(&s);
    return out;
}

GPtrArray* code_outline_tex(const gchar* text, gsize len) {
    GPtrArray* out = code_outline_entries_new();
    if (text == NULL) return out;
    Scan s;
    scan_init(&s, text, len);
    s.outline = out;
    scan(&s);
    return out;
}
