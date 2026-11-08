/* Font file details (see font_info.h). */
#include "font_info.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

#define MAX_FONT_FILE (32u * 1024 * 1024)

static guint16 be16(const guchar* p) {
    return (guint16)((p[0] << 8) | p[1]);
}

static guint32 be32(const guchar* p) {
    return ((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | p[3];
}

typedef struct {
    const guchar* data;
    gsize len;
    gboolean owned;       /* data was decompressed (WOFF) and must be freed */
} Table;

static void table_clear(Table* t) {
    if (t->owned) {
        g_free((gpointer)t->data);
    }
    t->data = NULL;
    t->len = 0;
    t->owned = FALSE;
}

/* zlib-compressed WOFF table */
static gboolean inflate_table(const guchar* src, gsize src_len, gsize out_len, Table* out) {
    GZlibDecompressor* dec = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB);
    guchar* buf = g_malloc(out_len + 1);
    gsize read = 0, written = 0;
    GConverterResult r = g_converter_convert(G_CONVERTER(dec), src, src_len, buf, out_len + 1,
                                             G_CONVERTER_INPUT_AT_END, &read, &written, NULL);
    g_object_unref(dec);
    if ((r != G_CONVERTER_FINISHED && r != G_CONVERTER_CONVERTED) || written != out_len) {
        g_free(buf);
        return FALSE;
    }
    out->data = buf;
    out->len = written;
    out->owned = TRUE;
    return TRUE;
}

/* Find a table of the font at offset base (sfnt) or of a WOFF file */
static gboolean find_table(const guchar* data, gsize len, gsize base, gboolean woff, const gchar* tag,
                           Table* out) {
    if (woff) {
        if (len < 44) {
            return FALSE;
        }
        guint n = be16(data + 12);
        for (guint i = 0; i < n; i++) {
            gsize e = 44 + (gsize)i * 20;
            if (e + 20 > len) {
                return FALSE;
            }
            if (memcmp(data + e, tag, 4) != 0) {
                continue;
            }
            guint32 off = be32(data + e + 4);
            guint32 comp = be32(data + e + 8);
            guint32 orig = be32(data + e + 12);
            if ((guint64)off + comp > len || orig > MAX_FONT_FILE) {
                return FALSE;
            }
            if (comp == orig) {
                out->data = data + off;
                out->len = orig;
                out->owned = FALSE;
                return TRUE;
            }
            return inflate_table(data + off, comp, orig, out);
        }
        return FALSE;
    }
    if (base + 12 > len) {
        return FALSE;
    }
    guint n = be16(data + base + 4);
    for (guint i = 0; i < n; i++) {
        gsize e = base + 12 + (gsize)i * 16;
        if (e + 16 > len) {
            return FALSE;
        }
        if (memcmp(data + e, tag, 4) == 0) {
            guint32 off = be32(data + e + 8);
            guint32 tlen = be32(data + e + 12);
            if ((guint64)off + tlen > len) {
                return FALSE;
            }
            out->data = data + off;
            out->len = tlen;
            out->owned = FALSE;
            return TRUE;
        }
    }
    return FALSE;
}

/* A name record as UTF-8: Windows (UTF-16BE) or Macintosh Roman (ASCII part) */
static gchar* decode_name(guint platform, const guchar* p, gsize n) {
    if (platform == 3 || platform == 0) {
        GString* s = g_string_new(NULL);
        for (gsize i = 0; i + 1 < n; i += 2) {
            gunichar c = be16(p + i);
            if (c >= 0xD800 && c <= 0xDBFF && i + 3 < n) {
                gunichar lo = be16(p + i + 2);
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
            if (g_unichar_validate(c)) {
                g_string_append_unichar(s, c);
            }
        }
        return g_string_free(s, FALSE);
    }
    return g_utf8_make_valid((const gchar*)p, (gssize)n);
}

/* Name ID from the 'name' table: Windows English first, then any Unicode or
 * Macintosh English record */
static gchar* get_name(const Table* t, guint id) {
    if (t->len < 6) {
        return NULL;
    }
    guint count = be16(t->data + 2);
    guint strings = be16(t->data + 4);
    gint best = -1;
    gint best_score = 0;
    for (guint i = 0; i < count; i++) {
        gsize r = 6 + (gsize)i * 12;
        if (r + 12 > t->len) {
            break;
        }
        guint platform = be16(t->data + r);
        guint language = be16(t->data + r + 4);
        guint name_id = be16(t->data + r + 6);
        if (name_id != id) {
            continue;
        }
        gint score = (platform == 3 && language == 0x0409) ? 3
                     : (platform == 3 || platform == 0) ? 2 : (platform == 1 && language == 0) ? 1 : 0;
        if (score > best_score) {
            best_score = score;
            best = (gint)i;
        }
    }
    if (best < 0) {
        return NULL;
    }
    gsize r = 6 + (gsize)best * 12;
    guint platform = be16(t->data + r);
    gsize n = be16(t->data + r + 8);
    gsize off = (gsize)strings + be16(t->data + r + 10);
    if (off + n > t->len) {
        return NULL;
    }
    gchar* s = decode_name(platform, t->data + off, n);
    g_strstrip(s);
    if (s[0] == '\0') {
        g_free(s);
        return NULL;
    }
    return s;
}

static gboolean not_a_font(GError** error) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a font file"));
    return FALSE;
}

FontInfo* font_info_parse(const guchar* data, gsize len, GError** error) {
    if (len < 12) {
        not_a_font(error);
        return NULL;
    }
    FontInfo* info = g_new0(FontInfo, 1);
    info->n_fonts = 1;
    gsize base = 0;
    gboolean woff = FALSE;
    guint32 flavor = be32(data);
    if (memcmp(data, "wOF2", 4) == 0) {
        info->format = g_strdup("WOFF2");
        return info; /* Brotli-compressed tables */
    }
    if (memcmp(data, "wOFF", 4) == 0) {
        woff = TRUE;
        flavor = be32(data + 4);
        info->format = g_strdup(flavor == 0x4F54544F ? "WOFF (CFF)" : "WOFF");
    } else if (memcmp(data, "ttcf", 4) == 0) {
        info->n_fonts = be32(data + 8);
        base = len >= 16 ? be32(data + 12) : len;
        flavor = base + 4 <= len ? be32(data + base) : 0;
        info->format = g_strdup(flavor == 0x4F54544F ? _("OpenType collection") : _("TrueType collection"));
    } else if (flavor == 0x00010000 || memcmp(data, "true", 4) == 0) {
        info->format = g_strdup("TrueType");
    } else if (memcmp(data, "OTTO", 4) == 0) {
        info->format = g_strdup("OpenType (CFF)");
    } else {
        font_info_free(info);
        not_a_font(error);
        return NULL;
    }

    Table name = { 0 };
    if (!find_table(data, len, base, woff, "name", &name)) {
        font_info_free(info);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("font without a name table"));
        return NULL;
    }
    info->family = get_name(&name, 16);
    if (info->family == NULL) {
        info->family = get_name(&name, 1);
    }
    info->style = get_name(&name, 17);
    if (info->style == NULL) {
        info->style = get_name(&name, 2);
    }
    info->full_name = get_name(&name, 4);
    info->version = get_name(&name, 5);
    if (info->version != NULL && g_str_has_prefix(info->version, "Version ")) {
        memmove(info->version, info->version + 8, strlen(info->version + 8) + 1);
    }
    info->manufacturer = get_name(&name, 8);
    info->designer = get_name(&name, 9);
    info->copyright = get_name(&name, 0);
    info->license = get_name(&name, 13);
    info->license_url = get_name(&name, 14);
    table_clear(&name);

    Table maxp = { 0 };
    if (find_table(data, len, base, woff, "maxp", &maxp) && maxp.len >= 6) {
        info->n_glyphs = be16(maxp.data + 4);
    }
    table_clear(&maxp);
    Table os2 = { 0 };
    if (find_table(data, len, base, woff, "OS/2", &os2) && os2.len >= 6) {
        info->weight = be16(os2.data + 4);
    }
    table_clear(&os2);
    return info;
}

FontInfo* font_info_read_file(const gchar* path, GError** error) {
    gchar* data = NULL;
    gsize len = 0;
    GFile* file = g_file_new_for_path(path);
    GFileInfo* finfo = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, error);
    if (finfo == NULL) {
        g_object_unref(file);
        return NULL;
    }
    goffset size = g_file_info_get_size(finfo);
    g_object_unref(finfo);
    if (size > (goffset)MAX_FONT_FILE) {
        g_object_unref(file);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("font file too large"));
        return NULL;
    }
    gboolean ok = g_file_load_contents(file, NULL, &data, &len, NULL, error);
    g_object_unref(file);
    if (!ok) {
        return NULL;
    }
    FontInfo* info = font_info_parse((const guchar*)data, len, error);
    g_free(data);
    return info;
}

void font_info_free(FontInfo* info) {
    if (info == NULL) {
        return;
    }
    g_free(info->format);
    g_free(info->family);
    g_free(info->style);
    g_free(info->full_name);
    g_free(info->version);
    g_free(info->designer);
    g_free(info->manufacturer);
    g_free(info->copyright);
    g_free(info->license);
    g_free(info->license_url);
    g_free(info);
}

const gchar* font_info_weight_name(guint weight) {
    if (weight == 0) return NULL;
    if (weight < 150) return "Thin";
    if (weight < 250) return "Extra Light";
    if (weight < 350) return "Light";
    if (weight < 450) return "Regular";
    if (weight < 550) return "Medium";
    if (weight < 650) return "Semi Bold";
    if (weight < 750) return "Bold";
    if (weight < 850) return "Extra Bold";
    return "Black";
}
