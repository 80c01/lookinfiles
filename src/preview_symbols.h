#ifndef PREVIEW_SYMBOLS_H
#define PREVIEW_SYMBOLS_H

/* Symbols of a source code preview for its tagbar (docs/previews.md): the
 * built-in scanner (code_scan.h) runs on the text shown in a worker, and
 * the symbols go to preview_tagbar_set_outline() while the preview is
 * current. No language server: previews change too often to start one. */
#include "app_types.h"

/* Scan text (the text of the preview, valid UTF-8, copied) of the file
 * name; first_line: line of the buffer where text starts (after a
 * truncation header). Does nothing when the language has no scanner. */
void preview_symbols_start(AppData* app, const gchar* name, const gchar* text, gint first_line);

#endif /* PREVIEW_SYMBOLS_H */
