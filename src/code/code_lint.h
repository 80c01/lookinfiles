#ifndef CODE_LINT_H
#define CODE_LINT_H

/* Built-in syntax diagnostics for structured formats (JSON, TOML, YAML, XML)
 * using LookinFiles's own Layer 1 parsers, with zero external processes.
 * Pure GLib (Layer 1), unit-tested, safe on worker threads. */
#include <glib.h>

#include "code/lsp_protocol.h"

#define CODE_LINT_MAX_BYTES (512 * 1024)

/* Name of the built-in checker for path ("json", "toml", "yaml", "xml"),
 * or NULL when path has no built-in syntax checker. */
const gchar* code_lint_source_name(const gchar* path);

/* Check text (len bytes) for syntax errors using the built-in parser for
 * path. Returns a GPtrArray* of LspDiagnostic* (0-based line and byte column),
 * empty when valid, too large, empty/whitespace, or unsupported. Caller unrefs. */
GPtrArray* code_lint_check(const gchar* path, const gchar* text, gsize len);

#endif /* CODE_LINT_H */
