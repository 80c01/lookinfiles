#ifndef CODE_INTEL_H
#define CODE_INTEL_H

/* Language servers for code navigation (docs/code-nav.md): one
 * LspClient per project and server command, started on the first request
 * from a tab of its language (lsp_config), fed the text of the open tabs
 * before each request (full text), and closed documents dropped. Results
 * come back with byte columns, on the UI thread, only while the tab of the
 * request is still open.
 *
 * Every function returning gboolean returns FALSE when no server serves
 * the tab (not installed, failed, or the capability is missing): the
 * caller then uses the built-in index. Once TRUE, the callback is always
 * called, with NULL results when the server did not answer. */
#include "app_types.h"
#include "lsp_protocol.h"

/* A location with a byte column (LspLocation, column converted) */
typedef void (*CodeIntelLocationsFunc)(AppData* app, EditorTab* tab, GPtrArray* locations, gpointer data);
typedef void (*CodeIntelTextFunc)(AppData* app, EditorTab* tab, const gchar* markdown, gpointer data);
typedef void (*CodeIntelSignatureFunc)(AppData* app, EditorTab* tab, const LspSignature* sig, gpointer data);
/* LspSymbol with byte columns */
typedef void (*CodeIntelSymbolsFunc)(AppData* app, EditorTab* tab, GPtrArray* symbols, gpointer data);

/* Definition / declaration / references of the position (line, byte
 * index) in tab */
gboolean code_intel_definition(AppData* app, EditorTab* tab, gint line, gint index, gboolean declaration,
                               CodeIntelLocationsFunc func, gpointer data);
gboolean code_intel_references(AppData* app, EditorTab* tab, gint line, gint index, CodeIntelLocationsFunc func,
                               gpointer data);
gboolean code_intel_hover(AppData* app, EditorTab* tab, gint line, gint index, CodeIntelTextFunc func,
                          gpointer data);
gboolean code_intel_signature(AppData* app, EditorTab* tab, gint line, gint index, CodeIntelSignatureFunc func,
                              gpointer data);
gboolean code_intel_document_symbols(AppData* app, EditorTab* tab, CodeIntelSymbolsFunc func, gpointer data);

/* Follow a new editor tab: its server starts and gets the text (so its
 * diagnostics come), edits are sent after a short pause, and diagnostics
 * are underlined (errors red, warnings yellow, others blue) */
void code_intel_attach(AppData* app, EditorTab* tab);

/* Diagnostics at the position (line, byte index) of tab as Markdown, or
 * NULL when there is none */
gchar* code_intel_diagnostics_at(AppData* app, EditorTab* tab, gint line, gint index);

/* Characters that trigger signature help in tab ("(," by default) */
const gchar* code_intel_signature_triggers(AppData* app, EditorTab* tab);

/* Name of the server serving tab ("clangd"), "clangd (starting)", or NULL
 * when none; starts nothing */
gchar* code_intel_server_label(AppData* app, EditorTab* tab);

/* Name of the server serving tab ("clangd" or "clangd (starting)") with,
 * once it is ready, the errors and warnings it reports for tab; NULL (and
 * no diagnostics) when none; starts nothing */
gchar* code_intel_server_status(AppData* app, EditorTab* tab, guint* errors, guint* warnings);

/* Stop every server (application exit) */
void code_intel_shutdown(AppData* app);

#endif /* CODE_INTEL_H */
