#ifndef HTML_TEXT_H
#define HTML_TEXT_H

/* Text helpers of the HTML preview (html.c): entities, attributes, tag
 * stripping. Pure string code, no widgets: unit-tested in tests/. */
#include <glib.h>

/* *ptr points to '&': append the character it names to out (&amp;, &#233;,
 * &#xE9;, &hellip;...) and move *ptr past the ';'. An unknown or unclosed
 * entity is copied as is. */
void html_text_append_entity(GString* out, const gchar** ptr);

/* Value of attribute attr_name (any case) in a tag ("<a href='x'>"), quoted
 * or not; NULL when absent. Caller frees. */
gchar* html_text_attr(const gchar* tag_str, const gchar* attr_name);

/* First needle in haystack, ignoring ASCII case, starting before limit
 * (NULL: the end of the string); NULL if none */
const gchar* html_text_find_case(const gchar* haystack, const gchar* needle, const gchar* limit);

/* src without its tags, entities decoded, runs of white space folded into
 * one space, no trailing space. NULL gives "". Caller frees. */
gchar* html_text_strip(const gchar* src);

/* Language of a code tag from class="language-x" or class="lang-x", or NULL.
 * Caller frees. */
gchar* html_text_code_lang(const gchar* tag_str);

/* content of <meta name="name"> (or property="name"), any case, or NULL.
 * Caller frees. */
gchar* html_text_meta(const gchar* html, const gchar* name);

#endif /* HTML_TEXT_H */
