#ifndef CODE_SCAN_H
#define CODE_SCAN_H

/* Symbol scanners of the built-in code index (TASK_CODE_NAV.md): the C
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

#endif /* CODE_SCAN_H */
