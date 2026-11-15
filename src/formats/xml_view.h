#ifndef XML_VIEW_H
#define XML_VIEW_H

#include <glib.h>

#include "preview/preview_doc.h"

/* XML pretty-printer for the preview: parses a document (its tags must
 * match; comments, CDATA sections, processing instructions, a DOCTYPE with
 * its internal subset and namespaces are kept, entities are shown as
 * written) and appends it re-indented with two spaces and coloured: tag
 * names, attribute names and values, comments. Whitespace between tags is
 * dropped; an element holding only a short text stays on one line. Pure
 * GLib, unit-tested. */

/* Most lines written; the rest is replaced by a note */
#define XML_VIEW_MAX_LINES 10000

typedef struct {
    guint n_elements;     /* elements in the document */
    gchar root[64];       /* name of the first element (cut if longer) */
    gint level;           /* fold level in effect (PreviewFold.level, never AUTO) */
    gboolean cut;         /* the cut text ended inside an element */
} XmlViewInfo;

/* Append the pretty-printed text to doc. FALSE with error set (line and
 * column of the problem) when the text is not XML; doc is then unchanged.
 *
 * cut: the text stops before the end of the file (the first PREVIEW_BYTES
 * of a large file); the elements open at the end are shown up to there,
 * each ending with a dimmed "…" line instead of its end tag, and a tag,
 * comment or section the cut shortened is left out.
 *
 * The elements written on several lines fold as fold asks (NULL:
 * PREVIEW_FOLD_AUTO, as json_view_append_folded()), each recorded as a fold
 * point named by its path ("/feed", "/feed/entry[2]": the position among the
 * siblings of the same name, from 1, when there are several), and each line
 * gets the path of its element (text and comments: the element around them).
 * The children of the outer element are tagged PREVIEW_STYLE_OUTLINE1 (name
 * and attributes) for the tagbar. */
gboolean xml_view_append(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                         const PreviewFold* fold, XmlViewInfo* info, GError** error);

/* The whole preview of an XML file in doc (monospace, not wrapped): a
 * dimmed summary line ("XML · <feed> · 12 elements"), the note if not NULL
 * (the text was cut), then xml_view_append(). FALSE with error set when the
 * text is not XML. */
gboolean xml_view_build(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                        const PreviewFold* fold, const gchar* note, GError** error);

/* The same as a tree (data_tree.h): the attributes ("@id") and nodes of
 * the outer element are the first level; an element holding only a text is
 * a row with that text as value, others list their attributes, elements
 * ("entry[2]"), texts ("#text"), CDATA ("#cdata") and comments
 * ("#comment"). The same fold points and paths as xml_view_build() (an
 * attribute's: "/feed/entry[2]/@id"). */
gboolean xml_view_build_tree(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                             const PreviewFold* fold, const gchar* note, GError** error);

#endif /* XML_VIEW_H */
