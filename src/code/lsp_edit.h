#ifndef LSP_EDIT_H
#define LSP_EDIT_H

/* Pure text-edit engine for LSP (textDocument/rename, completion textEdit)
 * and buffer-local identifier extraction for fallback completion without a
 * language server. Pure GLib (Layer 1), unit-tested. */
#include <glib.h>

#include "code/code_symbol.h"
#include "code/lsp_protocol.h"

/* Apply edits (GPtrArray* of LspTextEdit*) to text. Positions are interpreted
 * as UTF-8 bytes when utf8_positions is TRUE, else UTF-16 code units.
 * Returns a newly allocated string on success, or NULL with error set on
 * invalid / overlapping edits. Caller frees. */
gchar* lsp_edit_apply(const gchar* text, GPtrArray* edits, gboolean utf8_positions, GError** error);

/* Extract up to limit unique identifiers from text matching prefix
 * (case-insensitive substring, excluding an exact match with prefix itself),
 * ranked: exact-case prefix, case-insensitive prefix, substring, then
 * alphabetical. Returns a GPtrArray* of gchar* (caller unrefs). */
GPtrArray* lsp_edit_buffer_identifiers(const gchar* text, gsize len, CodeLang lang,
                                       const gchar* prefix, guint limit);

#endif /* LSP_EDIT_H */
