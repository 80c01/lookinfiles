#ifndef CODE_OUTLINE_H
#define CODE_OUTLINE_H

/* The outline of an editor tab (docs/code-nav.md), shared by the tagbar
 * (editor_tagbar.h) and the symbol picker (code_symbols.h): the language
 * server's document symbols, else the built-in scanner on the buffer, or
 * the headings of a Markdown file. Scans run in a worker thread on a copy
 * of the text. */
#include "core/app_types.h"
#include "code_outline_tree.h"

/* entries: CodeOutlineEntry* sorted by line (empty when the file has none),
 * owned by the caller of code_outline_collect() only during the call;
 * source: "clangd", "built-in scanner", "Markdown headings" */
typedef void (*CodeOutlineFunc)(AppData* app, EditorTab* tab, GPtrArray* entries, const gchar* source,
                                gpointer data);

/* TRUE when tab has an outline: source code of a known language, or Markdown */
gboolean code_outline_supported(EditorTab* tab);

/* Collect the outline of tab and call func on the UI thread, unless tab was
 * closed or cancellable (may be NULL) cancelled meanwhile. A server that is
 * slow to answer is not waited for: the built-in scanner answers. destroy (may be
 * NULL) frees data once the request is over, except for a server request
 * dropped with its closed tab (code_intel.h): pass data that needs no
 * freeing, guarded by cancellable, when that matters. */
void code_outline_collect(AppData* app, EditorTab* tab, GCancellable* cancellable, CodeOutlineFunc func,
                          gpointer data, GDestroyNotify destroy);

#endif /* CODE_OUTLINE_H */
