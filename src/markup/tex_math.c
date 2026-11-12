/* TeX math parsed into a tree of boxes (see tex_math.h). */
#include "markup/tex_math.h"
#include "markup/tex_symbols.h"

#include <stdarg.h>
#include <string.h>

#define MAX_DEPTH 64

/* Fonts of the letters (\mathbb...) */
typedef enum {
    VAR_NORMAL, /* letters italic */
    VAR_ROMAN,
    VAR_BOLD,
    VAR_ITALIC,
    VAR_BOLD_ITALIC,
    VAR_BB,
    VAR_CAL,
    VAR_FRAK,
    VAR_MONO,
} Variant;

typedef struct {
    const gchar* p;
    gint depth;
    gint brackets; /* inside a \sqrt[...] index: ']' ends the row */
    Variant variant;
} Parser;

/* --- Nodes --- */

static TexNode* node_new(TexKind kind) {
    TexNode* n = g_new0(TexNode, 1);
    n->kind = kind;
    n->cls = TEX_ORD;
    if (kind == TEX_ROW) n->items = g_ptr_array_new_with_free_func((GDestroyNotify)tex_node_free);
    return n;
}

static void free_row(gpointer row) {
    g_ptr_array_unref(row);
}

void tex_node_free(TexNode* node) {
    if (node == NULL) return;
    g_free(node->text);
    g_free(node->left);
    g_free(node->right);
    tex_node_free(node->body);
    tex_node_free(node->under);
    tex_node_free(node->over);
    if (node->items != NULL) g_ptr_array_unref(node->items);
    g_free(node);
}

static TexNode* atom_new(const gchar* text, TexClass cls, guint font) {
    TexNode* n = node_new(TEX_ATOM);
    n->text = g_strdup(text);
    n->cls = cls;
    n->font = font;
    return n;
}

static TexNode* space_new(gdouble em) {
    TexNode* n = node_new(TEX_SPACE);
    n->em = em;
    return n;
}

static TexNode* row_of(TexNode* first, ...) G_GNUC_NULL_TERMINATED;
static TexNode* row_of(TexNode* first, ...) {
    TexNode* row = node_new(TEX_ROW);
    va_list ap;
    va_start(ap, first);
    for (TexNode* n = first; n != NULL; n = va_arg(ap, TexNode*)) g_ptr_array_add(row->items, n);
    va_end(ap);
    return row;
}

TexClass tex_node_class(const TexNode* node) {
    if (node == NULL) return TEX_ORD;
    switch (node->kind) {
    case TEX_ATOM:
        return node->cls;
    case TEX_SCRIPTS:
        return tex_node_class(node->body);
    case TEX_FRAC:
    case TEX_FENCED:
        return TEX_INNER;
    case TEX_ACCENT:
        return (node->style == TEX_ACCENT_OVERBRACE || node->style == TEX_ACCENT_UNDERBRACE) ? TEX_OP : TEX_ORD;
    case TEX_ROW:
        return node->cls; /* ORD, or set by \mathrel{...} */
    default:
        return TEX_ORD;
    }
}

/* --- Characters --- */

/* The letter or digit c in the variant (ℝ for R in \mathbb), into out */
static void styled_char(gunichar c, Variant v, GString* out, guint* font) {
    *font = 0;
    gboolean upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z', digit = c >= '0' && c <= '9';
    gunichar mapped = 0;
    switch (v) {
    case VAR_NORMAL:
        if (upper || lower) *font = TEX_FONT_ITALIC;
        break;
    case VAR_ITALIC:
        *font = TEX_FONT_ITALIC;
        break;
    case VAR_BOLD:
        *font = TEX_FONT_BOLD;
        break;
    case VAR_BOLD_ITALIC:
        *font = TEX_FONT_BOLD | ((upper || lower) ? TEX_FONT_ITALIC : 0);
        break;
    case VAR_MONO:
        *font = TEX_FONT_MONO;
        break;
    case VAR_BB: {
        static const gchar BB_UPPER[] = "CHNPQRZ";
        static const gunichar BB_UPPER_CP[] = {0x2102, 0x210D, 0x2115, 0x2119, 0x211A, 0x211D, 0x2124};
        const gchar* hole = upper ? strchr(BB_UPPER, (gchar)c) : NULL;
        if (hole != NULL) mapped = BB_UPPER_CP[hole - BB_UPPER];
        else if (upper) mapped = 0x1D538 + (c - 'A');
        else if (lower) mapped = 0x1D552 + (c - 'a');
        else if (digit) mapped = 0x1D7D8 + (c - '0');
        break;
    }
    case VAR_CAL: {
        static const gchar CAL_HOLES[] = "BEFHILMRego";
        static const gunichar CAL_CP[] = {0x212C, 0x2130, 0x2131, 0x210B, 0x2110, 0x2112,
                                          0x2133, 0x211B, 0x212F, 0x210A, 0x2134};
        const gchar* hole = (upper || lower) ? strchr(CAL_HOLES, (gchar)c) : NULL;
        if (hole != NULL) mapped = CAL_CP[hole - CAL_HOLES];
        else if (upper) mapped = 0x1D49C + (c - 'A');
        else if (lower) mapped = 0x1D4B6 + (c - 'a');
        break;
    }
    case VAR_FRAK: {
        static const gchar FRAK_HOLES[] = "CHIRZ";
        static const gunichar FRAK_CP[] = {0x212D, 0x210C, 0x2111, 0x211C, 0x2128};
        const gchar* hole = upper ? strchr(FRAK_HOLES, (gchar)c) : NULL;
        if (hole != NULL) mapped = FRAK_CP[hole - FRAK_HOLES];
        else if (upper) mapped = 0x1D504 + (c - 'A');
        else if (lower) mapped = 0x1D51E + (c - 'a');
        break;
    }
    case VAR_ROMAN:
        break;
    }
    g_string_append_unichar(out, mapped != 0 ? mapped : c);
}

/* Class of a character typed as such */
static TexClass char_class(gunichar c) {
    switch (c) {
    case '+':
    case '-':
    case '*':
        return TEX_BIN;
    case '=':
    case '<':
    case '>':
    case ':':
        return TEX_REL;
    case '(':
    case '[':
        return TEX_OPEN;
    case ')':
    case ']':
    case '!':
    case '?':
        return TEX_CLOSE;
    case ',':
    case ';':
        return TEX_PUNCT;
    default:
        break;
    }
    /* A Unicode symbol typed directly: its class as a command */
    gchar buf[8] = {0};
    g_unichar_to_utf8(c, buf);
    const TexSymbol* sym = tex_symbol_find_text(buf);
    return sym != NULL ? sym->cls : TEX_ORD;
}

/* --- Parser --- */

static void skip_space(Parser* ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') ps->p++;
}

/* At \name (not followed by a letter) */
static gboolean at_command(Parser* ps, const gchar* name) {
    gsize n = strlen(name);
    return ps->p[0] == '\\' && strncmp(ps->p + 1, name, n) == 0 && !g_ascii_isalpha(ps->p[1 + n]);
}

/* The command at p (after its backslash): its letters, or one character;
 * spaces after a word are skipped */
static gchar* read_command(Parser* ps) {
    ps->p++; /* backslash */
    const gchar* start = ps->p;
    if (g_ascii_isalpha(*ps->p)) {
        while (g_ascii_isalpha(*ps->p)) ps->p++;
        gchar* name = g_strndup(start, ps->p - start);
        if (*ps->p == '*' && (strcmp(name, "operatorname") == 0 || strcmp(name, "tag") == 0)) {
            ps->p++;
            gchar* starred = g_strconcat(name, "*", NULL);
            g_free(name);
            name = starred;
        }
        skip_space(ps);
        return name;
    }
    if (*ps->p == '\0') return g_strdup("");
    const gchar* next = g_utf8_next_char(ps->p);
    gchar* name = g_strndup(start, next - start);
    ps->p = next;
    return name;
}

/* The text of a {...} group as written (nested braces kept), or of the
 * next character; NULL at the end */
static gchar* read_raw_group(Parser* ps) {
    skip_space(ps);
    if (*ps->p == '\0') return NULL;
    if (*ps->p != '{') {
        const gchar* start = ps->p;
        ps->p = g_utf8_next_char(ps->p);
        return g_strndup(start, ps->p - start);
    }
    const gchar* start = ++ps->p;
    gint depth = 1;
    while (*ps->p != '\0') {
        if (*ps->p == '\\' && ps->p[1] != '\0') {
            ps->p += 2;
            continue;
        }
        if (*ps->p == '{') depth++;
        if (*ps->p == '}' && --depth == 0) break;
        ps->p++;
    }
    gchar* text = g_strndup(start, ps->p - start);
    if (*ps->p == '}') ps->p++;
    return text;
}

/* Text of \text{...}: escapes resolved, $ signs dropped, spaces kept */
static gchar* plain_text(const gchar* raw) {
    GString* out = g_string_new(NULL);
    for (const gchar* c = raw; *c != '\0'; c++) {
        if (*c == '\\' && c[1] != '\0' && strchr("{}$%&#_ ", c[1]) != NULL) {
            g_string_append_c(out, c[1]);
            c++;
        } else if (*c == '\\' && c[1] == '\\') {
            g_string_append_c(out, ' ');
            c++;
        } else if (*c == '~') {
            g_string_append(out, "\xC2\xA0");
        } else if (*c != '$' && *c != '{' && *c != '}' && *c != '\n') {
            g_string_append_c(out, *c);
        } else if (*c == '\n') {
            g_string_append_c(out, ' ');
        }
    }
    return g_string_free(out, FALSE);
}

/* A dimension ("1em", "3mu", "-2pt") in em */
static gdouble dimension_em(const gchar* text) {
    gchar* end = NULL;
    gdouble v = g_ascii_strtod(text, &end);
    if (end == text) return 0.5;
    while (*end == ' ') end++;
    if (g_str_has_prefix(end, "mu")) return v / 18.0;
    if (g_str_has_prefix(end, "pt")) return v / 10.0;
    if (g_str_has_prefix(end, "ex")) return v * 0.43;
    if (g_str_has_prefix(end, "mm")) return v / 3.5;
    if (g_str_has_prefix(end, "cm")) return v / 0.35;
    if (g_str_has_prefix(end, "in")) return v * 7.2;
    return v; /* em */
}

/* The dimension argument of \hspace, \kern: {1em} or 1em */
static gdouble read_dimension(Parser* ps) {
    skip_space(ps);
    if (*ps->p == '{') {
        gchar* raw = read_raw_group(ps);
        gdouble em = dimension_em(raw);
        g_free(raw);
        return em;
    }
    const gchar* start = ps->p;
    while (*ps->p == '-' || *ps->p == '.' || g_ascii_isdigit(*ps->p) || *ps->p == ' ') ps->p++;
    while (g_ascii_isalpha(*ps->p) && ps->p - start < 16) ps->p++;
    gchar* text = g_strndup(start, ps->p - start);
    gdouble em = dimension_em(text);
    g_free(text);
    return em;
}

static TexNode* parse_row(Parser* ps);
static TexNode* parse_item(Parser* ps);
static GPtrArray* parse_rows(Parser* ps, gboolean top);

/* The argument of a command: a group, or one token */
static TexNode* parse_arg(Parser* ps) {
    skip_space(ps);
    if (*ps->p == '{') {
        ps->p++;
        TexNode* row = parse_row(ps);
        if (*ps->p == '}') ps->p++;
        return row;
    }
    if (*ps->p == '\0' || *ps->p == '}' || *ps->p == '&') return node_new(TEX_ROW);
    if (g_ascii_isdigit(*ps->p)) {
        gchar digit[2] = {*ps->p++, '\0'};
        GString* s = g_string_new(NULL);
        guint font;
        styled_char((gunichar)digit[0], ps->variant, s, &font);
        TexNode* n = atom_new(s->str, TEX_ORD, font);
        g_string_free(s, TRUE);
        return n;
    }
    TexNode* n = parse_item(ps);
    return n != NULL ? n : node_new(TEX_ROW);
}

/* The argument parsed in a variant (\mathbb{R}) */
static TexNode* parse_arg_in(Parser* ps, Variant v) {
    Variant saved = ps->variant;
    ps->variant = v;
    TexNode* n = parse_arg(ps);
    ps->variant = saved;
    return n;
}

/* A delimiter after \left, \right, \big: its text ("" for '.') */
static gchar* read_delim(Parser* ps) {
    skip_space(ps);
    if (*ps->p == '\0') return g_strdup("");
    if (*ps->p == '\\') {
        gchar* name = read_command(ps);
        const gchar* text = NULL;
        if (strcmp(name, "{") == 0) text = "{";
        else if (strcmp(name, "}") == 0) text = "}";
        else if (strcmp(name, "|") == 0) text = "‖";
        const TexSymbol* sym = text == NULL ? tex_symbol_find(name) : NULL;
        if (sym != NULL) text = sym->text;
        gchar* result = g_strdup(text != NULL ? text : "");
        g_free(name);
        return result;
    }
    const gchar* start = ps->p;
    ps->p = g_utf8_next_char(ps->p);
    if (*start == '.') return g_strdup("");
    if (*start == '<') return g_strdup("⟨");
    if (*start == '>') return g_strdup("⟩");
    return g_strndup(start, ps->p - start);
}

/* Column alignments of an environment */
static gchar* env_alignments(const gchar* env, Parser* ps, TexTableStyle* style) {
    *style = TEX_TABLE_MATRIX;
    if (strcmp(env, "array") == 0 || strcmp(env, "alignat") == 0 || strcmp(env, "alignat*") == 0 ||
        strcmp(env, "alignedat") == 0) {
        gchar* spec = read_raw_group(ps);
        GString* cols = g_string_new(NULL);
        for (const gchar* c = spec != NULL ? spec : ""; *c != '\0'; c++) {
            if (*c == 'l' || *c == 'c' || *c == 'r') g_string_append_c(cols, *c);
        }
        g_free(spec);
        if (env[0] != 'a' || env[1] != 'r') {
            *style = TEX_TABLE_ALIGNED;
            g_string_assign(cols, "rl");
        }
        if (cols->len == 0) g_string_assign(cols, "c");
        return g_string_free(cols, FALSE);
    }
    if (g_str_has_prefix(env, "align") || g_str_has_prefix(env, "split") || g_str_has_prefix(env, "eqnarray")) {
        *style = TEX_TABLE_ALIGNED;
        return g_strdup(g_str_has_prefix(env, "eqnarray") ? "rcl" : "rl");
    }
    if (g_str_has_prefix(env, "gather")) {
        *style = TEX_TABLE_ALIGNED;
        return g_strdup("c");
    }
    if (strstr(env, "cases") != NULL) {
        *style = TEX_TABLE_CASES;
        return g_strdup("l");
    }
    return g_strdup("c");
}

/* \begin{env} ... \end{env}, \begin read */
static TexNode* parse_environment(Parser* ps) {
    gchar* env = read_raw_group(ps);
    if (env == NULL) return NULL;
    g_strstrip(env);
    TexTableStyle style;
    gchar* aligns = env_alignments(env, ps, &style);
    GPtrArray* rows = parse_rows(ps, FALSE);
    if (at_command(ps, "end")) {
        g_free(read_command(ps));
        g_free(read_raw_group(ps));
    }
    TexNode* result;
    gboolean plain = g_str_has_prefix(env, "equation") || strcmp(env, "displaymath") == 0 ||
                     strcmp(env, "math") == 0;
    if (plain && rows->len == 1 && ((GPtrArray*)g_ptr_array_index(rows, 0))->len == 1) {
        GPtrArray* row = g_ptr_array_index(rows, 0);
        result = g_ptr_array_steal_index(row, 0);
        g_ptr_array_unref(rows);
        g_free(aligns);
    } else {
        result = node_new(TEX_TABLE);
        result->items = rows;
        result->text = aligns;
        result->style = style;
        result->em = style == TEX_TABLE_ALIGNED ? 2.0 : 1.0;
    }
    const gchar* left = NULL;
    const gchar* right = "";
    if (strcmp(env, "pmatrix") == 0) left = "(", right = ")";
    else if (strcmp(env, "bmatrix") == 0) left = "[", right = "]";
    else if (strcmp(env, "Bmatrix") == 0) left = "{", right = "}";
    else if (strcmp(env, "vmatrix") == 0) left = "|", right = "|";
    else if (strcmp(env, "Vmatrix") == 0) left = "‖", right = "‖";
    else if (strcmp(env, "cases") == 0 || strcmp(env, "dcases") == 0) left = "{";
    else if (strcmp(env, "rcases") == 0) left = "", right = "}";
    if (left != NULL) {
        TexNode* fenced = node_new(TEX_FENCED);
        fenced->left = g_strdup(left);
        fenced->right = g_strdup(right);
        fenced->body = result;
        result = fenced;
    }
    g_free(env);
    return result;
}

/* The command whose name was read */
static TexNode* parse_command(Parser* ps, const gchar* name) {
    /* Spaces and escaped characters */
    static const struct {
        const gchar* name;
        gdouble em;
    } SPACES[] = {
        {",", 3.0 / 18}, {":", 4.0 / 18}, {">", 4.0 / 18}, {";", 5.0 / 18}, {"!", -3.0 / 18},
        {" ", 1.0 / 3}, {"quad", 1.0}, {"qquad", 2.0}, {"enspace", 0.5}, {"thinspace", 3.0 / 18},
        {"medspace", 4.0 / 18}, {"thickspace", 5.0 / 18}, {"negthinspace", -3.0 / 18},
        {"negmedspace", -4.0 / 18}, {"negthickspace", -5.0 / 18}, {"space", 1.0 / 3}, {"nobreakspace", 1.0 / 3},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(SPACES); i++) {
        if (strcmp(name, SPACES[i].name) == 0) return space_new(SPACES[i].em);
    }
    if (strcmp(name, "{") == 0) return atom_new("{", TEX_OPEN, 0);
    if (strcmp(name, "}") == 0) return atom_new("}", TEX_CLOSE, 0);
    if (strcmp(name, "|") == 0) return atom_new("‖", TEX_ORD, 0);
    if (name[0] != '\0' && name[1] == '\0' && strchr("#$%&_", name[0]) != NULL) return atom_new(name, TEX_ORD, 0);

    const TexSymbol* sym = tex_symbol_find(name);
    if (sym != NULL) {
        guint font = 0;
        if (sym->italic && ps->variant != VAR_ROMAN && ps->variant != VAR_BOLD) font |= TEX_FONT_ITALIC;
        if (ps->variant == VAR_BOLD || ps->variant == VAR_BOLD_ITALIC) font |= TEX_FONT_BOLD;
        return atom_new(sym->text, sym->cls, font);
    }
    const gchar* text = NULL;
    TexLimits limits;
    if (tex_symbol_big_op(name, &text, &limits)) {
        TexNode* n = atom_new(text, TEX_OP, 0);
        n->big = 1;
        n->limits = limits;
        return n;
    }
    if (tex_symbol_function(name, &text, &limits)) {
        TexNode* n = atom_new(text, TEX_OP, 0);
        n->limits = limits;
        return n;
    }
    TexAccent accent;
    if (tex_symbol_accent(name, &accent, &text)) {
        TexNode* n = node_new(TEX_ACCENT);
        n->style = accent;
        n->text = g_strdup(text);
        n->body = parse_arg(ps);
        if (accent == TEX_ACCENT_OVERBRACE || accent == TEX_ACCENT_UNDERBRACE) n->limits = TEX_LIMITS_ALWAYS;
        return n;
    }

    /* Fractions and roots */
    if (strcmp(name, "frac") == 0 || strcmp(name, "dfrac") == 0 || strcmp(name, "tfrac") == 0 ||
        strcmp(name, "cfrac") == 0) {
        TexNode* n = node_new(TEX_FRAC);
        n->style = name[0] == 'd' || name[0] == 'c' ? TEX_FRAC_DISPLAY : name[0] == 't' ? TEX_FRAC_TEXT : TEX_FRAC_AUTO;
        n->body = parse_arg(ps);
        n->under = parse_arg(ps);
        return n;
    }
    if (strcmp(name, "binom") == 0 || strcmp(name, "dbinom") == 0 || strcmp(name, "tbinom") == 0) {
        TexNode* frac = node_new(TEX_FRAC);
        frac->style = TEX_FRAC_NO_BAR;
        frac->body = parse_arg(ps);
        frac->under = parse_arg(ps);
        TexNode* n = node_new(TEX_FENCED);
        n->left = g_strdup("(");
        n->right = g_strdup(")");
        n->body = frac;
        return n;
    }
    if (strcmp(name, "sqrt") == 0) {
        TexNode* n = node_new(TEX_SQRT);
        skip_space(ps);
        if (*ps->p == '[') {
            ps->p++;
            ps->brackets++;
            n->under = parse_row(ps);
            ps->brackets--;
            if (*ps->p == ']') ps->p++;
        }
        n->body = parse_arg(ps);
        return n;
    }

    /* Delimiters */
    if (strcmp(name, "left") == 0) {
        TexNode* n = node_new(TEX_FENCED);
        n->left = read_delim(ps);
        n->body = parse_row(ps);
        if (at_command(ps, "right")) {
            g_free(read_command(ps));
            n->right = read_delim(ps);
        } else {
            n->right = g_strdup("");
        }
        return n;
    }
    if (strcmp(name, "right") == 0) { /* without \left */
        g_free(read_delim(ps));
        return NULL;
    }
    if (strcmp(name, "middle") == 0) {
        gchar* d = read_delim(ps);
        TexNode* n = atom_new(d, TEX_REL, 0);
        n->delim = TRUE;
        g_free(d);
        return n;
    }
    if (g_str_has_prefix(name, "big") || g_str_has_prefix(name, "Big")) {
        const gchar* rest = name + 3;
        gint size = name[0] == 'b' ? 1 : 2;
        if (rest[0] == 'g') {
            size += 2;
            rest++;
        }
        if (rest[0] == '\0' || ((rest[0] == 'l' || rest[0] == 'r' || rest[0] == 'm') && rest[1] == '\0')) {
            gchar* d = read_delim(ps);
            TexClass cls = rest[0] == 'l' ? TEX_OPEN : rest[0] == 'r' ? TEX_CLOSE : rest[0] == 'm' ? TEX_REL : TEX_ORD;
            TexNode* n = atom_new(d, cls, 0);
            n->delim = TRUE;
            n->big = size;
            g_free(d);
            return n;
        }
    }

    /* Fonts and text */
    static const struct {
        const gchar* name;
        Variant variant;
    } VARIANTS[] = {
        {"mathrm", VAR_ROMAN},  {"mathup", VAR_ROMAN},   {"mathsf", VAR_ROMAN},    {"mathbf", VAR_BOLD},
        {"mathit", VAR_ITALIC}, {"mathnormal", VAR_NORMAL}, {"boldsymbol", VAR_BOLD_ITALIC},
        {"bm", VAR_BOLD_ITALIC}, {"mathbb", VAR_BB},     {"mathcal", VAR_CAL},     {"mathscr", VAR_CAL},
        {"mathfrak", VAR_FRAK}, {"mathtt", VAR_MONO},    {"Bbb", VAR_BB},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(VARIANTS); i++) {
        if (strcmp(name, VARIANTS[i].name) == 0) return parse_arg_in(ps, VARIANTS[i].variant);
    }
    static const struct {
        const gchar* name;
        guint font;
    } TEXTS[] = {
        {"text", 0},      {"textrm", 0},  {"textnormal", 0}, {"textup", 0}, {"textsf", 0}, {"mbox", 0},
        {"hbox", 0},      {"textbf", TEX_FONT_BOLD}, {"textit", TEX_FONT_ITALIC}, {"emph", TEX_FONT_ITALIC},
        {"texttt", TEX_FONT_MONO},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(TEXTS); i++) {
        if (strcmp(name, TEXTS[i].name) != 0) continue;
        gchar* raw = read_raw_group(ps);
        gchar* text = plain_text(raw != NULL ? raw : "");
        TexNode* n = atom_new(text, TEX_ORD, TEXTS[i].font);
        g_free(text);
        g_free(raw);
        return n;
    }
    if (strcmp(name, "operatorname") == 0 || strcmp(name, "operatorname*") == 0) {
        gchar* raw = read_raw_group(ps);
        gchar* text = plain_text(raw != NULL ? raw : "");
        TexNode* n = atom_new(text, TEX_OP, 0);
        n->limits = name[12] == '*' ? TEX_LIMITS_DISPLAY : TEX_LIMITS_SIDE;
        g_free(text);
        g_free(raw);
        return n;
    }

    /* Classes */
    static const struct {
        const gchar* name;
        TexClass cls;
    } CLASSES[] = {
        {"mathop", TEX_OP},    {"mathrel", TEX_REL},     {"mathbin", TEX_BIN},     {"mathord", TEX_ORD},
        {"mathopen", TEX_OPEN}, {"mathclose", TEX_CLOSE}, {"mathpunct", TEX_PUNCT}, {"mathinner", TEX_INNER},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(CLASSES); i++) {
        if (strcmp(name, CLASSES[i].name) != 0) continue;
        TexNode* n = parse_arg(ps);
        if (n->kind == TEX_ATOM) {
            n->cls = CLASSES[i].cls;
            if (n->cls == TEX_OP) n->limits = TEX_LIMITS_DISPLAY;
        } else if (n->kind == TEX_ROW) {
            n->cls = CLASSES[i].cls;
            if (n->items->len == 1 && ((TexNode*)g_ptr_array_index(n->items, 0))->kind == TEX_ATOM) {
                TexNode* only = g_ptr_array_steal_index(n->items, 0);
                tex_node_free(n);
                only->cls = CLASSES[i].cls;
                if (only->cls == TEX_OP) only->limits = TEX_LIMITS_DISPLAY;
                return only;
            }
        }
        return n;
    }

    /* Stacking */
    if (strcmp(name, "overset") == 0 || strcmp(name, "stackrel") == 0 || strcmp(name, "underset") == 0) {
        TexNode* n = node_new(TEX_SCRIPTS);
        TexNode* script = parse_arg(ps);
        n->body = parse_arg(ps);
        if (name[0] == 'u') n->under = script;
        else n->over = script;
        n->limits = TEX_LIMITS_ALWAYS;
        return n;
    }
    if (strcmp(name, "xrightarrow") == 0 || strcmp(name, "xleftarrow") == 0) {
        TexNode* n = node_new(TEX_SCRIPTS);
        skip_space(ps);
        if (*ps->p == '[') {
            ps->p++;
            ps->brackets++;
            n->under = parse_row(ps);
            ps->brackets--;
            if (*ps->p == ']') ps->p++;
        }
        n->over = parse_arg(ps);
        n->body = atom_new(name[1] == 'r' ? "⟶" : "⟵", TEX_REL, 0);
        n->limits = TEX_LIMITS_ALWAYS;
        return n;
    }
    if (strcmp(name, "substack") == 0) {
        skip_space(ps);
        if (*ps->p != '{') return parse_arg(ps);
        ps->p++;
        TexNode* n = node_new(TEX_TABLE);
        n->items = parse_rows(ps, FALSE);
        if (*ps->p == '}') ps->p++;
        n->text = g_strdup("c");
        n->style = TEX_TABLE_MATRIX;
        n->em = 0.0;
        return n;
    }
    if (strcmp(name, "begin") == 0) return parse_environment(ps);
    if (strcmp(name, "end") == 0) {
        g_free(read_raw_group(ps));
        return NULL;
    }

    /* Modulo */
    if (strcmp(name, "bmod") == 0) return atom_new("mod", TEX_BIN, 0);
    if (strcmp(name, "pmod") == 0 || strcmp(name, "pod") == 0) {
        TexNode* arg = parse_arg(ps);
        TexNode* row = row_of(space_new(1.0), atom_new("(", TEX_OPEN, 0), NULL);
        if (name[1] == 'm') {
            g_ptr_array_add(row->items, atom_new("mod", TEX_ORD, 0));
            g_ptr_array_add(row->items, space_new(1.0 / 3));
        }
        g_ptr_array_add(row->items, arg);
        g_ptr_array_add(row->items, atom_new(")", TEX_CLOSE, 0));
        return row;
    }
    if (strcmp(name, "mod") == 0) return row_of(space_new(1.0), atom_new("mod", TEX_ORD, 0), space_new(1.0 / 3), NULL);

    /* Boxes */
    if (strcmp(name, "boxed") == 0 || strcmp(name, "fbox") == 0) {
        TexNode* n = node_new(TEX_BOX);
        n->style = TEX_BOX_FRAME;
        if (name[0] == 'f') {
            gchar* raw = read_raw_group(ps);
            gchar* text = plain_text(raw != NULL ? raw : "");
            n->body = atom_new(text, TEX_ORD, 0);
            g_free(text);
            g_free(raw);
        } else {
            n->body = parse_arg(ps);
        }
        return n;
    }
    if (strcmp(name, "phantom") == 0 || strcmp(name, "hphantom") == 0 || strcmp(name, "vphantom") == 0) {
        TexNode* n = node_new(TEX_BOX);
        n->style = TEX_BOX_PHANTOM;
        n->body = parse_arg(ps);
        return n;
    }

    /* \not before a relation */
    if (strcmp(name, "not") == 0) {
        TexNode* n = parse_item(ps);
        if (n == NULL || n->kind != TEX_ATOM) return n != NULL ? n : atom_new("/", TEX_REL, 0);
        const gchar* negated = tex_symbol_negated(n->text);
        if (negated != NULL) {
            g_free(n->text);
            n->text = g_strdup(negated);
            return n;
        }
        gchar* combined = g_strconcat(n->text, "\xCC\xB8", NULL); /* U+0338 */
        g_free(n->text);
        n->text = combined;
        return n;
    }

    /* Equation numbers */
    if (strcmp(name, "tag") == 0 || strcmp(name, "tag*") == 0) {
        gchar* raw = read_raw_group(ps);
        gchar* text = plain_text(raw != NULL ? raw : "");
        gchar* shown = name[3] == '*' ? g_strdup(text) : g_strdup_printf("(%s)", text);
        TexNode* row = row_of(space_new(2.0), atom_new(shown, TEX_ORD, 0), NULL);
        g_free(shown);
        g_free(text);
        g_free(raw);
        return row;
    }

    /* Spacing with a length */
    if (strcmp(name, "hspace") == 0 || strcmp(name, "kern") == 0 || strcmp(name, "mkern") == 0 ||
        strcmp(name, "mspace") == 0 || strcmp(name, "hskip") == 0 || strcmp(name, "mskip") == 0) {
        if (*ps->p == '*') ps->p++;
        return space_new(read_dimension(ps));
    }

    /* Ignored, with their argument */
    static const gchar* const SKIP_ARG[] = {"label", "color", "cline", "eqref", "ref", "hyperref", "href"};
    for (gsize i = 0; i < G_N_ELEMENTS(SKIP_ARG); i++) {
        if (strcmp(name, SKIP_ARG[i]) == 0) {
            g_free(read_raw_group(ps));
            return NULL;
        }
    }
    if (strcmp(name, "textcolor") == 0 || strcmp(name, "colorbox") == 0) {
        g_free(read_raw_group(ps));
        return parse_arg(ps);
    }
    /* Ignored */
    static const gchar* const IGNORED[] = {
        "displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle", "nonumber", "notag", "hline",
        "hdashline", "limits", "nolimits", "mathstrut", "strut", "allowbreak", "nobreak", "relax",
        "normalsize", "small", "large", "Large",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(IGNORED); i++) {
        if (strcmp(name, IGNORED[i]) == 0) return NULL;
    }

    /* Unknown: its name, upright */
    gchar* shown = g_strconcat("\\", name, NULL);
    TexNode* n = atom_new(shown, TEX_ORD, 0);
    g_free(shown);
    return n;
}

/* One item, without its scripts; NULL when it shows nothing (\label) */
static TexNode* parse_item(Parser* ps) {
    skip_space(ps);
    const gchar* p = ps->p;
    if (*p == '\0' || *p == '}' || *p == '&') return NULL;
    if (ps->depth >= MAX_DEPTH) {
        /* Too deep: the rest as text */
        TexNode* n = atom_new(p, TEX_ORD, 0);
        ps->p += strlen(p);
        return n;
    }
    ps->depth++;
    TexNode* result = NULL;
    if (*p == '{') {
        ps->p++;
        result = parse_row(ps);
        if (*ps->p == '}') ps->p++;
    } else if (*p == '\\') {
        gchar* name = read_command(ps);
        result = parse_command(ps, name);
        g_free(name);
    } else if (g_ascii_isdigit(*p) || (*p == '.' && g_ascii_isdigit(p[1]))) {
        GString* s = g_string_new(NULL);
        guint font = 0;
        while (g_ascii_isdigit(*ps->p) || (*ps->p == '.' && g_ascii_isdigit(ps->p[1]))) {
            styled_char((gunichar)*ps->p, ps->variant, s, &font);
            ps->p++;
        }
        result = atom_new(s->str, TEX_ORD, font);
        g_string_free(s, TRUE);
    } else if (g_ascii_isalpha(*p)) {
        GString* s = g_string_new(NULL);
        guint font = 0;
        styled_char((gunichar)*p, ps->variant, s, &font);
        ps->p++;
        result = atom_new(s->str, TEX_ORD, font);
        g_string_free(s, TRUE);
    } else if (*p == '~') {
        ps->p++;
        result = space_new(1.0 / 3);
    } else if (*p == '\'') {
        ps->p++;
        result = atom_new("′", TEX_ORD, 0);
    } else {
        gunichar c = g_utf8_get_char_validated(p, -1);
        const gchar* next = (c == (gunichar)-1 || c == (gunichar)-2) ? p + 1 : g_utf8_next_char(p);
        ps->p = next;
        if (c == (gunichar)-1 || c == (gunichar)-2) {
            result = atom_new("\xEF\xBF\xBD", TEX_ORD, 0);
        } else {
            gchar* text = g_strndup(p, next - p);
            TexClass cls = char_class(c);
            const gchar* shown = text;
            if (c == '-') shown = "−";
            else if (c == '*') shown = "∗";
            result = atom_new(shown, cls, 0);
            g_free(text);
        }
    }
    ps->depth--;
    return result;
}

/* An item and its scripts (x_i^2, f'', \sum\limits_0) */
static TexNode* parse_scripted(Parser* ps) {
    skip_space(ps);
    TexNode* base = NULL;
    if (*ps->p != '^' && *ps->p != '_') {
        base = parse_item(ps);
        if (base == NULL) return NULL;
    }
    TexNode* over = NULL;
    TexNode* under = NULL;
    gint limits = -1;
    while (TRUE) {
        skip_space(ps);
        if (*ps->p == '\'') {
            /* Primes join the base: f′ */
            GString* primes = g_string_new(NULL);
            while (*ps->p == '\'') {
                g_string_append(primes, "′");
                ps->p++;
            }
            TexNode* prime = atom_new(primes->str, TEX_ORD, 0);
            g_string_free(primes, TRUE);
            if (base == NULL) base = node_new(TEX_ROW);
            if (base->kind != TEX_ROW || base->cls != TEX_ORD) base = row_of(base, NULL);
            g_ptr_array_add(base->items, prime);
        } else if (*ps->p == '^' && over == NULL) {
            ps->p++;
            over = parse_arg(ps);
        } else if (*ps->p == '_' && under == NULL) {
            ps->p++;
            under = parse_arg(ps);
        } else if (at_command(ps, "limits") || at_command(ps, "nolimits") || at_command(ps, "displaylimits")) {
            gchar* name = read_command(ps);
            limits = strcmp(name, "limits") == 0     ? TEX_LIMITS_ALWAYS
                     : strcmp(name, "nolimits") == 0 ? TEX_LIMITS_SIDE
                                                     : TEX_LIMITS_DISPLAY;
            g_free(name);
        } else {
            break;
        }
    }
    if (base == NULL) base = node_new(TEX_ROW);
    if (limits >= 0 && base->kind == TEX_ATOM) base->limits = (TexLimits)limits;
    if (over == NULL && under == NULL) return base;
    if (base->kind == TEX_SCRIPTS && base->limits == TEX_LIMITS_ALWAYS &&
        ((over != NULL && base->over == NULL) || (under != NULL && base->under == NULL))) {
        /* \underbrace's label after \overset...: fill the free place */
        if (base->over == NULL) base->over = over, over = NULL;
        if (base->under == NULL) base->under = under, under = NULL;
        tex_node_free(over);
        tex_node_free(under);
        return base;
    }
    TexNode* n = node_new(TEX_SCRIPTS);
    n->body = base;
    n->over = over;
    n->under = under;
    if (limits >= 0) {
        n->limits = (TexLimits)limits;
    } else if (base->kind == TEX_ATOM && base->cls == TEX_OP) {
        n->limits = base->limits;
    } else if (base->kind == TEX_ACCENT) {
        n->limits = base->limits;
    } else {
        n->limits = TEX_LIMITS_SIDE;
    }
    return n;
}

/* At a token that ends a row (not consumed) */
static gboolean at_row_end(Parser* ps) {
    const gchar* p = ps->p;
    return *p == '\0' || *p == '}' || *p == '&' || (p[0] == '\\' && p[1] == '\\') || at_command(ps, "right") ||
           at_command(ps, "end") || at_command(ps, "cr") || (*p == ']' && ps->brackets > 0);
}

static TexNode* parse_row(Parser* ps) {
    TexNode* row = node_new(TEX_ROW);
    while (TRUE) {
        skip_space(ps);
        if (at_row_end(ps)) break;
        const gchar* before = ps->p;
        TexNode* n = parse_scripted(ps);
        if (n != NULL) g_ptr_array_add(row->items, n);
        if (ps->p == before) ps->p++; /* never stuck */
    }
    return row;
}

/* Rows of cells separated by & and \\ up to \end (or '}', or the end);
 * at the top, stray '}', \right and \end are skipped */
static GPtrArray* parse_rows(Parser* ps, gboolean top) {
    GPtrArray* rows = g_ptr_array_new_with_free_func(free_row);
    GPtrArray* row = g_ptr_array_new_with_free_func((GDestroyNotify)tex_node_free);
    g_ptr_array_add(rows, row);
    TexNode* cell = node_new(TEX_ROW);
    while (TRUE) {
        TexNode* part = parse_row(ps);
        /* Continue the cell after a skipped token */
        for (guint i = 0; i < part->items->len; i++) g_ptr_array_add(cell->items, part->items->pdata[i]);
        g_ptr_array_set_free_func(part->items, NULL);
        tex_node_free(part);
        if (*ps->p == '&') {
            ps->p++;
            g_ptr_array_add(row, cell);
            cell = node_new(TEX_ROW);
        } else if ((ps->p[0] == '\\' && ps->p[1] == '\\') || at_command(ps, "cr")) {
            ps->p += ps->p[1] == '\\' ? 2 : 3;
            skip_space(ps);
            if (*ps->p == '[') { /* \\[2pt] */
                const gchar* close = strchr(ps->p, ']');
                if (close != NULL) ps->p = close + 1;
            }
            g_ptr_array_add(row, cell);
            cell = node_new(TEX_ROW);
            row = g_ptr_array_new_with_free_func((GDestroyNotify)tex_node_free);
            g_ptr_array_add(rows, row);
        } else if (top && *ps->p == '}') {
            ps->p++;
        } else if (at_command(ps, "right")) {
            g_free(read_command(ps));
            g_free(read_delim(ps));
        } else if (top && at_command(ps, "end")) {
            g_free(read_command(ps));
            g_free(read_raw_group(ps));
        } else {
            break;
        }
    }
    g_ptr_array_add(row, cell);
    /* A last \\ leaves an empty row */
    if (rows->len > 1 && row->len == 1 && cell->items->len == 0) g_ptr_array_remove_index(rows, rows->len - 1);
    return rows;
}

TexNode* tex_math_parse(const gchar* tex) {
    Parser ps = {0};
    ps.p = tex != NULL ? tex : "";
    ps.variant = VAR_NORMAL;
    GPtrArray* rows = parse_rows(&ps, TRUE);
    GPtrArray* first = g_ptr_array_index(rows, 0);
    if (rows->len == 1 && first->len == 1) {
        TexNode* only = g_ptr_array_steal_index(first, 0);
        g_ptr_array_unref(rows);
        if (only->kind == TEX_ROW && only->items->len == 1 && only->cls == TEX_ORD) {
            TexNode* item = g_ptr_array_steal_index(only->items, 0);
            tex_node_free(only);
            return item;
        }
        return only;
    }
    /* & or \\ outside an environment: aligned rows, as MathJax shows them */
    gboolean columns = FALSE;
    for (guint i = 0; i < rows->len; i++) columns |= ((GPtrArray*)g_ptr_array_index(rows, i))->len > 1;
    TexNode* n = node_new(TEX_TABLE);
    n->items = rows;
    n->style = TEX_TABLE_ALIGNED;
    n->text = g_strdup(columns ? "rl" : "c");
    n->em = 2.0;
    return n;
}

/* --- Linear text --- */

/* TRUE when the text of n needs parentheses as a fraction part or a script */
static gboolean needs_parens(const TexNode* n, const gchar* text) {
    if (n == NULL || n->kind == TEX_ATOM || n->kind == TEX_FENCED) return FALSE;
    return g_utf8_strlen(text, -1) > 1;
}

static void append_text(GString* out, const TexNode* n);

static void append_part(GString* out, const TexNode* n) {
    GString* part = g_string_new(NULL);
    append_text(part, n);
    if (needs_parens(n, part->str)) {
        g_string_append_printf(out, "(%s)", part->str);
    } else {
        g_string_append(out, part->str);
    }
    g_string_free(part, TRUE);
}

static void append_text(GString* out, const TexNode* n) {
    if (n == NULL) return;
    switch (n->kind) {
    case TEX_ROW:
        for (guint i = 0; i < n->items->len; i++) append_text(out, g_ptr_array_index(n->items, i));
        break;
    case TEX_ATOM:
        g_string_append(out, n->text);
        break;
    case TEX_FRAC:
        if (n->style == TEX_FRAC_NO_BAR) {
            append_text(out, n->body);
            g_string_append_c(out, ' ');
            append_text(out, n->under);
        } else {
            append_part(out, n->body);
            g_string_append_c(out, '/');
            append_part(out, n->under);
        }
        break;
    case TEX_SQRT:
        if (n->under != NULL) {
            g_string_append_c(out, '[');
            append_text(out, n->under);
            g_string_append_c(out, ']');
        }
        g_string_append(out, "√");
        append_part(out, n->body);
        break;
    case TEX_SCRIPTS:
        append_text(out, n->body);
        if (n->under != NULL) {
            g_string_append_c(out, '_');
            append_part(out, n->under);
        }
        if (n->over != NULL) {
            g_string_append_c(out, '^');
            append_part(out, n->over);
        }
        break;
    case TEX_FENCED:
        g_string_append(out, n->left);
        append_text(out, n->body);
        g_string_append(out, n->right);
        break;
    case TEX_ACCENT:
        append_text(out, n->body);
        switch ((TexAccent)n->style) {
        case TEX_ACCENT_GLYPH:
        case TEX_ACCENT_WIDE: {
            static const struct {
                const gchar* glyph;
                const gchar* combining;
            } COMBINING[] = {
                {"ˆ", "\xCC\x82"}, {"ˇ", "\xCC\x8C"}, {"˜", "\xCC\x83"}, {"´", "\xCC\x81"}, {"`", "\xCC\x80"},
                {"˙", "\xCC\x87"}, {"¨", "\xCC\x88"}, {"˘", "\xCC\x86"}, {"˚", "\xCC\x8A"},
            };
            for (gsize i = 0; i < G_N_ELEMENTS(COMBINING); i++) {
                if (g_strcmp0(n->text, COMBINING[i].glyph) == 0) g_string_append(out, COMBINING[i].combining);
            }
            break;
        }
        case TEX_ACCENT_OVERLINE:
            g_string_append(out, "\xCC\x85");
            break;
        case TEX_ACCENT_UNDERLINE:
            g_string_append(out, "\xCC\xB2");
            break;
        case TEX_ACCENT_VEC:
        case TEX_ACCENT_RIGHTARROW:
            g_string_append(out, "\xE2\x83\x97");
            break;
        case TEX_ACCENT_LEFTARROW:
            g_string_append(out, "\xE2\x83\x96");
            break;
        case TEX_ACCENT_OVERBRACE:
        case TEX_ACCENT_UNDERBRACE:
            break;
        }
        break;
    case TEX_TABLE:
        for (guint r = 0; r < n->items->len; r++) {
            GPtrArray* row = g_ptr_array_index(n->items, r);
            if (r > 0) g_string_append(out, "; ");
            for (guint c = 0; c < row->len; c++) {
                if (c > 0 && n->style != TEX_TABLE_ALIGNED) g_string_append(out, ", ");
                append_text(out, g_ptr_array_index(row, c));
            }
        }
        break;
    case TEX_SPACE:
        if (n->em >= 0.25 && out->len > 0 && out->str[out->len - 1] != ' ') g_string_append_c(out, ' ');
        break;
    case TEX_BOX:
        if (n->style == TEX_BOX_FRAME) append_text(out, n->body);
        break;
    }
}

gchar* tex_math_to_text(const TexNode* node) {
    GString* out = g_string_new(NULL);
    append_text(out, node);
    return g_string_free(out, FALSE);
}
