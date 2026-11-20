#ifndef XML_TEXT_H
#define XML_TEXT_H

#include <glib.h>

/* The visible text of one line of an XML file (a Canvas note's page.xml):
 * tags removed, the entities &lt; &gt; &amp; &quot; &apos; and &#N; /
 * &#xN; decoded. A line may start inside an element's text (its second
 * line). The match at byte column / length of line is moved onto the
 * returned text; NULL when it touches markup (a tag or an entity). Pure
 * helper, no widgets. */
gchar* xml_text_line(const gchar* line, gint* column, gint* length);

#endif /* XML_TEXT_H */
