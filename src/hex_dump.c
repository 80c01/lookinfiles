/* Hex dump formatting for the preview of binary files */
#include "hex_dump.h"

gchar* hex_dump_format(const guchar* data, gsize len, guint64 base_offset) {
    static const gchar digits[] = "0123456789abcdef";
    gsize n_lines = (len + HEX_DUMP_BYTES_PER_LINE - 1) / HEX_DUMP_BYTES_PER_LINE;
    GString* out = g_string_sized_new(n_lines * (HEX_DUMP_LINE_WIDTH + 1) + 1);

    for (gsize start = 0; start < len; start += HEX_DUMP_BYTES_PER_LINE) {
        gsize count = MIN((gsize)HEX_DUMP_BYTES_PER_LINE, len - start);
        g_string_append_printf(out, "%0*" G_GINT64_MODIFIER "x  ",
                               HEX_DUMP_OFFSET_WIDTH, base_offset + start);
        for (gsize i = 0; i < HEX_DUMP_BYTES_PER_LINE; i++) {
            if (i == HEX_DUMP_BYTES_PER_LINE / 2) {
                g_string_append_c(out, ' ');
            }
            if (i < count) {
                guchar b = data[start + i];
                g_string_append_c(out, digits[b >> 4]);
                g_string_append_c(out, digits[b & 0x0f]);
                g_string_append_c(out, ' ');
            } else {
                g_string_append(out, "   ");
            }
        }
        g_string_append_c(out, ' ');
        g_string_append_c(out, '|');
        for (gsize i = 0; i < HEX_DUMP_BYTES_PER_LINE; i++) {
            if (i < count) {
                guchar b = data[start + i];
                g_string_append_c(out, (b >= 0x20 && b < 0x7f) ? (gchar)b : '.');
            } else {
                g_string_append_c(out, ' ');
            }
        }
        g_string_append(out, "|\n");
    }
    return g_string_free(out, FALSE);
}
