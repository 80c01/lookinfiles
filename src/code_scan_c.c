/* C-family symbol scanner (see code_scan_c.h) */
#include "code_scan_c.h"
#include <string.h>

typedef enum {
    BLK_TOP = 0,  /* file scope */
    BLK_SCOPE,    /* extern "C" { }, namespace { }: still file scope */
    BLK_STRUCT,   /* struct / union / class body: fields and methods */
    BLK_ENUM,     /* enum body: constants */
    BLK_BODY      /* function body, initializer...: skipped */
} BlkKind;

typedef struct {
    BlkKind kind;
    gboolean is_typedef;
} Blk;

typedef struct {
    const gchar* text;
    gsize len;
    CodeLang lang;
    const gchar* only;
    gsize only_len;
    GPtrArray* out;
    GArray* lines;    /* gsize: byte offset of each line start */
    GArray* blocks;   /* Blk */
    GString* acc;     /* current statement, comments and literals blanked */
    GArray* apos;     /* gsize: source offset of each acc byte */
    gint paren;       /* ( depth inside acc */
    gboolean after_close;         /* a struct / enum body just closed: names may follow */
    gboolean after_close_typedef; /* ... and it was a typedef */
    gboolean angles;  /* < > are brackets in declarations (C++, Java, C#) */
} CScan;

/* Words that are never a symbol name */
static const gchar* const KEYWORDS[] = {
    "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Noreturn", "_Static_assert",
    "_Thread_local", "__asm", "__asm__", "__attribute", "__attribute__", "__declspec",
    "__extension__", "__inline", "__inline__", "__restrict", "__restrict__", "__typeof__",
    "__volatile__", "abstract", "alignas", "alignof", "asm", "async", "auto", "await", "bool",
    "break", "byte", "case", "catch", "char", "class", "const", "consteval", "constexpr",
    "constinit", "continue", "decltype", "default", "defined", "delegate", "delete", "do",
    "double", "else", "enum", "event", "explicit", "export", "extends", "extern", "false",
    "final", "float", "for", "friend", "goto", "if", "implements", "import", "inline", "int",
    "interface", "internal", "long", "mutable", "namespace", "native", "new", "noexcept",
    "null", "nullptr", "operator", "override", "package", "partial", "private", "protected",
    "public", "readonly", "record", "register", "restrict", "return", "sealed", "short",
    "signed", "sizeof", "static", "static_assert", "struct", "switch", "synchronized",
    "template", "this", "throw", "throws", "transient", "true", "try", "typedef", "typename",
    "typeof", "union", "unsigned", "using", "var", "virtual", "void", "volatile", "while",
};

static gboolean is_keyword(const gchar* s, gsize n) {
    for (gsize i = 0; i < G_N_ELEMENTS(KEYWORDS); i++) {
        if (strlen(KEYWORDS[i]) == n && memcmp(KEYWORDS[i], s, n) == 0) return TRUE;
    }
    return FALSE;
}

static gboolean word_is(const gchar* s, gsize n, const gchar* w) {
    return strlen(w) == n && memcmp(s, w, n) == 0;
}

static gboolean id_start(gchar c) {
    return g_ascii_isalpha(c) || c == '_' || c == '$';
}

static gboolean id_char(gchar c) {
    return g_ascii_isalnum(c) || c == '_' || c == '$';
}

/* Macro-like name: no lower-case letter, at least two characters */
static gboolean all_caps(const gchar* s, gsize n) {
    if (n < 2) return FALSE;
    for (gsize i = 0; i < n; i++) {
        if (g_ascii_islower(s[i])) return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Output                                                                    */
/* ------------------------------------------------------------------------- */

static gint line_of(CScan* sc, gsize off) {
    gint lo = 0, hi = (gint)sc->lines->len - 1;
    while (lo < hi) {
        gint mid = (lo + hi + 1) / 2;
        if (g_array_index(sc->lines, gsize, mid) <= off) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static void emit_at(CScan* sc, gsize off, gsize n, CodeSymbolKind kind, gboolean decl) {
    const gchar* name = sc->text + off;
    if (sc->only != NULL && (n != sc->only_len || memcmp(name, sc->only, n) != 0)) return;
    gint line = line_of(sc, off);
    gsize start = g_array_index(sc->lines, gsize, line);
    const gchar* nl = memchr(sc->text + start, '\n', sc->len - start);
    gsize end = nl != NULL ? (gsize)(nl - sc->text) : sc->len;
    g_ptr_array_add(sc->out, code_symbol_new(name, n, kind, decl, line, (gint)(off - start),
                                             sc->text + start, end - start));
}

/* A name found at acc index i (identifiers are copied contiguously) */
static void emit(CScan* sc, gsize i, gsize n, CodeSymbolKind kind, gboolean decl) {
    if (n == 0) return;
    emit_at(sc, g_array_index(sc->apos, gsize, i), n, kind, decl);
}

/* ------------------------------------------------------------------------- */
/* Statement analysis (on acc)                                               */
/* ------------------------------------------------------------------------- */

static void acc_add(CScan* sc, gchar c, gsize off) {
    g_string_append_c(sc->acc, c);
    g_array_append_val(sc->apos, off);
}

static void acc_clear(CScan* sc) {
    g_string_truncate(sc->acc, 0);
    g_array_set_size(sc->apos, 0);
    sc->paren = 0;
}

static BlkKind ctx_kind(CScan* sc) {
    return g_array_index(sc->blocks, Blk, sc->blocks->len - 1).kind;
}

static void push_block(CScan* sc, BlkKind kind, gboolean is_typedef) {
    Blk b = {kind, is_typedef};
    g_array_append_val(sc->blocks, b);
}

/* The identifier starting at i (or 0 when s[i] does not start one) */
static gsize ident_len(const gchar* s, gsize i, gsize e) {
    if (i >= e || !id_start(s[i])) return 0;
    gsize j = i;
    while (j < e && id_char(s[j])) j++;
    return j - i;
}

static gsize skip_spaces(const gchar* s, gsize i, gsize e) {
    while (i < e && g_ascii_isspace(s[i])) i++;
    return i;
}

/* Depth tracker over (), [], {} and, when angles, <> */
typedef struct {
    gint depth;
    gint angle;
} Depth;

/* Update the depth for s[i]; TRUE when s[i] is at depth 0 (outside any
 * bracket, the bracket characters themselves excluded) */
static gboolean depth_step(CScan* sc, Depth* d, const gchar* s, gsize i) {
    gchar c = s[i];
    if (c == '(' || c == '[' || c == '{') {
        d->depth++;
        return FALSE;
    }
    if (c == ')' || c == ']' || c == '}') {
        if (d->depth > 0) d->depth--;
        return FALSE;
    }
    if (sc->angles && d->depth == 0) {
        if (c == '<' && (i == 0 || s[i - 1] != '<') && s[i + 1] != '<' && s[i + 1] != '=') {
            d->angle++;
            return FALSE;
        }
        if (c == '>' && d->angle > 0 && (i == 0 || s[i - 1] != '-')) {
            d->angle--;
            return FALSE;
        }
    }
    return d->depth == 0 && d->angle == 0;
}

/* First '=' at depth 0 that is an assignment, or -1 */
static gssize top_assign(CScan* sc, const gchar* s, gsize b, gsize e) {
    Depth d = {0, 0};
    for (gsize i = b; i < e; i++) {
        if (!depth_step(sc, &d, s, i)) continue;
        if (s[i] != '=') continue;
        if (i + 1 < e && s[i + 1] == '=') {
            i++;
            continue;
        }
        if (i > b && strchr("=!<>+-*/%&|^", s[i - 1]) != NULL) continue;
        return (gssize)i;
    }
    return -1;
}

/* Last identifier at depth 0 in [b, e) that is not a keyword and not
 * followed by '(' (attributes); FALSE when none */
static gboolean last_name(CScan* sc, const gchar* s, gsize b, gsize e, gsize* at, gsize* n) {
    Depth d = {0, 0};
    gboolean found = FALSE;
    for (gsize i = b; i < e; i++) {
        gboolean top = depth_step(sc, &d, s, i);
        if (!top || !id_start(s[i]) || (i > b && id_char(s[i - 1]))) continue;
        gsize len = ident_len(s, i, e);
        gsize after = skip_spaces(s, i + len, e);
        if (!is_keyword(s + i, len) && !(after < e && s[after] == '(')) {
            *at = i;
            *n = len;
            found = TRUE;
        }
        i += len - 1;
    }
    return found;
}

/* Number of tokens (identifiers, '*', '&') at depth 0 in [b, e) */
static gint count_tokens(CScan* sc, const gchar* s, gsize b, gsize e) {
    Depth d = {0, 0};
    gint count = 0;
    for (gsize i = b; i < e; i++) {
        if (!depth_step(sc, &d, s, i)) continue;
        if (s[i] == '*' || s[i] == '&') {
            count++;
        } else if (id_start(s[i]) && (i == b || !id_char(s[i - 1]))) {
            count++;
            i += ident_len(s, i, e) - 1;
        }
    }
    return count;
}

/* Names of a declaration list "type a = 1, *b[2], c": each part's name.
 * need_type: the first part must have a type before its name. */
static void emit_names(CScan* sc, gsize b, gsize e, CodeSymbolKind kind, gboolean decl,
                       gboolean need_type) {
    const gchar* s = sc->acc->str;
    gboolean bitfields = kind == CODE_SYM_FIELD && (sc->lang == CODE_LANG_C || sc->lang == CODE_LANG_CPP);
    gsize part = b;
    Depth d = {0, 0};
    for (gsize i = b; i <= e; i++) {
        gboolean top = i < e ? depth_step(sc, &d, s, i) : TRUE;
        if (i < e && !(top && s[i] == ',')) continue;
        /* One part: [part, i) */
        gsize pe = i;
        gssize eq = top_assign(sc, s, part, pe);
        if (eq >= 0) pe = (gsize)eq;
        if (bitfields) {
            for (gsize j = part; j < pe; j++) {
                if (s[j] == ':' && (j + 1 >= pe || s[j + 1] != ':') && (j == part || s[j - 1] != ':')) {
                    pe = j;
                    break;
                }
            }
        }
        gboolean ok = !need_type || part != b || count_tokens(sc, s, part, pe) >= 2;
        gsize at = 0, n = 0;
        if (ok && last_name(sc, s, part, pe, &at, &n)) emit(sc, at, n, kind, decl);
        part = i + 1;
    }
}

/* A "(*name)" group (function pointer): the name inside */
static gboolean pointer_group(const gchar* s, gsize open, gsize close, gsize* at, gsize* n) {
    gsize i = skip_spaces(s, open + 1, close);
    gboolean star = FALSE;
    while (i < close && (s[i] == '*' || s[i] == '&' || s[i] == '^' || g_ascii_isspace(s[i]))) {
        if (!g_ascii_isspace(s[i])) star = TRUE;
        i++;
    }
    gsize len = ident_len(s, i, close);
    if (!star || len == 0) return FALSE;
    gsize j = skip_spaces(s, i + len, close);
    /* Arrays of function pointers: (*name[4]) */
    if (j < close && s[j] == '[') {
        while (j < close && s[j] != ']') j++;
        j = skip_spaces(s, j + 1, close);
    }
    if (j != close) return FALSE;
    *at = i;
    *n = len;
    return TRUE;
}

/* Paren groups at depth 0 of [b, e): pairs of (open, close) indexes */
static GArray* top_groups(const gchar* s, gsize b, gsize e) {
    GArray* g = g_array_new(FALSE, FALSE, sizeof(gsize));
    gint depth = 0;
    gsize open = 0;
    for (gsize i = b; i < e; i++) {
        if (s[i] == '(') {
            if (depth == 0) open = i;
            depth++;
        } else if (s[i] == ')' && depth > 0) {
            depth--;
            if (depth == 0) {
                g_array_append_val(g, open);
                g_array_append_val(g, i);
            }
        }
    }
    return g;
}

/* Name of a function declared or defined by [b, e): the identifier before
 * its parameter list (the last paren group not preceded by an attribute or
 * a macro), or the name of a function pointer "(*name)(...)" (*pointer) */
static gboolean function_name(CScan* sc, gsize b, gsize e, gsize* at, gsize* n, gboolean* pointer) {
    const gchar* s = sc->acc->str;
    GArray* g = top_groups(s, b, e);
    gint groups = (gint)g->len / 2;
    gboolean found = FALSE;
    *pointer = FALSE;
    for (gint i = groups - 1; i >= 0 && !found; i--) {
        gsize open = g_array_index(g, gsize, 2 * i);
        gsize close = g_array_index(g, gsize, 2 * i + 1);
        if (i < groups - 1 && pointer_group(s, open, close, at, n)) {
            *pointer = TRUE;
            found = TRUE;
        }
    }
    for (gint pass = 0; pass < 2 && !found; pass++) {
        for (gint i = groups - 1; i >= 0; i--) {
            gsize open = g_array_index(g, gsize, 2 * i);
            gsize j = open;
            while (j > b && g_ascii_isspace(s[j - 1])) j--;
            gsize end = j;
            while (j > b && id_char(s[j - 1])) j--;
            gsize len = end - j;
            if (len == 0 || !id_start(s[j]) || is_keyword(s + j, len)) continue;
            /* Pass 0 skips macro calls (G_GNUC_PRINTF (1, 2), G_DEFINE_TYPE (...)) */
            if (pass == 0 && all_caps(s + j, len)) continue;
            *at = j;
            *n = len;
            found = TRUE;
            break;
        }
    }
    g_array_free(g, TRUE);
    return found;
}

/* struct / union / enum / class / interface / record keyword at depth 0:
 * the index after the last one (0 when none), its kind and enum flag */
static gsize aggregate_keyword(CScan* sc, gsize b, gsize e, CodeSymbolKind* kind, gboolean* is_enum,
                               gsize* last_paren) {
    const gchar* s = sc->acc->str;
    Depth d = {0, 0};
    gsize after = 0;
    *last_paren = 0;
    *is_enum = FALSE;
    for (gsize i = b; i < e; i++) {
        if (s[i] == '(' && d.depth == 0 && d.angle == 0) *last_paren = i + 1;
        if (!depth_step(sc, &d, s, i)) continue;
        if (!id_start(s[i]) || (i > b && id_char(s[i - 1]))) continue;
        gsize len = ident_len(s, i, e);
        const gchar* w = s + i;
        /* "enum class Name" / "enum struct Name": still an enum */
        gboolean enum_tag = *is_enum && after > 0 && skip_spaces(s, after, e) == i;
        if (word_is(w, len, "struct") || word_is(w, len, "union")) {
            if (!enum_tag) {
                *kind = CODE_SYM_TYPE;
                *is_enum = FALSE;
            }
            after = i + len;
        } else if (word_is(w, len, "enum")) {
            *kind = CODE_SYM_TYPE;
            *is_enum = TRUE;
            after = i + len;
        } else if (word_is(w, len, "class") || word_is(w, len, "interface") || word_is(w, len, "record")) {
            if (!enum_tag) {
                *kind = sc->lang == CODE_LANG_C ? CODE_SYM_TYPE : CODE_SYM_CLASS;
                *is_enum = FALSE;
            }
            after = i + len;
        }
        i += len - 1;
    }
    return after;
}

/* Name of an aggregate after its keyword: the last identifier before ':',
 * '<', '{', extends / implements ("class EXPORT Name final : Base") */
static gboolean aggregate_name(CScan* sc, gsize from, gsize e, gsize* at, gsize* n) {
    const gchar* s = sc->acc->str;
    gboolean found = FALSE;
    gsize i = from;
    while (i < e) {
        i = skip_spaces(s, i, e);
        if (i >= e) break;
        gchar c = s[i];
        if (c == ':' && i + 1 < e && s[i + 1] == ':') {
            i += 2;
            continue;
        }
        if (c == ':' || c == '<' || c == '{' || c == ';' || c == '=' || c == ',') break;
        if (c == '(' || c == '[') {
            /* Attribute arguments: __attribute__((packed)), [[nodiscard]] */
            gchar open = c, close = c == '(' ? ')' : ']';
            gint depth = 0;
            for (; i < e; i++) {
                if (s[i] == open) depth++;
                else if (s[i] == close && --depth == 0) break;
            }
            i++;
            continue;
        }
        gsize len = ident_len(s, i, e);
        if (len == 0) {
            i++;
            continue;
        }
        const gchar* w = s + i;
        if (word_is(w, len, "extends") || word_is(w, len, "implements") || word_is(w, len, "where") ||
            word_is(w, len, "permits")) {
            break;
        }
        gsize after = skip_spaces(s, i + len, e);
        if (!is_keyword(w, len) && !(after < e && s[after] == '(')) {
            *at = i;
            *n = len;
            found = TRUE;
        }
        i += len;
    }
    return found;
}

/* Statement start: skip macros written before a keyword (G_BEGIN_DECLS
 * typedef ..., EXPORT_API extern ...) */
static gsize statement_start(const gchar* s, gsize b, gsize e) {
    for (;;) {
        gsize len = ident_len(s, b, e);
        if (len == 0 || !all_caps(s + b, len)) return b;
        gsize next = skip_spaces(s, b + len, e);
        gsize nlen = ident_len(s, next, e);
        if (nlen == 0 || !is_keyword(s + next, nlen)) return b;
        b = next;
    }
}

/* Index of a top-level word in [b, e), or -1 */
static gssize find_word(CScan* sc, gsize b, gsize e, const gchar* word) {
    const gchar* s = sc->acc->str;
    Depth d = {0, 0};
    for (gsize i = b; i < e; i++) {
        if (!depth_step(sc, &d, s, i)) continue;
        if (!id_start(s[i]) || (i > b && id_char(s[i - 1]))) continue;
        gsize len = ident_len(s, i, e);
        if (word_is(s + i, len, word)) return (gssize)i;
        i += len - 1;
    }
    return -1;
}

/* C++ constructor initializer list: "Foo::Foo() : a(1), b(2)" ends at ':' */
static gsize cut_initializers(const gchar* s, gsize b, gsize e) {
    gint depth = 0;
    for (gsize i = b; i < e; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')' && depth > 0 && --depth == 0) {
            gsize j = skip_spaces(s, i + 1, e);
            if (j < e && s[j] == ':' && (j + 1 >= e || s[j + 1] != ':')) return i + 1;
            return e;
        }
    }
    return e;
}

/* One complete statement in acc, ended by '{' (brace) or ';' */
static void classify(CScan* sc, gboolean brace) {
    const gchar* s = sc->acc->str;
    gsize b = skip_spaces(s, 0, sc->acc->len);
    gsize e = sc->acc->len;
    while (e > b && g_ascii_isspace(s[e - 1])) e--;
    BlkKind ctx = ctx_kind(sc);
    gboolean in_struct = ctx == BLK_STRUCT;
    CodeSymbolKind var_kind = in_struct ? CODE_SYM_FIELD : CODE_SYM_VARIABLE;
    BlkKind push = BLK_BODY;
    gboolean push_typedef = FALSE;
    gsize at = 0, n = 0;

    if (sc->after_close) {
        /* "} Name, *PName;" after a struct / enum body */
        sc->after_close = FALSE;
        if (b < e) emit_names(sc, b, e, sc->after_close_typedef ? CODE_SYM_TYPE : var_kind, FALSE, FALSE);
        goto done;
    }
    if (b >= e) goto done;
    b = statement_start(s, b, e);

    gsize flen = ident_len(s, b, e);
    const gchar* first = s + b;
    if (word_is(first, flen, "return") || word_is(first, flen, "goto") || word_is(first, flen, "break") ||
        word_is(first, flen, "continue") || word_is(first, flen, "import") ||
        word_is(first, flen, "package") || word_is(first, flen, "static_assert") ||
        word_is(first, flen, "_Static_assert") || word_is(first, flen, "friend") ||
        word_is(first, flen, "case") || word_is(first, flen, "throw") || word_is(first, flen, "delete")) {
        goto done;
    }
    if (word_is(first, flen, "using")) {
        /* using Name = type; (using namespace std; declares nothing) */
        gssize eq = top_assign(sc, s, b, e);
        if (eq >= 0 && last_name(sc, s, b + flen, (gsize)eq, &at, &n)) emit(sc, at, n, CODE_SYM_TYPE, FALSE);
        goto done;
    }
    if (word_is(first, flen, "namespace")) {
        if (aggregate_name(sc, b + flen, e, &at, &n)) emit(sc, at, n, CODE_SYM_MODULE, FALSE);
        push = BLK_SCOPE;
        goto done;
    }
    if (word_is(first, flen, "extern") && brace) {
        gsize next = skip_spaces(s, b + flen, e);
        if (next < e && s[next] == '"' && strchr(s + next, '(') == NULL) {
            push = BLK_SCOPE;
            goto done;
        }
    }

    gssize tdef = find_word(sc, b, e, "typedef");
    gboolean is_typedef = tdef >= 0;
    if (is_typedef) b = (gsize)tdef;
    gboolean is_extern = find_word(sc, b, e, "extern") >= 0;

    gssize eq = top_assign(sc, s, b, e);
    if (eq >= 0 && !is_typedef) {
        /* Initialized variable: int x = 1, y; Foo bar = { ... } */
        emit_names(sc, b, e, var_kind, is_extern, TRUE);
        goto done;
    }

    CodeSymbolKind agg_kind = CODE_SYM_TYPE;
    gboolean is_enum = FALSE;
    gsize last_paren = 0;
    gsize agg = aggregate_keyword(sc, b, e, &agg_kind, &is_enum, &last_paren);
    if (agg > 0 && last_paren < agg) {
        gboolean named = aggregate_name(sc, agg, e, &at, &n);
        if (brace) {
            if (named) emit(sc, at, n, agg_kind, FALSE);
            push = is_enum ? BLK_ENUM : BLK_STRUCT;
            push_typedef = is_typedef;
            goto done;
        }
        if (!is_typedef) {
            gsize after = skip_spaces(s, agg, e);
            gsize len = ident_len(s, after, e);
            if (len > 0 && skip_spaces(s, after + len, e) == e) {
                /* Forward declaration: struct foo; */
                emit(sc, after, len, agg_kind, TRUE);
            } else {
                emit_names(sc, agg, e, var_kind, is_extern, FALSE);
            }
            goto done;
        }
    }

    if (is_typedef && !brace) {
        gboolean pointer = FALSE;
        if (strchr(s + b, '(') != NULL && function_name(sc, b, e, &at, &n, &pointer)) {
            emit(sc, at, n, CODE_SYM_TYPE, FALSE);
        } else {
            emit_names(sc, b + strlen("typedef"), e, CODE_SYM_TYPE, FALSE, TRUE);
        }
        goto done;
    }

    if (memchr(s + b, '(', e - b) != NULL) {
        gsize fe = sc->lang == CODE_LANG_CPP ? cut_initializers(s, b, e) : e;
        gboolean pointer = FALSE;
        if (function_name(sc, b, fe, &at, &n, &pointer)) {
            if (pointer) {
                emit(sc, at, n, var_kind, is_extern);
            } else {
                gboolean method = in_struct || (at >= 2 && s[at - 1] == ':' && s[at - 2] == ':');
                emit(sc, at, n, method ? CODE_SYM_METHOD : CODE_SYM_FUNCTION, !brace);
            }
            goto done;
        }
    }

    if (brace) {
        /* C# property: public int Count { get; set; } */
        if (in_struct && count_tokens(sc, s, b, e) >= 2 && last_name(sc, s, b, e, &at, &n)) {
            emit(sc, at, n, CODE_SYM_FIELD, FALSE);
        }
        goto done;
    }
    emit_names(sc, b, e, var_kind, is_extern, TRUE);

done:
    if (brace) push_block(sc, push, push_typedef);
    acc_clear(sc);
}

/* One enum constant (the text before ',' or '}') */
static void enum_item(CScan* sc) {
    const gchar* s = sc->acc->str;
    gsize e = sc->acc->len;
    gsize i = skip_spaces(s, 0, e);
    /* Annotations (@Deprecated) and attributes ([[deprecated]]) */
    while (i < e && (s[i] == '@' || s[i] == '[')) {
        if (s[i] == '[') {
            while (i < e && s[i] != ']') i++;
            while (i < e && s[i] == ']') i++;
        } else {
            i++;
            i += ident_len(s, i, e);
            gsize j = skip_spaces(s, i, e);
            if (j < e && s[j] == '(') {
                gint depth = 0;
                for (; j < e; j++) {
                    if (s[j] == '(') depth++;
                    else if (s[j] == ')' && --depth == 0) break;
                }
                i = j + 1;
            }
        }
        i = skip_spaces(s, i, e);
    }
    gsize len = ident_len(s, i, e);
    if (len > 0 && !is_keyword(s + i, len)) emit(sc, i, len, CODE_SYM_CONSTANT, FALSE);
    acc_clear(sc);
}

static void close_block(CScan* sc) {
    if (sc->blocks->len <= 1) {
        acc_clear(sc);
        return;
    }
    Blk top = g_array_index(sc->blocks, Blk, sc->blocks->len - 1);
    if (top.kind == BLK_ENUM) enum_item(sc);
    g_array_set_size(sc->blocks, sc->blocks->len - 1);
    /* Java and C# bodies are never followed by declarator names */
    gboolean names_follow = sc->lang == CODE_LANG_C || sc->lang == CODE_LANG_CPP;
    sc->after_close = names_follow && (top.kind == BLK_STRUCT || top.kind == BLK_ENUM);
    sc->after_close_typedef = top.is_typedef;
    acc_clear(sc);
}

/* ------------------------------------------------------------------------- */
/* Lexing                                                                    */
/* ------------------------------------------------------------------------- */

/* Index of the last character of the string or character literal opened
 * at i (the quote); an unterminated literal ends before the newline */
static gsize skip_literal(CScan* sc, gsize i) {
    const gchar* t = sc->text;
    gsize len = sc->len;
    gchar q = t[i];
    if (q == '"' && sc->lang == CODE_LANG_CPP && i > 0 && t[i - 1] == 'R') {
        /* Raw string R"delim( ... )delim" */
        gsize open = i + 1;
        while (open < len && t[open] != '(' && t[open] != '\n' && open - i <= 17) open++;
        if (open < len && t[open] == '(') {
            gsize dlen = open - i - 1;
            for (gsize j = open + 1; j + dlen + 1 < len; j++) {
                if (t[j] == ')' && memcmp(t + j + 1, t + i + 1, dlen) == 0 && t[j + dlen + 1] == '"') {
                    return j + dlen + 1;
                }
            }
            return len - 1;
        }
    }
    if (q == '"' && sc->lang == CODE_LANG_CSHARP && i > 0 && t[i - 1] == '@') {
        /* Verbatim string: no escapes, "" is a quote */
        for (gsize j = i + 1; j < len; j++) {
            if (t[j] == '"') {
                if (j + 1 < len && t[j + 1] == '"') j++;
                else return j;
            }
        }
        return len - 1;
    }
    if (q == '"' && i + 2 < len && t[i + 1] == '"' && t[i + 2] == '"' &&
        (sc->lang == CODE_LANG_JAVA || sc->lang == CODE_LANG_CSHARP)) {
        /* Text block """ ... """ */
        for (gsize j = i + 3; j + 2 < len; j++) {
            if (t[j] == '\\') {
                j++;
                continue;
            }
            if (t[j] == '"' && t[j + 1] == '"' && t[j + 2] == '"') return j + 2;
        }
        return len - 1;
    }
    for (gsize j = i + 1; j < len; j++) {
        if (t[j] == '\\') {
            j++;
            continue;
        }
        if (t[j] == q) return j;
        if (t[j] == '\n') return j - 1;
    }
    return len - 1;
}

typedef struct {
    gboolean skipping;  /* inside an #else / #elif branch */
    gint nest;          /* #if nesting inside the skipped branch */
} PpState;

/* A preprocessor line starting at i ('#'): returns the index of its
 * newline (or len). Defines macros; skips the #else branches. */
static gsize directive(CScan* sc, gsize i, PpState* pp) {
    const gchar* t = sc->text;
    gsize len = sc->len;
    gsize j = i + 1;
    while (j < len && (t[j] == ' ' || t[j] == '\t')) j++;
    gsize w = j;
    while (j < len && g_ascii_isalpha(t[j])) j++;
    gsize wlen = j - w;
    const gchar* word = t + w;
    if (word_is(word, wlen, "if") || word_is(word, wlen, "ifdef") || word_is(word, wlen, "ifndef")) {
        if (pp->skipping) pp->nest++;
    } else if (word_is(word, wlen, "else") || word_is(word, wlen, "elif") ||
               word_is(word, wlen, "elifdef") || word_is(word, wlen, "elifndef")) {
        if (!pp->skipping) {
            pp->skipping = TRUE;
            pp->nest = 0;
        }
    } else if (word_is(word, wlen, "endif")) {
        if (pp->skipping) {
            if (pp->nest == 0) pp->skipping = FALSE;
            else pp->nest--;
        }
    } else if (word_is(word, wlen, "define") && !pp->skipping &&
               (sc->lang == CODE_LANG_C || sc->lang == CODE_LANG_CPP || sc->lang == CODE_LANG_CSHARP)) {
        while (j < len && (t[j] == ' ' || t[j] == '\t')) j++;
        gsize n = 0;
        while (j + n < len && id_char(t[j + n])) n++;
        if (n > 0 && id_start(t[j])) emit_at(sc, j, n, CODE_SYM_MACRO, FALSE);
    }
    /* To the end of the line, with \ continuations and comments */
    for (; j < len; j++) {
        if (t[j] == '\\' && j + 1 < len && (t[j + 1] == '\n' || t[j + 1] == '\r')) {
            j++;
            if (t[j] == '\r' && j + 1 < len && t[j + 1] == '\n') j++;
            continue;
        }
        if (t[j] == '/' && j + 1 < len && t[j + 1] == '*') {
            const gchar* end = g_strstr_len(t + j + 2, (gssize)(len - j - 2), "*/");
            j = end != NULL ? (gsize)(end - t) + 1 : len;
            continue;
        }
        if (t[j] == '/' && j + 1 < len && t[j + 1] == '/') {
            while (j < len && t[j] != '\n') j++;
            return j;
        }
        if (t[j] == '\n') return j;
    }
    return len;
}

void code_scan_c(const gchar* text, gsize len, CodeLang lang, const gchar* only_name,
                 GPtrArray* out) {
    if (text == NULL || out == NULL) return;
    if (only_name != NULL && !code_text_has_word(text, len, only_name, lang)) return;
    CScan sc = {0};
    sc.text = text;
    sc.len = len;
    sc.lang = lang;
    sc.only = only_name;
    sc.only_len = only_name != NULL ? strlen(only_name) : 0;
    sc.out = out;
    sc.angles = lang == CODE_LANG_CPP || lang == CODE_LANG_JAVA || lang == CODE_LANG_CSHARP;
    sc.lines = g_array_new(FALSE, FALSE, sizeof(gsize));
    sc.blocks = g_array_new(FALSE, FALSE, sizeof(Blk));
    sc.acc = g_string_new(NULL);
    sc.apos = g_array_new(FALSE, FALSE, sizeof(gsize));
    gsize zero = 0;
    g_array_append_val(sc.lines, zero);
    for (const gchar* p = text; (p = memchr(p, '\n', len - (gsize)(p - text))) != NULL; p++) {
        gsize start = (gsize)(p - text) + 1;
        g_array_append_val(sc.lines, start);
    }
    push_block(&sc, BLK_TOP, FALSE);

    PpState pp = {FALSE, 0};
    gboolean line_blank = TRUE;
    for (gsize i = 0; i < len; i++) {
        gchar c = text[i];
        BlkKind ctx = ctx_kind(&sc);
        gboolean collect = !pp.skipping && ctx != BLK_BODY;
        if (c == '\n') {
            line_blank = TRUE;
            if (collect) acc_add(&sc, ' ', i);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            if (collect) acc_add(&sc, ' ', i);
            continue;
        }
        if (c == '/' && i + 1 < len && text[i + 1] == '/') {
            while (i + 1 < len && text[i + 1] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < len && text[i + 1] == '*') {
            const gchar* end = g_strstr_len(text + i + 2, (gssize)(len - i - 2), "*/");
            i = end != NULL ? (gsize)(end - text) + 1 : len;
            if (collect) acc_add(&sc, ' ', i < len ? i : len - 1);
            continue;
        }
        if (line_blank && c == '#') {
            i = directive(&sc, i, &pp) - 1;
            continue;
        }
        line_blank = FALSE;
        if (c == '\'' && lang == CODE_LANG_CPP && i > 0 && g_ascii_isxdigit(text[i - 1]) &&
            i + 1 < len && g_ascii_isxdigit(text[i + 1])) {
            continue; /* digit separator: 1'000'000 */
        }
        if (c == '"' || c == '\'') {
            gsize start = i;
            i = skip_literal(&sc, i);
            if (collect) acc_add(&sc, '"', start);
            continue;
        }
        if (pp.skipping) continue;
        if (ctx == BLK_BODY) {
            if (c == '{') push_block(&sc, BLK_BODY, FALSE);
            else if (c == '}') close_block(&sc);
            continue;
        }
        switch (c) {
            case '(':
                sc.paren++;
                acc_add(&sc, c, i);
                break;
            case ')':
                if (sc.paren > 0) sc.paren--;
                acc_add(&sc, c, i);
                break;
            case '{':
                if (sc.paren > 0) {
                    acc_add(&sc, c, i);
                } else if (ctx == BLK_ENUM) {
                    /* Java enum constant with a body: RED { ... } */
                    enum_item(&sc);
                    push_block(&sc, BLK_BODY, FALSE);
                } else {
                    classify(&sc, TRUE);
                }
                break;
            case '}':
                if (sc.paren > 0) acc_add(&sc, c, i);
                else close_block(&sc);
                break;
            case ';':
                if (sc.paren > 0) {
                    acc_add(&sc, c, i);
                } else if (ctx == BLK_ENUM) {
                    /* Java: the constants end, members follow */
                    enum_item(&sc);
                    g_array_index(sc.blocks, Blk, sc.blocks->len - 1).kind = BLK_STRUCT;
                } else {
                    classify(&sc, FALSE);
                }
                break;
            case ',':
                if (ctx == BLK_ENUM && sc.paren == 0) enum_item(&sc);
                else acc_add(&sc, c, i);
                break;
            case ':':
                if (ctx == BLK_STRUCT && sc.paren == 0 && (i + 1 >= len || text[i + 1] != ':') &&
                    (i == 0 || text[i - 1] != ':')) {
                    /* Access specifiers: public: protected: private: (Qt: public slots:) */
                    static const gchar* const LABELS[] = {
                        "public", "protected", "private", "internal", "signals", "Q_SIGNALS",
                        "Q_SLOTS", "public slots", "protected slots", "private slots",
                        "public Q_SLOTS", "protected Q_SLOTS", "private Q_SLOTS",
                    };
                    gchar* label = g_strstrip(g_strdup(sc.acc->str));
                    gboolean access = FALSE;
                    for (gsize k = 0; k < G_N_ELEMENTS(LABELS) && !access; k++) {
                        access = strcmp(label, LABELS[k]) == 0;
                    }
                    g_free(label);
                    if (access) {
                        acc_clear(&sc);
                        break;
                    }
                }
                acc_add(&sc, c, i);
                break;
            default:
                acc_add(&sc, c, i);
                break;
        }
    }

    g_array_free(sc.lines, TRUE);
    g_array_free(sc.blocks, TRUE);
    g_string_free(sc.acc, TRUE);
    g_array_free(sc.apos, TRUE);
}
