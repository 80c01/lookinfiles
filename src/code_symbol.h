#ifndef CODE_SYMBOL_H
#define CODE_SYMBOL_H

/* Source code symbols for code navigation (TASK_CODE_NAV.md): languages
 * and their families, the identifier under the cursor, and CodeSymbol,
 * one definition or declaration found by a scanner (code_scan). Pure GLib,
 * unit-tested. */
#include <glib.h>

typedef enum {
    CODE_LANG_NONE = 0,
    CODE_LANG_C,
    CODE_LANG_CPP,
    CODE_LANG_JAVA,
    CODE_LANG_CSHARP,
    CODE_LANG_PYTHON,
    CODE_LANG_SHELL,
    CODE_LANG_JS,       /* JavaScript and TypeScript */
    CODE_LANG_GO,
    CODE_LANG_RUST,
    CODE_LANG_LUA,
    CODE_LANG_RUBY,
    CODE_LANG_MAKE,
    /* Read by ctags or a language server only (no built-in scanner) */
    CODE_LANG_OBJC,     /* Objective-C: shares the C family (headers) */
    CODE_LANG_PHP,
    CODE_LANG_PERL,
    CODE_LANG_KOTLIN,
    CODE_LANG_SWIFT,
    CODE_LANG_SCALA,
    CODE_LANG_DART,
    CODE_LANG_HASKELL,
    CODE_LANG_ELIXIR,
    CODE_LANG_ERLANG,
    CODE_LANG_JULIA,
    CODE_LANG_R,
    CODE_LANG_TCL
} CodeLang;

typedef enum {
    CODE_SYM_FUNCTION = 0,
    CODE_SYM_METHOD,
    CODE_SYM_MACRO,
    CODE_SYM_TYPE,      /* typedef, struct, union, enum, interface, type alias */
    CODE_SYM_CLASS,
    CODE_SYM_VARIABLE,
    CODE_SYM_CONSTANT,  /* enum constant, const */
    CODE_SYM_FIELD,     /* struct / class member */
    CODE_SYM_MODULE,    /* namespace, module, package */
    CODE_SYM_TARGET     /* Makefile target */
} CodeSymbolKind;

typedef struct {
    gchar* name;
    CodeSymbolKind kind;
    gboolean declaration; /* prototype, extern variable, forward declaration */
    gint line;            /* 0-based */
    gint column;          /* byte index of the name in its line */
    gchar* text;          /* the source line, trimmed, valid UTF-8 */
    gchar* signature;     /* parameters / type when the backend knows them (ctags, LSP), or NULL */
} CodeSymbol;

/* Language of a file name or path (extension, well-known names such as
 * Makefile); CODE_LANG_NONE when unknown */
CodeLang code_lang_from_name(const gchar* name);
/* Same, then the #! line of text when the name says nothing */
CodeLang code_lang_detect(const gchar* name, const gchar* text, gssize len);
/* Languages whose files see each other's symbols: C and C++ share
 * CODE_LANG_C (headers are common), the others are their own family */
CodeLang code_lang_family(CodeLang lang);
/* Short name for messages ("C", "Python"...) */
const gchar* code_lang_name(CodeLang lang);

/* "function", "macro", "field"... */
const gchar* code_symbol_kind_name(CodeSymbolKind kind);

/* TRUE when c can be part of an identifier in lang */
gboolean code_is_word_char(CodeLang lang, gunichar c);
/* The identifier at byte index in line (or ending just before it, when the
 * cursor is right after a word): *start and *end are byte indexes, end
 * excluded. FALSE when there is none or it is a number. */
gboolean code_word_at(const gchar* line, gssize len, gint index, CodeLang lang,
                      gint* start, gint* end);
/* TRUE when text contains word as a whole identifier */
gboolean code_text_has_word(const gchar* text, gsize len, const gchar* word, CodeLang lang);

CodeSymbol* code_symbol_new(const gchar* name, gsize name_len, CodeSymbolKind kind,
                            gboolean declaration, gint line, gint column,
                            const gchar* line_text, gsize line_len);
CodeSymbol* code_symbol_copy(const CodeSymbol* sym);
void code_symbol_free(CodeSymbol* sym);

#endif /* CODE_SYMBOL_H */
