/* Languages, identifiers and symbols for code navigation */
#include "code_symbol.h"
#include <string.h>

typedef struct {
    const gchar* ext;
    CodeLang lang;
} ExtLang;

static const ExtLang EXTENSIONS[] = {
    {"c", CODE_LANG_C}, {"h", CODE_LANG_C},
    {"cc", CODE_LANG_CPP}, {"cpp", CODE_LANG_CPP}, {"cxx", CODE_LANG_CPP}, {"c++", CODE_LANG_CPP},
    {"hh", CODE_LANG_CPP}, {"hpp", CODE_LANG_CPP}, {"hxx", CODE_LANG_CPP}, {"h++", CODE_LANG_CPP},
    {"ipp", CODE_LANG_CPP}, {"tpp", CODE_LANG_CPP}, {"inl", CODE_LANG_CPP}, {"ino", CODE_LANG_CPP},
    {"java", CODE_LANG_JAVA},
    {"cs", CODE_LANG_CSHARP},
    {"py", CODE_LANG_PYTHON}, {"pyw", CODE_LANG_PYTHON}, {"pyi", CODE_LANG_PYTHON},
    {"sh", CODE_LANG_SHELL}, {"bash", CODE_LANG_SHELL}, {"zsh", CODE_LANG_SHELL},
    {"ksh", CODE_LANG_SHELL}, {"dash", CODE_LANG_SHELL},
    {"js", CODE_LANG_JS}, {"mjs", CODE_LANG_JS}, {"cjs", CODE_LANG_JS}, {"jsx", CODE_LANG_JS},
    {"ts", CODE_LANG_JS}, {"tsx", CODE_LANG_JS}, {"mts", CODE_LANG_JS}, {"cts", CODE_LANG_JS},
    {"go", CODE_LANG_GO},
    {"rs", CODE_LANG_RUST},
    {"lua", CODE_LANG_LUA},
    {"rb", CODE_LANG_RUBY}, {"rake", CODE_LANG_RUBY}, {"gemspec", CODE_LANG_RUBY},
    {"mk", CODE_LANG_MAKE}, {"mak", CODE_LANG_MAKE},
};

static const ExtLang NAMES[] = {
    {"Makefile", CODE_LANG_MAKE}, {"makefile", CODE_LANG_MAKE}, {"GNUmakefile", CODE_LANG_MAKE},
    {"Rakefile", CODE_LANG_RUBY}, {"Gemfile", CODE_LANG_RUBY},
    {".bashrc", CODE_LANG_SHELL}, {".bash_profile", CODE_LANG_SHELL}, {".profile", CODE_LANG_SHELL},
    {".zshrc", CODE_LANG_SHELL}, {".kshrc", CODE_LANG_SHELL},
};

CodeLang code_lang_from_name(const gchar* name) {
    if (name == NULL) return CODE_LANG_NONE;
    const gchar* base = strrchr(name, '/');
    base = base != NULL ? base + 1 : name;
    for (gsize i = 0; i < G_N_ELEMENTS(NAMES); i++) {
        if (strcmp(base, NAMES[i].ext) == 0) return NAMES[i].lang;
    }
    const gchar* dot = strrchr(base, '.');
    if (dot == NULL || dot == base) return CODE_LANG_NONE;
    gchar* ext = g_ascii_strdown(dot + 1, -1);
    CodeLang lang = CODE_LANG_NONE;
    for (gsize i = 0; i < G_N_ELEMENTS(EXTENSIONS); i++) {
        if (strcmp(ext, EXTENSIONS[i].ext) == 0) {
            lang = EXTENSIONS[i].lang;
            break;
        }
    }
    /* .C and .H (upper case) are C++ by convention */
    if (lang == CODE_LANG_C && (dot[1] == 'C' || dot[1] == 'H')) lang = CODE_LANG_CPP;
    g_free(ext);
    return lang;
}

CodeLang code_lang_detect(const gchar* name, const gchar* text, gssize len) {
    CodeLang lang = code_lang_from_name(name);
    if (lang != CODE_LANG_NONE || text == NULL) return lang;
    if (len < 0) len = (gssize)strlen(text);
    if (len < 3 || text[0] != '#' || text[1] != '!') return CODE_LANG_NONE;
    const gchar* nl = memchr(text, '\n', (gsize)len);
    gchar* first = g_strndup(text + 2, nl != NULL ? (gsize)(nl - text - 2) : (gsize)len - 2);
    gchar** words = g_strsplit_set(g_strstrip(first), " \t", -1);
    const gchar* interp = NULL;
    for (gint i = 0; words[i] != NULL; i++) {
        if (words[i][0] == '\0' || words[i][0] == '-') continue;
        const gchar* b = strrchr(words[i], '/');
        b = b != NULL ? b + 1 : words[i];
        if (strcmp(b, "env") == 0) continue;
        interp = b;
        break;
    }
    lang = CODE_LANG_NONE;
    if (interp != NULL) {
        if (strcmp(interp, "sh") == 0 || strcmp(interp, "bash") == 0 || strcmp(interp, "zsh") == 0 ||
            strcmp(interp, "ksh") == 0 || strcmp(interp, "dash") == 0) {
            lang = CODE_LANG_SHELL;
        } else if (g_str_has_prefix(interp, "python")) {
            lang = CODE_LANG_PYTHON;
        } else if (strcmp(interp, "node") == 0 || strcmp(interp, "deno") == 0 || strcmp(interp, "bun") == 0) {
            lang = CODE_LANG_JS;
        } else if (g_str_has_prefix(interp, "lua")) {
            lang = CODE_LANG_LUA;
        } else if (strcmp(interp, "ruby") == 0) {
            lang = CODE_LANG_RUBY;
        } else if (strcmp(interp, "make") == 0) {
            lang = CODE_LANG_MAKE;
        }
    }
    g_strfreev(words);
    g_free(first);
    return lang;
}

CodeLang code_lang_family(CodeLang lang) {
    return lang == CODE_LANG_CPP ? CODE_LANG_C : lang;
}

const gchar* code_lang_name(CodeLang lang) {
    switch (lang) {
        case CODE_LANG_C: return "C";
        case CODE_LANG_CPP: return "C++";
        case CODE_LANG_JAVA: return "Java";
        case CODE_LANG_CSHARP: return "C#";
        case CODE_LANG_PYTHON: return "Python";
        case CODE_LANG_SHELL: return "Shell";
        case CODE_LANG_JS: return "JavaScript";
        case CODE_LANG_GO: return "Go";
        case CODE_LANG_RUST: return "Rust";
        case CODE_LANG_LUA: return "Lua";
        case CODE_LANG_RUBY: return "Ruby";
        case CODE_LANG_MAKE: return "Makefile";
        case CODE_LANG_NONE: break;
    }
    return "text";
}

const gchar* code_symbol_kind_name(CodeSymbolKind kind) {
    switch (kind) {
        case CODE_SYM_FUNCTION: return "function";
        case CODE_SYM_METHOD: return "method";
        case CODE_SYM_MACRO: return "macro";
        case CODE_SYM_TYPE: return "type";
        case CODE_SYM_CLASS: return "class";
        case CODE_SYM_VARIABLE: return "variable";
        case CODE_SYM_CONSTANT: return "constant";
        case CODE_SYM_FIELD: return "field";
        case CODE_SYM_MODULE: return "module";
        case CODE_SYM_TARGET: return "target";
    }
    return "symbol";
}

gboolean code_is_word_char(CodeLang lang, gunichar c) {
    if (c == '_' || g_unichar_isalnum(c)) return TRUE;
    if (c == '$') return lang == CODE_LANG_JS;
    if (c == '-') return lang == CODE_LANG_MAKE;
    return FALSE;
}

static gboolean ascii_word_byte(CodeLang lang, guchar b) {
    return b < 0x80 && code_is_word_char(lang, b);
}

gboolean code_word_at(const gchar* line, gssize len, gint index, CodeLang lang,
                      gint* start, gint* end) {
    if (line == NULL) return FALSE;
    if (len < 0) len = (gssize)strlen(line);
    if (index < 0 || index > len) return FALSE;
    const gchar* lim = line + len;
    const gchar* at = line + index;
    /* On a word character, or right after one */
    gboolean on = at < lim && code_is_word_char(lang, g_utf8_get_char_validated(at, lim - at));
    if (!on) {
        if (at == line) return FALSE;
        const gchar* prev = g_utf8_find_prev_char(line, at);
        if (prev == NULL || !code_is_word_char(lang, g_utf8_get_char_validated(prev, at - prev))) return FALSE;
        at = prev;
    }
    const gchar* s = at;
    while (s > line) {
        const gchar* prev = g_utf8_find_prev_char(line, s);
        if (prev == NULL || !code_is_word_char(lang, g_utf8_get_char_validated(prev, s - prev))) break;
        s = prev;
    }
    const gchar* e = at;
    while (e < lim) {
        gunichar c = g_utf8_get_char_validated(e, lim - e);
        if (c == (gunichar)-1 || c == (gunichar)-2 || !code_is_word_char(lang, c)) break;
        e = g_utf8_next_char(e);
    }
    /* Ruby predicates and bang methods: empty? save! */
    if (lang == CODE_LANG_RUBY && e < lim && (*e == '?' || *e == '!') &&
        (e + 1 >= lim || *(e + 1) != '=')) {
        e++;
    }
    /* Makefile words do not start or end with '-' */
    if (lang == CODE_LANG_MAKE) {
        while (s < e && *s == '-') s++;
        while (e > s && *(e - 1) == '-') e--;
    }
    if (s >= e || g_ascii_isdigit(*s)) return FALSE;
    if (start != NULL) *start = (gint)(s - line);
    if (end != NULL) *end = (gint)(e - line);
    return TRUE;
}

gboolean code_text_has_word(const gchar* text, gsize len, const gchar* word, CodeLang lang) {
    gsize wlen = word != NULL ? strlen(word) : 0;
    if (text == NULL || wlen == 0 || wlen > len) return FALSE;
    const gchar* p = text;
    const gchar* lim = text + len - wlen;
    while (p <= lim) {
        const gchar* hit = memchr(p, word[0], (gsize)(lim - p) + 1);
        if (hit == NULL) return FALSE;
        if (memcmp(hit, word, wlen) == 0 &&
            (hit == text || !ascii_word_byte(lang, (guchar)hit[-1])) &&
            (hit + wlen == text + len || !ascii_word_byte(lang, (guchar)hit[wlen]))) {
            return TRUE;
        }
        p = hit + 1;
    }
    return FALSE;
}

CodeSymbol* code_symbol_new(const gchar* name, gsize name_len, CodeSymbolKind kind,
                            gboolean declaration, gint line, gint column,
                            const gchar* line_text, gsize line_len) {
    CodeSymbol* sym = g_new0(CodeSymbol, 1);
    sym->name = g_strndup(name, name_len);
    sym->kind = kind;
    sym->declaration = declaration;
    sym->line = line;
    sym->column = column;
    gchar* raw = line_text != NULL ? g_strndup(line_text, line_len) : g_strdup("");
    if (!g_utf8_validate(raw, -1, NULL)) {
        gchar* valid = g_utf8_make_valid(raw, -1);
        g_free(raw);
        raw = valid;
    }
    /* Tabs and trailing CR would show as odd characters in a one-line message */
    for (gchar* c = raw; *c != '\0'; c++) {
        if (*c == '\t' || *c == '\r') *c = ' ';
    }
    sym->text = g_strdup(g_strstrip(raw));
    g_free(raw);
    return sym;
}

CodeSymbol* code_symbol_copy(const CodeSymbol* sym) {
    if (sym == NULL) return NULL;
    CodeSymbol* c = g_new0(CodeSymbol, 1);
    *c = *sym;
    c->name = g_strdup(sym->name);
    c->text = g_strdup(sym->text);
    return c;
}

void code_symbol_free(CodeSymbol* sym) {
    if (sym == NULL) return;
    g_free(sym->name);
    g_free(sym->text);
    g_free(sym);
}
