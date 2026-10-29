#ifndef RTF_H
#define RTF_H

#include <glib.h>

/* Rich Text Format (.rtf) converted to simple HTML for the HTML renderer:
 * paragraphs, line breaks, bold / italic / underline / strike-through, tables
 * (\trowd ... \cell ... \row), Unicode (\uN) and code-page (\'hh, Windows-1252
 * or \ansicpgN) characters, PNG and JPEG pictures (\pngblip, \jpegblip) as
 * data: URIs, and the title and author of the \info group. Font, colour and
 * style tables, headers, footers and footnotes are skipped. Pure GLib,
 * unit-tested. */

/* HTML of an RTF text, about max_text bytes of text at most; NULL with error
 * set if it does not start with "{\rtf" */
gchar* rtf_to_html(const gchar* rtf, gsize len, gsize max_text, GError** error);

#endif /* RTF_H */
