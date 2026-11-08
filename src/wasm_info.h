#ifndef WASM_INFO_H
#define WASM_INFO_H

#include <glib.h>
#include "preview/preview_doc.h"

/* WebAssembly (.wasm) binary parser and preview builder.
 * Extracts WASM version, imported modules/functions, exports, memory limits,
 * types, and function definitions.
 * Pure GLib, Layer 1 foundation module (no widgets, unit-tested). */

typedef struct {
    guint32 version;
    guint n_types;
    guint n_imported_funcs;
    guint n_imported_mems;
    guint n_imported_tables;
    guint n_imported_globals;
    guint n_defined_funcs;
    guint n_tables;
    guint n_mems;
    guint64 mem_initial_pages;
    guint64 mem_max_pages;
    gboolean has_mem_max;
    guint n_globals;
    guint n_exports;
    guint n_data_segments;
    guint64 total_data_bytes;
    GPtrArray* imports; /* gchar* formatted */
    GPtrArray* exports; /* gchar* formatted */
} WasmInfo;

WasmInfo* wasm_info_parse(const guchar* data, gsize len, GError** error);
void wasm_info_free(WasmInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(WasmInfo, wasm_info_free)

/* Appends structured WebAssembly preview to doc */
gboolean wasm_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error);

#endif /* WASM_INFO_H */
