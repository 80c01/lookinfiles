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
#include "core/app_types.h"
#include "code/lsp_protocol.h"

/* A location with a byte column (LspLocation, column converted) */
typedef void (*CodeIntelLocationsFunc)(AppData* app, EditorTab* tab, GPtrArray* locations, gpointer data);
typedef void (*CodeIntelTextFunc)(AppData* app, EditorTab* tab, const gchar* markdown, gpointer data);
typedef void (*CodeIntelSignatureFunc)(AppData* app, EditorTab* tab, const LspSignature* sig, gpointer data);
/* LspSymbol with byte columns */
typedef void (*CodeIntelSymbolsFunc)(AppData* app, EditorTab* tab, GPtrArray* symbols, gpointer data);
typedef void (*CodeIntelCompletionFunc)(AppData* app, EditorTab* tab, const LspCompletionList* list, gpointer data);
typedef void (*CodeIntelPrepareRenameFunc)(AppData* app, EditorTab* tab, gboolean ok, const gchar* placeholder,
                                           gint start_line, gint start_col, gint end_line, gint end_col, gpointer data);
typedef void (*CodeIntelWorkspaceEditFunc)(AppData* app, EditorTab* tab, LspWorkspaceEdit* edit, gpointer data);

/* One diagnostic entry in a snapshot of all known diagnostics */
typedef struct {
    gchar* path;
    LspDiagnostic diag;
} CodeDiagEntry;

void code_diag_entry_free(CodeDiagEntry* entry);

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
/* Workspace symbols / completion: return > 0 request id (cancelable with
 * code_intel_cancel()), or 0 when no server supports the capability */
gint64 code_intel_workspace_symbols(AppData* app, EditorTab* tab, const gchar* query, CodeIntelSymbolsFunc func,
                                    gpointer data);
gint64 code_intel_completion(AppData* app, EditorTab* tab, gint line, gint index, const gchar* trigger_char,
                             CodeIntelCompletionFunc func, gpointer data);
void code_intel_cancel(AppData* app, EditorTab* tab, gint64 req_id);

/* Rename support */
gboolean code_intel_supports_rename(AppData* app, EditorTab* tab);
gboolean code_intel_utf8_positions(AppData* app, EditorTab* tab);
gboolean code_intel_prepare_rename(AppData* app, EditorTab* tab, gint line, gint index,
                                   CodeIntelPrepareRenameFunc func, gpointer data);
gboolean code_intel_rename(AppData* app, EditorTab* tab, gint line, gint index, const gchar* new_name,
                           CodeIntelWorkspaceEditFunc func, gpointer data);

/* Follow a new editor tab: its server starts and gets the text (so its
 * diagnostics come), edits are sent after a short pause, and diagnostics
 * are underlined (errors red, warnings yellow, others blue) */
void code_intel_attach(AppData* app, EditorTab* tab);

/* Diagnostics at the position (line, byte index) of tab as Markdown, or
 * NULL when there is none */
gchar* code_intel_diagnostics_at(AppData* app, EditorTab* tab, gint line, gint index);
/* Snapshot of all diagnostics across all files, sorted by path/line/col
 * (GPtrArray* of CodeDiagEntry*, caller unrefs) */
GPtrArray* code_intel_all_diagnostics(AppData* app);
/* Next (forward) or previous diagnostic in tab, wrapping around. Returns FALSE
 * when tab has no diagnostics. *out_diag (optional) is a copy; caller frees
 * with lsp_diagnostic_free(). */
gboolean code_intel_step_diagnostic(AppData* app, EditorTab* tab, gint line, gint index, gboolean forward,
                                    gint* out_line, gint* out_index, LspDiagnostic** out_diag);

/* Characters that trigger signature help in tab ("(," by default) */
const gchar* code_intel_signature_triggers(AppData* app, EditorTab* tab);
/* Characters that trigger completion in tab, or NULL when none */
const gchar* code_intel_completion_triggers(AppData* app, EditorTab* tab);

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
