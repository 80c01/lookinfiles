#ifndef TEXT_ENCODING_H
#define TEXT_ENCODING_H

#include <glib.h>

/* Text encodings of previewed files: byte order marks (UTF-8, UTF-16,
 * UTF-32), UTF-16 without a mark (one NUL byte in two), UTF-8, and legacy
 * 8-bit text (Windows-1252, a superset of the printable Latin-1). Pure GLib,
 * unit-tested. */

/* Encoding of the bytes: "UTF-8", "UTF-8 BOM", "UTF-16LE", "UTF-16BE",
 * "UTF-32LE", "UTF-32BE" or "Windows-1252" (static strings) */
const gchar* text_encoding_detect(const guchar* data, gsize len);

/* Text as UTF-8 (the mark removed, a character cut by the end of the data
 * dropped, invalid bytes repaired); *encoding (may be NULL) receives the
 * detected encoding. Caller frees. */
gchar* text_encoding_to_utf8(const gchar* data, gsize len, const gchar** encoding);

/* Line ending used by the text, for the status bar: "Windows (CRLF)",
 * "Mac (CR)" or "Unix (LF)" (static strings). The most frequent kind wins
 * (ties go to LF, then CRLF); text without a line break is "Unix (LF)". */
const gchar* text_line_ending_detect(const gchar* data, gsize len);

/* Line break the text ends with: "\r\n", "\n", "\r" or "" (static strings).
 * An editor strips it at load (a text view would show an extra empty last
 * line) and writes it back at save. */
const gchar* text_final_break(const gchar* data, gsize len);

#endif /* TEXT_ENCODING_H */
