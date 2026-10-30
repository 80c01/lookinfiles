/* Symbol scanners of the built-in code index (see code_scan.h) */
#include "code_scan.h"
#include "code_scan_c.h"
#include <string.h>

/* Rule flags */
enum {
    RULE_TOP_ONLY = 1 << 0,  /* only at column 0 */
    RULE_LAST_PART = 1 << 1, /* keep the last part of a dotted name (M.foo, A::B) */
    RULE_TARGETS = 1 << 2,   /* Makefile: several targets before ':' */
    RULE_DECL = 1 << 3       /* a declaration, not a definition */
};

/* Line-scoped states for the blocks of Go and Rust */
typedef enum {
    BLOCK_NONE = 0,
    BLOCK_GO_CONST,
    BLOCK_GO_VAR,
    BLOCK_GO_TYPE,
    BLOCK_GO_STRUCT,
    BLOCK_GO_INTERFACE,
    BLOCK_RUST_STRUCT,
    BLOCK_RUST_ENUM
} BlockState;

typedef struct {
    CodeLang lang;
    BlockState block;    /* BLOCK_NONE: anywhere outside blocks; else only inside it */
    const gchar* pattern; /* the name is the group (?P<n>...) */
    CodeSymbolKind top;   /* kind at column 0 */
    CodeSymbolKind indented;
    guint flags;
    BlockState opens;     /* block entered after the line (Go, Rust) */
} Rule;

#define RUST_VIS "(?:pub(?:\\([^)]*\\))?\\s+)?"

static const Rule RULES[] = {
    /* Python */
    {CODE_LANG_PYTHON, BLOCK_NONE, "^\\s*(?:async\\s+)?def\\s+(?P<n>[A-Za-z_]\\w*)", CODE_SYM_FUNCTION, CODE_SYM_METHOD, 0, 0},
    {CODE_LANG_PYTHON, BLOCK_NONE, "^\\s*class\\s+(?P<n>[A-Za-z_]\\w*)", CODE_SYM_CLASS, CODE_SYM_CLASS, 0, 0},
    {CODE_LANG_PYTHON, BLOCK_NONE, "^\\s+self\\.(?P<n>[A-Za-z_]\\w*)\\s*(?::[^=]*)?=(?!=)", CODE_SYM_FIELD, CODE_SYM_FIELD, 0, 0},
    {CODE_LANG_PYTHON, BLOCK_NONE, "^(?P<n>[A-Za-z_]\\w*)\\s*(?::[^=]*)?=(?!=)", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, RULE_TOP_ONLY, 0},
    /* Shell */
    {CODE_LANG_SHELL, BLOCK_NONE, "^\\s*function\\s+(?P<n>[A-Za-z_][\\w:.-]*)", CODE_SYM_FUNCTION, CODE_SYM_FUNCTION, 0, 0},
    {CODE_LANG_SHELL, BLOCK_NONE, "^\\s*(?P<n>[A-Za-z_][\\w:.-]*)\\s*\\(\\s*\\)", CODE_SYM_FUNCTION, CODE_SYM_FUNCTION, 0, 0},
    {CODE_LANG_SHELL, BLOCK_NONE, "^\\s*(?:(?:export|local|readonly|declare|typeset)(?:\\s+-\\w+)*\\s+)?(?P<n>[A-Za-z_]\\w*)(?:\\[[^\\]]*\\])?\\+?=", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, 0, 0},
    /* JavaScript / TypeScript */
    {CODE_LANG_JS, BLOCK_NONE, "^\\s*(?:export\\s+)?(?:default\\s+)?(?:async\\s+)?function\\s*\\*?\\s*(?P<n>[\\w$]+)", CODE_SYM_FUNCTION, CODE_SYM_FUNCTION, 0, 0},
    {CODE_LANG_JS, BLOCK_NONE, "^\\s*(?:export\\s+)?(?:default\\s+)?(?:abstract\\s+)?class\\s+(?P<n>[\\w$]+)", CODE_SYM_CLASS, CODE_SYM_CLASS, 0, 0},
    {CODE_LANG_JS, BLOCK_NONE, "^\\s*(?:export\\s+)?(?:declare\\s+)?(?:const\\s+)?(?:interface|type|enum)\\s+(?P<n>[\\w$]+)", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, 0},
    {CODE_LANG_JS, BLOCK_NONE, "^\\s*(?:export\\s+)?(?:declare\\s+)?(?:namespace|module)\\s+(?P<n>[\\w$.]+)", CODE_SYM_MODULE, CODE_SYM_MODULE, RULE_LAST_PART, 0},
    {CODE_LANG_JS, BLOCK_NONE, "^\\s*(?:export\\s+)?(?:declare\\s+)?(?:const|let|var)\\s+(?P<n>[\\w$]+)", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, 0, 0},
    {CODE_LANG_JS, BLOCK_NONE, "^\\s+(?:(?:static|async|get|set|public|private|protected|readonly|override)\\s+)*(?P<n>[\\w$]+)\\s*(?:<[^>]*>)?\\s*\\([^)]*\\)\\s*(?::\\s*[^={]+)?\\{", CODE_SYM_METHOD, CODE_SYM_METHOD, 0, 0},
    /* Go */
    {CODE_LANG_GO, BLOCK_NONE, "^func\\s+\\([^)]*\\)\\s*(?P<n>\\w+)", CODE_SYM_METHOD, CODE_SYM_METHOD, 0, 0},
    {CODE_LANG_GO, BLOCK_NONE, "^func\\s+(?P<n>\\w+)", CODE_SYM_FUNCTION, CODE_SYM_FUNCTION, 0, 0},
    {CODE_LANG_GO, BLOCK_NONE, "^\\s*type\\s+(?P<n>\\w+)\\s+struct\\s*\\{\\s*$", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, BLOCK_GO_STRUCT},
    {CODE_LANG_GO, BLOCK_NONE, "^\\s*type\\s+(?P<n>\\w+)\\s+interface\\s*\\{\\s*$", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, BLOCK_GO_INTERFACE},
    {CODE_LANG_GO, BLOCK_NONE, "^\\s*type\\s+(?P<n>\\w+)", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, 0},
    {CODE_LANG_GO, BLOCK_NONE, "^const\\s+(?P<n>\\w+)", CODE_SYM_CONSTANT, CODE_SYM_CONSTANT, 0, 0},
    {CODE_LANG_GO, BLOCK_NONE, "^var\\s+(?P<n>\\w+)", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, 0, 0},
    {CODE_LANG_GO, BLOCK_GO_CONST, "^\\s+(?P<n>[A-Za-z_]\\w*)", CODE_SYM_CONSTANT, CODE_SYM_CONSTANT, 0, 0},
    {CODE_LANG_GO, BLOCK_GO_VAR, "^\\s+(?P<n>[A-Za-z_]\\w*)", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, 0, 0},
    {CODE_LANG_GO, BLOCK_GO_TYPE, "^\\s+(?P<n>[A-Za-z_]\\w*)", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, 0},
    {CODE_LANG_GO, BLOCK_GO_STRUCT, "^\\s+(?P<n>[A-Za-z_]\\w*)(?:\\s*,\\s*\\w+)*\\s+[^\\s/]", CODE_SYM_FIELD, CODE_SYM_FIELD, 0, 0},
    {CODE_LANG_GO, BLOCK_GO_INTERFACE, "^\\s+(?P<n>[A-Za-z_]\\w*)\\s*\\(", CODE_SYM_METHOD, CODE_SYM_METHOD, RULE_DECL, 0},
    /* Rust */
    {CODE_LANG_RUST, BLOCK_NONE, "^\\s*" RUST_VIS "(?:(?:const|async|unsafe|extern\\s+\"[^\"]*\")\\s+)*fn\\s+(?P<n>\\w+)", CODE_SYM_FUNCTION, CODE_SYM_METHOD, 0, 0},
    {CODE_LANG_RUST, BLOCK_NONE, "^\\s*" RUST_VIS "struct\\s+(?P<n>\\w+)[^;(]*\\{\\s*$", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, BLOCK_RUST_STRUCT},
    {CODE_LANG_RUST, BLOCK_NONE, "^\\s*" RUST_VIS "enum\\s+(?P<n>\\w+)[^;]*\\{\\s*$", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, BLOCK_RUST_ENUM},
    {CODE_LANG_RUST, BLOCK_NONE, "^\\s*" RUST_VIS "(?:struct|enum|union|trait|type)\\s+(?P<n>\\w+)", CODE_SYM_TYPE, CODE_SYM_TYPE, 0, 0},
    {CODE_LANG_RUST, BLOCK_NONE, "^\\s*" RUST_VIS "mod\\s+(?P<n>\\w+)", CODE_SYM_MODULE, CODE_SYM_MODULE, 0, 0},
    {CODE_LANG_RUST, BLOCK_NONE, "^\\s*" RUST_VIS "(?:const|static)\\s+(?:mut\\s+)?(?P<n>\\w+)\\s*:", CODE_SYM_CONSTANT, CODE_SYM_CONSTANT, 0, 0},
    {CODE_LANG_RUST, BLOCK_NONE, "^\\s*macro_rules!\\s*(?P<n>\\w+)", CODE_SYM_MACRO, CODE_SYM_MACRO, 0, 0},
    {CODE_LANG_RUST, BLOCK_RUST_STRUCT, "^\\s+" RUST_VIS "(?P<n>[a-z_]\\w*)\\s*:", CODE_SYM_FIELD, CODE_SYM_FIELD, 0, 0},
    {CODE_LANG_RUST, BLOCK_RUST_ENUM, "^\\s+(?P<n>[A-Z]\\w*)", CODE_SYM_CONSTANT, CODE_SYM_CONSTANT, 0, 0},
    /* Lua */
    {CODE_LANG_LUA, BLOCK_NONE, "^\\s*(?:local\\s+)?function\\s+(?P<n>[\\w.:]+)", CODE_SYM_FUNCTION, CODE_SYM_FUNCTION, RULE_LAST_PART, 0},
    {CODE_LANG_LUA, BLOCK_NONE, "^\\s*(?:local\\s+)?(?P<n>[\\w.]+)\\s*=\\s*function\\b", CODE_SYM_FUNCTION, CODE_SYM_FUNCTION, RULE_LAST_PART, 0},
    {CODE_LANG_LUA, BLOCK_NONE, "^\\s*local\\s+(?P<n>[A-Za-z_]\\w*)", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, 0, 0},
    {CODE_LANG_LUA, BLOCK_NONE, "^(?P<n>[A-Za-z_][\\w.]*)\\s*=(?!=)", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, RULE_TOP_ONLY | RULE_LAST_PART, 0},
    /* Ruby */
    {CODE_LANG_RUBY, BLOCK_NONE, "^\\s*def\\s+(?:self\\.)?(?P<n>\\w+[?!=]?)", CODE_SYM_FUNCTION, CODE_SYM_METHOD, 0, 0},
    {CODE_LANG_RUBY, BLOCK_NONE, "^\\s*class\\s+(?P<n>[\\w:]+)", CODE_SYM_CLASS, CODE_SYM_CLASS, RULE_LAST_PART, 0},
    {CODE_LANG_RUBY, BLOCK_NONE, "^\\s*module\\s+(?P<n>[\\w:]+)", CODE_SYM_MODULE, CODE_SYM_MODULE, RULE_LAST_PART, 0},
    {CODE_LANG_RUBY, BLOCK_NONE, "^\\s*attr_(?:reader|writer|accessor)\\s+:(?P<n>\\w+)", CODE_SYM_FIELD, CODE_SYM_FIELD, 0, 0},
    {CODE_LANG_RUBY, BLOCK_NONE, "^\\s*(?P<n>[A-Z]\\w*)\\s*=(?!=)", CODE_SYM_CONSTANT, CODE_SYM_CONSTANT, 0, 0},
    /* Makefile */
    {CODE_LANG_MAKE, BLOCK_NONE, "^define\\s+(?P<n>[\\w.-]+)", CODE_SYM_MACRO, CODE_SYM_MACRO, 0, 0},
    {CODE_LANG_MAKE, BLOCK_NONE, "^\\s*(?:export\\s+|override\\s+)?(?P<n>[A-Za-z_][\\w.-]*)\\s*(?::::=|::=|:=|\\?=|\\+=|!=|=)", CODE_SYM_VARIABLE, CODE_SYM_VARIABLE, 0, 0},
    {CODE_LANG_MAKE, BLOCK_NONE, "^(?P<n>[^\\s:=#$%.][^:=#]*?)\\s*(?::(?![=:])|::(?!=))", CODE_SYM_TARGET, CODE_SYM_TARGET, RULE_TARGETS, 0},
};

static GRegex* compiled[G_N_ELEMENTS(RULES)];

static void compile_rules(void) {
    static gsize done = 0;
    if (g_once_init_enter(&done)) {
        for (gsize i = 0; i < G_N_ELEMENTS(RULES); i++) {
            GError* err = NULL;
            compiled[i] = g_regex_new(RULES[i].pattern, G_REGEX_OPTIMIZE, 0, &err);
            if (compiled[i] == NULL) {
                g_warning("code_scan: bad pattern %s: %s", RULES[i].pattern, err->message);
                g_error_free(err);
            }
        }
        g_once_init_leave(&done, 1);
    }
}

gboolean code_scan_supported(CodeLang lang) {
    return lang != CODE_LANG_NONE && lang <= CODE_LANG_OBJC;
}

typedef struct {
    const gchar* only;
    gsize only_len;
    GPtrArray* out;
} Sink;

/* Control keywords that look like a call followed by a block: if (x) { */
static gboolean is_control_word(const gchar* s, gsize n) {
    static const gchar* const WORDS[] = {
        "if", "elif", "else", "for", "foreach", "while", "until", "do", "switch", "case", "catch",
        "return", "function", "with", "typeof", "await", "yield",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(WORDS); i++) {
        if (strlen(WORDS[i]) == n && memcmp(WORDS[i], s, n) == 0) return TRUE;
    }
    return FALSE;
}

static void add(Sink* sink, const gchar* line, gsize line_len, gint line_no, gsize s, gsize e,
                CodeSymbolKind kind, gboolean decl) {
    if (e <= s || is_control_word(line + s, e - s)) return;
    if (sink->only != NULL && (e - s != sink->only_len || memcmp(line + s, sink->only, e - s) != 0)) return;
    g_ptr_array_add(sink->out, code_symbol_new(line + s, e - s, kind, decl, line_no, (gint)s, line, line_len));
}

/* Makefile targets "a b: deps": each name, special and pattern targets excluded */
static void add_targets(Sink* sink, const gchar* line, gsize line_len, gint line_no, gsize s, gsize e) {
    gsize i = s;
    while (i < e) {
        while (i < e && g_ascii_isspace(line[i])) i++;
        gsize w = i;
        while (i < e && !g_ascii_isspace(line[i])) i++;
        if (i > w && line[w] != '.' && memchr(line + w, '$', i - w) == NULL &&
            memchr(line + w, '%', i - w) == NULL) {
            add(sink, line, line_len, line_no, w, i, CODE_SYM_TARGET, FALSE);
        }
    }
}

/* Leading whitespace length */
static gsize indent_of(const gchar* line, gsize len) {
    gsize i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    return i;
}

static gboolean starts_with(const gchar* s, gsize len, const gchar* prefix) {
    gsize n = strlen(prefix);
    return len >= n && memcmp(s, prefix, n) == 0;
}

/* Does the line hold the token tok (count of occurrences, for docstrings) */
static gint count_token(const gchar* line, gsize len, const gchar* tok) {
    gint n = 0;
    gsize tl = strlen(tok);
    for (gsize i = 0; i + tl <= len; i++) {
        if (memcmp(line + i, tok, tl) == 0) {
            n++;
            i += tl - 1;
        }
    }
    return n;
}

static void scan_lines(const gchar* text, gsize len, CodeLang lang, Sink* sink) {
    compile_rules();
    gboolean in_comment = FALSE;   /* block comment, Python docstring, Ruby =begin */
    const gchar* doc_quote = NULL; /* Python: """ or ''' that opened the docstring */
    BlockState block = BLOCK_NONE;
    gsize block_indent = 0;
    gint line_no = 0;
    const gchar* p = text;
    const gchar* end = text + len;
    while (p < end) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        gsize ll = nl != NULL ? (gsize)(nl - p) : (gsize)(end - p);
        const gchar* line = p;
        p = nl != NULL ? nl + 1 : end;
        gint this_line = line_no++;
        if (ll > 0 && line[ll - 1] == '\r') ll--;
        gsize ind = indent_of(line, ll);
        const gchar* t = line + ind;
        gsize tl = ll - ind;

        /* Comments */
        if (lang == CODE_LANG_PYTHON) {
            if (in_comment) {
                if (count_token(line, ll, doc_quote) % 2 == 1) in_comment = FALSE;
                continue;
            }
        } else if (lang == CODE_LANG_RUBY) {
            if (in_comment) {
                if (starts_with(line, ll, "=end")) in_comment = FALSE;
                continue;
            }
            if (starts_with(line, ll, "=begin")) {
                in_comment = TRUE;
                continue;
            }
        } else if (lang == CODE_LANG_LUA) {
            if (in_comment) {
                if (count_token(line, ll, "]]") > 0) in_comment = FALSE;
                continue;
            }
            if (starts_with(t, tl, "--[[") || starts_with(t, tl, "--[=[")) {
                in_comment = count_token(line, ll, "]]") == 0;
                continue;
            }
            if (starts_with(t, tl, "--")) continue;
        } else if (lang == CODE_LANG_JS || lang == CODE_LANG_GO || lang == CODE_LANG_RUST) {
            if (in_comment) {
                if (count_token(line, ll, "*/") > 0) in_comment = FALSE;
                continue;
            }
            if (starts_with(t, tl, "/*")) {
                in_comment = count_token(t, tl, "*/") == 0;
                continue;
            }
            if (starts_with(t, tl, "//") || starts_with(t, tl, "*")) continue;
        }
        if ((lang == CODE_LANG_PYTHON || lang == CODE_LANG_SHELL || lang == CODE_LANG_RUBY ||
             lang == CODE_LANG_MAKE) && starts_with(t, tl, "#")) {
            continue;
        }
        if (lang == CODE_LANG_MAKE && ll > 0 && line[0] == '\t') continue; /* recipe */

        /* End of Go and Rust blocks */
        if (block == BLOCK_GO_CONST || block == BLOCK_GO_VAR || block == BLOCK_GO_TYPE) {
            if (starts_with(t, tl, ")")) {
                block = BLOCK_NONE;
                continue;
            }
        } else if (block != BLOCK_NONE) {
            if (starts_with(t, tl, "}") && (lang == CODE_LANG_GO || ind <= block_indent)) {
                block = BLOCK_NONE;
                continue;
            }
        }
        if (lang == CODE_LANG_GO && block == BLOCK_NONE) {
            if (starts_with(line, ll, "const (")) block = BLOCK_GO_CONST;
            else if (starts_with(line, ll, "var (")) block = BLOCK_GO_VAR;
            else if (starts_with(line, ll, "type (")) block = BLOCK_GO_TYPE;
            if (block != BLOCK_NONE) continue;
        }

        for (gsize r = 0; r < G_N_ELEMENTS(RULES); r++) {
            const Rule* rule = &RULES[r];
            if (rule->lang != lang || rule->block != block || compiled[r] == NULL) continue;
            if ((rule->flags & RULE_TOP_ONLY) && ind > 0) continue;
            GMatchInfo* mi = NULL;
            if (!g_regex_match_full(compiled[r], line, (gssize)ll, 0, 0, &mi, NULL)) {
                g_match_info_free(mi);
                continue;
            }
            gint s = -1, e = -1;
            g_match_info_fetch_named_pos(mi, "n", &s, &e);
            g_match_info_free(mi);
            if (s < 0 || e <= s) continue;
            if (rule->flags & RULE_LAST_PART) {
                for (gint k = e - 1; k > s; k--) {
                    if (line[k] == '.' || line[k] == ':') {
                        s = k + 1;
                        break;
                    }
                }
            }
            CodeSymbolKind kind = ind > 0 ? rule->indented : rule->top;
            if (rule->flags & RULE_TARGETS) {
                add_targets(sink, line, ll, this_line, (gsize)s, (gsize)e);
            } else {
                add(sink, line, ll, this_line, (gsize)s, (gsize)e, kind, (rule->flags & RULE_DECL) != 0);
            }
            if (rule->opens != BLOCK_NONE) {
                block = rule->opens;
                block_indent = ind;
            }
            break;
        }

        /* A docstring opened on this line (after its symbol, if any) */
        if (lang == CODE_LANG_PYTHON) {
            gint dq = count_token(line, ll, "\"\"\"");
            gint sq = count_token(line, ll, "'''");
            if (dq % 2 == 1) {
                in_comment = TRUE;
                doc_quote = "\"\"\"";
            } else if (sq % 2 == 1) {
                in_comment = TRUE;
                doc_quote = "'''";
            }
        }
    }
}

GPtrArray* code_scan_text(const gchar* text, gsize len, CodeLang lang, const gchar* only_name) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_symbol_free);
    if (text == NULL || !code_scan_supported(lang)) return out;
    if (only_name != NULL && !code_text_has_word(text, len, only_name, lang)) return out;
    switch (lang) {
        case CODE_LANG_C:
        case CODE_LANG_CPP:
        case CODE_LANG_JAVA:
        case CODE_LANG_CSHARP:
            code_scan_c(text, len, lang, only_name, out);
            break;
        case CODE_LANG_OBJC:
            code_scan_c(text, len, CODE_LANG_C, only_name, out);
            break;
        default: {
            Sink sink = {only_name, only_name != NULL ? strlen(only_name) : 0, out};
            scan_lines(text, len, lang, &sink);
            break;
        }
    }
    return out;
}

/* Byte column of the first whole-word occurrence of name in line, from
 * index from; -1 when none */
static gint find_in_line(const gchar* line, gsize ll, CodeLang lang, const gchar* name, gsize nlen,
                         gsize from) {
    for (gsize i = from; i + nlen <= ll; i++) {
        if (memcmp(line + i, name, nlen) != 0) continue;
        gboolean before = i == 0 || (guchar)line[i - 1] >= 0x80 || !code_is_word_char(lang, (guchar)line[i - 1]);
        gboolean after = i + nlen == ll || (guchar)line[i + nlen] >= 0x80 ||
                         !code_is_word_char(lang, (guchar)line[i + nlen]);
        if (before && after) return (gint)i;
    }
    return -1;
}

/* First occurrence of name in lines [first, last] other than the cursor */
static gboolean first_occurrence(GPtrArray* lines, GArray* lens, gint first, gint last, CodeLang lang,
                                 const gchar* name, gint cursor_line, gint cursor_column,
                                 gint* line, gint* column) {
    gsize nlen = strlen(name);
    for (gint l = first; l <= last && l < (gint)lines->len; l++) {
        const gchar* s = g_ptr_array_index(lines, l);
        gsize ll = g_array_index(lens, gsize, l);
        gsize from = 0;
        gint col;
        while ((col = find_in_line(s, ll, lang, name, nlen, from)) >= 0) {
            gboolean at_cursor = l == cursor_line && cursor_column >= col && cursor_column <= col + (gint)nlen;
            if (!at_cursor) {
                *line = l;
                *column = col;
                return TRUE;
            }
            from = (gsize)col + nlen;
        }
    }
    return FALSE;
}

gboolean code_scan_local_definition(const gchar* text, gsize len, CodeLang lang, const gchar* name,
                                    gint cursor_line, gint cursor_column, gint* line, gint* column) {
    if (text == NULL || name == NULL || name[0] == '\0') return FALSE;
    GPtrArray* lines = g_ptr_array_new();
    GArray* lens = g_array_new(FALSE, FALSE, sizeof(gsize));
    const gchar* p = text;
    const gchar* end = text + len;
    while (p <= end) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        gsize ll = nl != NULL ? (gsize)(nl - p) : (gsize)(end - p);
        g_ptr_array_add(lines, (gpointer)p);
        g_array_append_val(lens, ll);
        if (nl == NULL) break;
        p = nl + 1;
    }
    if (cursor_line >= (gint)lines->len) cursor_line = (gint)lines->len - 1;
    /* Scope start: the nearest line at or above the cursor that starts at
     * column 0 with code (not a brace, comment or preprocessor line) */
    gint scope = 0;
    for (gint l = cursor_line; l >= 0; l--) {
        const gchar* s = g_ptr_array_index(lines, l);
        gsize ll = g_array_index(lens, gsize, l);
        if (ll == 0 || g_ascii_isspace(s[0])) continue;
        if (strchr("{}#/*)", s[0]) != NULL) continue;
        scope = l;
        break;
    }
    gboolean ok = first_occurrence(lines, lens, scope, cursor_line, lang, name, cursor_line, cursor_column,
                                   line, column) ||
                  first_occurrence(lines, lens, 0, (gint)lines->len - 1, lang, name, cursor_line,
                                   cursor_column, line, column);
    g_ptr_array_free(lines, TRUE);
    g_array_free(lens, TRUE);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Help: documentation comments, calls, parameters                           */
/* ------------------------------------------------------------------------- */

/* Lines of text (pointers into it) */
static GPtrArray* split_lines(const gchar* text, gsize len, GArray* lens) {
    GPtrArray* lines = g_ptr_array_new();
    const gchar* p = text;
    const gchar* end = text + len;
    while (p <= end) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        gsize ll = nl != NULL ? (gsize)(nl - p) : (gsize)(end - p);
        if (ll > 0 && p[ll - 1] == '\r') ll--;
        g_ptr_array_add(lines, (gpointer)p);
        g_array_append_val(lens, ll);
        if (nl == NULL) break;
        p = nl + 1;
    }
    return lines;
}

static gchar* trimmed(const gchar* s, gsize n) {
    gchar* t = g_strndup(s, n);
    return g_strstrip(t);
}

/* Remove comment markers of one line of a comment block */
static gchar* strip_marker(gchar* line, CodeLang lang) {
    const gchar* t = line;
    static const gchar* const PREFIXES[] = {"///", "//!", "//", "/**", "/*!", "/*", "*/", "*", "#", "--", ";"};
    for (gsize i = 0; i < G_N_ELEMENTS(PREFIXES); i++) {
        if (g_str_has_prefix(t, PREFIXES[i])) {
            /* '#' and '--' only in their languages */
            if (strcmp(PREFIXES[i], "#") == 0 && !(lang == CODE_LANG_PYTHON || lang == CODE_LANG_SHELL ||
                                                   lang == CODE_LANG_RUBY || lang == CODE_LANG_MAKE ||
                                                   lang == CODE_LANG_PERL || lang == CODE_LANG_R ||
                                                   lang == CODE_LANG_TCL || lang == CODE_LANG_ELIXIR ||
                                                   lang == CODE_LANG_JULIA)) {
                continue;
            }
            if (strcmp(PREFIXES[i], "--") == 0 && !(lang == CODE_LANG_LUA || lang == CODE_LANG_HASKELL)) continue;
            if (strcmp(PREFIXES[i], ";") == 0) continue;
            t += strlen(PREFIXES[i]);
            break;
        }
    }
    gchar* out = g_strdup(t);
    gsize n = strlen(out);
    if (n >= 2 && strcmp(out + n - 2, "*/") == 0) out[n - 2] = '\0';
    g_free(line);
    return g_strstrip(out);
}

static gboolean is_comment_line(const gchar* t, CodeLang lang) {
    if (g_str_has_prefix(t, "//") || g_str_has_prefix(t, "/*") || g_str_has_prefix(t, "*") ||
        g_str_has_suffix(t, "*/")) {
        return lang != CODE_LANG_PYTHON && lang != CODE_LANG_SHELL && lang != CODE_LANG_MAKE;
    }
    if (g_str_has_prefix(t, "#") && !g_str_has_prefix(t, "#include") && !g_str_has_prefix(t, "#define") &&
        !g_str_has_prefix(t, "#if") && !g_str_has_prefix(t, "#!")) {
        return lang == CODE_LANG_PYTHON || lang == CODE_LANG_SHELL || lang == CODE_LANG_RUBY ||
               lang == CODE_LANG_MAKE || lang == CODE_LANG_PERL || lang == CODE_LANG_R || lang == CODE_LANG_TCL ||
               lang == CODE_LANG_ELIXIR || lang == CODE_LANG_JULIA;
    }
    if (g_str_has_prefix(t, "--")) return lang == CODE_LANG_LUA || lang == CODE_LANG_HASKELL;
    return FALSE;
}

gchar* code_scan_doc_comment(const gchar* text, gsize len, CodeLang lang, gint line) {
    if (text == NULL || line < 0) return NULL;
    GArray* lens = g_array_new(FALSE, FALSE, sizeof(gsize));
    GPtrArray* lines = split_lines(text, len, lens);
    gchar* result = NULL;
    if (line < (gint)lines->len) {
        GPtrArray* parts = g_ptr_array_new_with_free_func(g_free);
        if (lang == CODE_LANG_PYTHON) {
            /* Docstring: first statement of the body, after the def / class line(s) */
            gint l = line + 1;
            while (l < (gint)lines->len) {
                gchar* t = trimmed(g_ptr_array_index(lines, l), g_array_index(lens, gsize, l));
                gboolean blank = t[0] == '\0';
                g_free(t);
                if (!blank) break;
                l++;
            }
            gchar* first = l < (gint)lines->len ? trimmed(g_ptr_array_index(lines, l), g_array_index(lens, gsize, l))
                                               : g_strdup("");
            const gchar* quote = g_str_has_prefix(first, "\"\"\"") ? "\"\"\"" : g_str_has_prefix(first, "'''") ? "'''" : NULL;
            if (quote != NULL) {
                gchar* rest = g_strdup(first + 3);
                for (;;) {
                    gchar* close = strstr(rest, quote);
                    if (close != NULL) *close = '\0';
                    g_ptr_array_add(parts, g_strdup(g_strstrip(rest)));
                    g_free(rest);
                    if (close != NULL || ++l >= (gint)lines->len) break;
                    rest = g_strndup(g_ptr_array_index(lines, l), g_array_index(lens, gsize, l));
                }
            }
            g_free(first);
        }
        if (parts->len == 0) {
            /* Comment block right above (attributes / decorators skipped) */
            gint l = line - 1;
            while (l >= 0) {
                gchar* t = trimmed(g_ptr_array_index(lines, l), g_array_index(lens, gsize, l));
                gboolean decorator = (t[0] == '@' || g_str_has_prefix(t, "#[")) && lang != CODE_LANG_SHELL;
                g_free(t);
                if (!decorator) break;
                l--;
            }
            gint last = l;
            while (l >= 0) {
                gchar* t = trimmed(g_ptr_array_index(lines, l), g_array_index(lens, gsize, l));
                gboolean comment = t[0] != '\0' && is_comment_line(t, lang);
                g_free(t);
                if (!comment) break;
                l--;
            }
            for (gint i = l + 1; i <= last; i++) {
                gchar* t = trimmed(g_ptr_array_index(lines, i), g_array_index(lens, gsize, i));
                g_ptr_array_add(parts, strip_marker(t, lang));
            }
        }
        /* Drop empty lines at both ends, join the rest */
        while (parts->len > 0 && ((gchar*)g_ptr_array_index(parts, 0))[0] == '\0') g_ptr_array_remove_index(parts, 0);
        while (parts->len > 0 && ((gchar*)g_ptr_array_index(parts, parts->len - 1))[0] == '\0') {
            g_ptr_array_remove_index(parts, parts->len - 1);
        }
        if (parts->len > 0) {
            g_ptr_array_add(parts, NULL);
            result = g_strjoinv("\n", (gchar**)parts->pdata);
            g_ptr_array_remove_index(parts, parts->len - 1);
        }
        g_ptr_array_unref(parts);
    }
    g_ptr_array_free(lines, TRUE);
    g_array_free(lens, TRUE);
    return result;
}

gboolean code_scan_call_at(const gchar* line, gssize len, gint index, CodeLang lang, gint* name_start,
                           gint* name_end, gint* argument) {
    if (line == NULL) return FALSE;
    if (len < 0) len = (gssize)strlen(line);
    if (index > len) index = (gint)len;
    gboolean in_string = FALSE;
    gchar quote = 0;
    /* Strings before the cursor: scan forward to know which quotes are open */
    GArray* opens = g_array_new(FALSE, FALSE, sizeof(gint));   /* positions of unclosed '(' */
    GArray* comma_at = g_array_new(FALSE, FALSE, sizeof(gint)); /* commas per open level */
    for (gint i = 0; i < index; i++) {
        gchar c = line[i];
        if (in_string) {
            if (c == '\\') i++;
            else if (c == quote) in_string = FALSE;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            in_string = TRUE;
            quote = c;
        } else if (c == '(' || c == '[' || c == '{') {
            g_array_append_val(opens, i);
            gint zero = 0;
            g_array_append_val(comma_at, zero);
        } else if ((c == ')' || c == ']' || c == '}') && opens->len > 0) {
            g_array_set_size(opens, opens->len - 1);
            g_array_set_size(comma_at, comma_at->len - 1);
        } else if (c == ',' && comma_at->len > 0) {
            g_array_index(comma_at, gint, comma_at->len - 1)++;
        }
    }
    gboolean ok = FALSE;
    /* The innermost '(' (brackets and braces are not calls) */
    for (gint k = (gint)opens->len - 1; k >= 0 && !ok; k--) {
        gint open = g_array_index(opens, gint, k);
        if (line[open] != '(') {
            continue;
        }
        gint e = open;
        while (e > 0 && (line[e - 1] == ' ' || line[e - 1] == '\t')) e--;
        gint s = e;
        while (s > 0 && ((guchar)line[s - 1] >= 0x80 || code_is_word_char(lang, (guchar)line[s - 1]))) s--;
        if (s < e && !g_ascii_isdigit(line[s])) {
            *name_start = s;
            *name_end = e;
            *argument = g_array_index(comma_at, gint, k);
            ok = TRUE;
        }
        break;
    }
    g_array_free(opens, TRUE);
    g_array_free(comma_at, TRUE);
    return ok;
}

gboolean code_scan_parameter_range(const gchar* signature, gint arg, gint* start, gint* end) {
    if (signature == NULL || arg < 0) return FALSE;
    const gchar* open = strchr(signature, '(');
    if (open == NULL) return FALSE;
    gint depth = 0, current = 0;
    gint s = (gint)(open - signature) + 1;
    for (gint i = s; signature[i] != '\0'; i++) {
        gchar c = signature[i];
        if (c == '(' || c == '[' || c == '{' || c == '<') {
            depth++;
        } else if ((c == ')' || c == ']' || c == '}' || c == '>') && depth > 0) {
            depth--;
        } else if ((c == ',' || c == ')') && depth == 0) {
            if (current == arg) {
                gint a = s, b = i;
                while (a < b && signature[a] == ' ') a++;
                while (b > a && signature[b - 1] == ' ') b--;
                /* "f()" and C's "f(void)" have no parameter */
                if (a == b || (c == ')' && current == 0 && b - a == 4 && strncmp(signature + a, "void", 4) == 0)) {
                    return FALSE;
                }
                *start = a;
                *end = b;
                return TRUE;
            }
            if (c == ')') return FALSE;
            current++;
            s = i + 1;
        }
    }
    return FALSE;
}
