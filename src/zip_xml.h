#ifndef ZIP_XML_H
#define ZIP_XML_H

#include <glib.h>

#include "zip_reader.h"

/* XML parts of ZIP-based documents (Office Open XML, OpenDocument, EPUB):
 * parse a part with a GMarkupParser, resolve relationship targets, read
 * attributes and element names without their namespace prefix. Pure GLib. */

/* Parse a part (a UTF-8 byte order mark is skipped). FALSE with error set if
 * it is missing (unless optional: FALSE, error untouched), larger than
 * max_size, or not well-formed. A callback may stop the parse early by
 * setting ZIP_XML_STOP: the result is then TRUE. */
gboolean zip_xml_parse(ZipReader* zip, const gchar* part, gsize max_size, gboolean optional,
                       const GMarkupParser* parser, gpointer data, GError** error);

/* Error a parser callback sets to stop parsing early without failing */
#define ZIP_XML_STOP_DOMAIN (zip_xml_stop_quark())
GQuark zip_xml_stop_quark(void);
void zip_xml_stop(GError** error);

/* Element or attribute name without its prefix ("w:t" -> "t") */
const gchar* zip_xml_local(const gchar* name);

/* Value of an attribute by local name, or NULL */
const gchar* zip_xml_attr(const gchar** names, const gchar** values, const gchar* local);

/* Part path from a target relative to base_dir ("" for the root) or
 * absolute ("/xl/x.xml"); "." and ".." resolved. Caller frees. */
gchar* zip_xml_resolve(const gchar* base_dir, const gchar* target);

/* Directory of a part ("xl/workbook.xml" -> "xl", "a.xml" -> ""). Caller frees. */
gchar* zip_xml_dirname(const gchar* part);

/* Relationships of a part: Id -> resolved target part (from
 * "<dir>/_rels/<file>.rels"). Empty if there are none. */
GHashTable* zip_xml_relationships(ZipReader* zip, const gchar* part);

#endif /* ZIP_XML_H */
