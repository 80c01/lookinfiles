/* Text encoding detection and conversion (see text_encoding.h). */
#include "core/text_encoding.h"

#include <string.h>

/* UTF-16 without a byte order mark: mostly NUL bytes at odd (LE) or even
 * (BE) positions, as ASCII text gives */
static const gchar* utf16_without_bom(const guchar* d, gsize len) {
    if (len < 8) {
        return NULL;
    }
    gsize n = MIN(len, 4096) & ~(gsize)1;
    gsize even = 0, odd = 0;
    for (gsize i = 0; i < n; i += 2) {
        even += d[i] == 0;
        odd += d[i + 1] == 0;
    }
    gsize pairs = n / 2;
    if (odd * 10 >= pairs * 6 && even * 20 <= pairs) {
        return "UTF-16LE";
    }
    if (even * 10 >= pairs * 6 && odd * 20 <= pairs) {
        return "UTF-16BE";
    }
    return NULL;
}

const gchar* text_encoding_detect(const guchar* d, gsize len) {
    if (len >= 4 && memcmp(d, "\xFF\xFE\0\0", 4) == 0) return "UTF-32LE";
    if (len >= 4 && memcmp(d, "\0\0\xFE\xFF", 4) == 0) return "UTF-32BE";
    if (len >= 3 && memcmp(d, "\xEF\xBB\xBF", 3) == 0) return "UTF-8 BOM";
    if (len >= 2 && memcmp(d, "\xFF\xFE", 2) == 0) return "UTF-16LE";
    if (len >= 2 && memcmp(d, "\xFE\xFF", 2) == 0) return "UTF-16BE";
    const gchar* utf16 = utf16_without_bom(d, len);
    if (utf16 != NULL) {
        return utf16;
    }
    const gchar* end = NULL;
    if (g_utf8_validate((const gchar*)d, (gssize)len, &end) || (gsize)((const guchar*)end - d) + 4 > len) {
        return "UTF-8"; /* valid, or only the last character is cut */
    }
    return "Windows-1252";
}

gchar* text_encoding_to_utf8(const gchar* data, gsize len, const gchar** encoding) {
    const guchar* d = (const guchar*)data;
    const gchar* enc = text_encoding_detect(d, len);
    if (encoding != NULL) {
        *encoding = enc;
    }
    gsize skip = 0;
    const gchar* from = NULL;
    gsize unit = 1;
    if (strcmp(enc, "UTF-32LE") == 0 || strcmp(enc, "UTF-32BE") == 0) {
        skip = 4;
        from = enc;
        unit = 4;
    } else if (strcmp(enc, "UTF-16LE") == 0 || strcmp(enc, "UTF-16BE") == 0) {
        skip = (len >= 2 && (memcmp(d, "\xFF\xFE", 2) == 0 || memcmp(d, "\xFE\xFF", 2) == 0)) ? 2 : 0;
        from = enc;
        unit = 2;
    } else if (strcmp(enc, "UTF-8 BOM") == 0) {
        skip = 3;
    } else if (strcmp(enc, "Windows-1252") == 0) {
        from = "WINDOWS-1252";
    }
    if (from == NULL) {
        return g_utf8_make_valid(data + skip, (gssize)(len - skip));
    }
    gsize n = (len - skip) / unit * unit;
    gsize read = 0;
    gchar* out = g_convert_with_fallback(data + skip, (gssize)n, "UTF-8", from, "?", &read, NULL, NULL);
    if (out == NULL && strcmp(from, "WINDOWS-1252") == 0) {
        /* Bytes Windows-1252 leaves undefined (0x81...): Latin-1 maps every byte */
        out = g_convert(data, (gssize)len, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
    }
    if (out == NULL && unit == 2) {
        /* A cut surrogate pair at the end: drop the last unit */
        out = g_convert_with_fallback(data + skip, (gssize)(n - 2), "UTF-8", from, "?", NULL, NULL, NULL);
    }
    return out != NULL ? out : g_utf8_make_valid(data, (gssize)len);
}

const gchar* text_line_ending_detect(const gchar* data, gsize len) {
    gsize lf = 0, crlf = 0, cr = 0;
    for (gsize i = 0; data != NULL && i < len; i++) {
        if (data[i] == '\n') {
            lf++;
        } else if (data[i] == '\r') {
            if (i + 1 < len && data[i + 1] == '\n') {
                crlf++;
                i++;
            } else {
                cr++;
            }
        }
    }
    if (crlf > lf && crlf >= cr) return "Windows (CRLF)";
    if (cr > lf && cr > crlf) return "Mac (CR)";
    return "Unix (LF)";
}

const gchar* text_final_break(const gchar* data, gsize len) {
    if (data == NULL || len == 0) return "";
    if (data[len - 1] == '\n') {
        return (len >= 2 && data[len - 2] == '\r') ? "\r\n" : "\n";
    }
    return data[len - 1] == '\r' ? "\r" : "";
}
