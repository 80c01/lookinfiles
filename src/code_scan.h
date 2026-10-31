#ifndef CODE_SCAN_H
#define CODE_SCAN_H

/* Symbol scanners of the built-in code index (docs/code-nav.md): the C
 * family through code_scan_c, the other languages with line-based regular
 * expressions (Python, Shell, JavaScript / TypeScript, Go, Rust, Lua, Ruby,
 * Makefile). Heuristic: no parser, no scopes, local variables are not
 * listed. Pure GLib, unit-tested; safe to call from worker threads. */
#include "code_symbol.h"

/* TRUE when lang has a built-in scanner (the others need ctags or LSP) */
gboolean code_scan_supported(CodeLang lang);

/* Symbols of text (CodeSymbol*, freed with the array), in file order: all
 * of them, or only those named only_name when it is not NULL */
GPtrArray* code_scan_text(const gchar* text, gsize len, CodeLang lang, const gchar* only_name);

/* Line (0-based) and byte column of the first whole-word occurrence of
 * name in the "scope" of cursor_line: from the nearest line above it that
 * starts at column 0 (a function head, roughly) down to the cursor line,
 * else the first occurrence in the text, never the occurrence at the
 * cursor itself (cursor_column: byte index of the word under the cursor).
 * The local fallback of a definition lookup (Vim's gd). FALSE when there is
 * no other occurrence. */
gboolean code_scan_local_definition(const gchar* text, gsize len, CodeLang lang, const gchar* name,
                                    gint cursor_line, gint cursor_column, gint* line, gint* column);

/* Documentation comment of the symbol at line (0-based): the comment
 * block right above it (line or block comments, # and -- in their
 * languages), or a Python docstring right
 * below it, without the comment markers. NULL when there is none. */
gchar* code_scan_doc_comment(const gchar* text, gsize len, CodeLang lang, gint line);

/* The call the cursor (byte index in line) is in: the identifier before
 * the innermost unclosed '(' (byte range *name_start, *name_end) and the
 * index of the argument under the cursor (commas at depth 0). FALSE when
 * the cursor is not inside a call's parentheses on this line. */
gboolean code_scan_call_at(const gchar* line, gssize len, gint index, CodeLang lang, gint* name_start,
                           gint* name_end, gint* argument);

/* Byte range of parameter number arg in a signature "type name(a, b)":
 * FALSE when it has no such parameter */
gboolean code_scan_parameter_range(const gchar* signature, gint arg, gint* start, gint* end);

#endif /* CODE_SCAN_H */
