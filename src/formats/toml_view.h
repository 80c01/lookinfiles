#ifndef TOML_VIEW_H
#define TOML_VIEW_H

#include <glib.h>

#include "preview/preview_doc.h"

/* TOML preview: the document as written (its own layout and comments kept),
 * coloured: table headers, keys, strings, numbers and dates, booleans,
 * comments. Each statement must be a key = value pair, a [table] or
 * [[array of tables]] header, a comment or a blank line, so that INI and
 * other files are not taken for TOML. Pure GLib, unit-tested. */

/* Most lines written; the rest is replaced by a note */
#define TOML_VIEW_MAX_LINES 10000

typedef struct {
    guint n_tables;       /* table and array of tables headers */
    guint n_keys;         /* key = value pairs outside inline tables */
    gint level;           /* fold level in effect (PreviewFold.level, never AUTO) */
    gboolean cut;         /* the cut text ended inside a value */
} TomlViewInfo;

/* Append the coloured text to doc. FALSE with error set (line and column
 * of the problem) when the text is not TOML; doc is then unchanged.
 *
 * cut: the text stops before the end of the file (the first PREVIEW_BYTES
 * of a large file); its last, incomplete line is left out, a value still
 * open at the end (a string or array on several lines) is shown up to
 * there, and a dimmed "…" line ends the text.
 *
 * Blocks fold as fold asks (NULL: PREVIEW_FOLD_AUTO, as
 * json_view_append_folded()): a table with its keys and the tables under it
 * ([a.b] after [a]; after [[a]], under its last entry), and an array or
 * inline table written on several lines. Each is a fold point named by its
 * path ("servers", "servers[1].meta", "deps[2]"), and each line gets the
 * path of its key or array item. Table headers are tagged
 * PREVIEW_STYLE_OUTLINE1..6 by depth for the tagbar. */
gboolean toml_view_append(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                          const PreviewFold* fold, TomlViewInfo* info, GError** error);

/* The whole preview of a TOML file in doc (monospace, not wrapped): a
 * dimmed summary line ("TOML · 4 tables"), the note if not NULL (the text
 * was cut), then toml_view_append(). FALSE with error set when the text is
 * not TOML. */
gboolean toml_view_build(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                         const PreviewFold* fold, const gchar* note, GError** error);

#endif /* TOML_VIEW_H */
