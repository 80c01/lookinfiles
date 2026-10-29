#ifndef JSON_VIEW_H
#define JSON_VIEW_H

#include <glib.h>

#include "preview_doc.h"

/* JSON pretty-printer for the preview: parses a document (tolerant: comments,
 * trailing commas, unquoted keys, NaN / Infinity, as in JSONC and JSON5) or a
 * JSON Lines file (one value per line), and appends it re-indented with two
 * spaces and coloured like VS Code: keys, strings, numbers, keywords. Short
 * arrays of scalars stay on one line. Strings are kept as written (escapes
 * included). Pure GLib, unit-tested. */

/* Most lines written; the rest is replaced by a note */
#define JSON_VIEW_MAX_LINES 10000

typedef struct {
    gboolean lines;        /* JSON Lines: several top-level values */
    guint n_values;        /* top-level values */
    gchar root;            /* '{', '[' or 0 (scalar) of the first value */
    guint n_root_items;    /* keys or items of the first value */
} JsonViewInfo;

/* Append the pretty-printed text to doc. FALSE with error set (line and
 * column of the problem) when the text is not JSON; doc is then unchanged. */
gboolean json_view_append(PreviewDoc* doc, const gchar* text, gsize len, JsonViewInfo* info,
                          GError** error);

#endif /* JSON_VIEW_H */
