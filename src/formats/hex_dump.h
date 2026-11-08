#ifndef HEX_DUMP_H
#define HEX_DUMP_H

#include <glib.h>

/* Classic hex dump of binary content, one line per 16 bytes:
 *   00000000  7f 45 4c 46 02 01 01 00  00 00 00 00 00 00 00 00  |.ELF............|
 * Every line, the last one included, has the same width so the columns stay
 * aligned in a monospace font. */

#define HEX_DUMP_BYTES_PER_LINE 16
#define HEX_DUMP_OFFSET_WIDTH   8   /* hex digits of the offset column */
#define HEX_DUMP_ASCII_COLUMN   60  /* character index of the opening '|' */
#define HEX_DUMP_LINE_WIDTH     78  /* characters per line, without '\n' */

/* Dump of len bytes; offsets start at base_offset. Bytes outside printable
 * ASCII show as '.' in the ASCII column, so the result is valid UTF-8.
 * Returns "" for len 0. Caller frees. */
gchar* hex_dump_format(const guchar* data, gsize len, guint64 base_offset);

#endif /* HEX_DUMP_H */
