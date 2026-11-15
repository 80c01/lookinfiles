#ifndef YAML_VIEW_H
#define YAML_VIEW_H

#include <glib.h>

#include "preview/preview_doc.h"

/* YAML preview: the document as written (its own layout and comments kept,
 * at most TEXT_OUTLINE_MAX_LINES lines), coloured: keys, strings, numbers
 * and dates, true / false / null, anchors, aliases and tags, comments,
 * document markers. A subset of YAML is read: block mappings and sequences
 * (compact ones too: "- key: value"), plain scalars on several lines,
 * quoted strings, block scalars (| and >), flow collections ([a, b],
 * {a: 1}) on one line or several, several documents. Tabs in indentation,
 * lines indented where nothing is open and complex keys (?) make the text
 * fail, to be shown as plain text. Pure GLib, unit-tested. */

typedef struct {
    guint n_documents;    /* documents (between --- markers) */
    gchar root;           /* first document: 'm' mapping, 's' sequence, 0 scalar or empty */
    guint n_root;         /* keys or items of the first document's root */
    gint level;           /* fold level in effect (PreviewFold.level, never AUTO) */
    gboolean cut;         /* the cut text ended inside a value */
} YamlViewInfo;

/* Append the coloured text to doc. FALSE with error set (line and column
 * of the problem) when the text is not in the subset read; doc is then
 * unchanged.
 *
 * cut: the text stops before the end of the file (the first PREVIEW_BYTES
 * of a large file); its last, incomplete line is left out, a value still
 * open at the end (a quoted string or flow collection on several lines) is
 * shown up to there, and a dimmed "…" line ends the text.
 *
 * Blocks fold as fold asks (NULL: PREVIEW_FOLD_AUTO, as
 * json_view_append_folded()): the mappings and sequences under a key or
 * an item, and flow collections on several lines. Each is a fold point
 * named by its path ("servers", "servers[1].ports", "[2].kind" in the
 * third document of a file of several), and each line gets the path of its
 * key or item. The keys of the root mapping, and the keys holding a block
 * below them, are tagged PREVIEW_STYLE_OUTLINE1..6 by depth for the
 * tagbar. */
gboolean yaml_view_append(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                          const PreviewFold* fold, YamlViewInfo* info, GError** error);

/* The whole preview of a YAML file in doc (monospace, not wrapped): a
 * dimmed summary line ("YAML · 5 keys", "YAML · 3 documents"), the note if
 * not NULL (the text was cut), then yaml_view_append(). FALSE with error
 * set when the text is not read as YAML. */
gboolean yaml_view_build(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                         const PreviewFold* fold, const gchar* note, GError** error);

#endif /* YAML_VIEW_H */
