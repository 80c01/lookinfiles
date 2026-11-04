/* TeX font metric (.tfm), virtual font (.vf) and PostScript Type 1 (.pfb) parser. */
#include "tfm_info.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static guint16 read_be16(const guchar* p) {
    return (guint16)((p[0] << 8) | p[1]);
}

static guint32 read_be32(const guchar* p) {
    return ((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | p[3];
}

static guint32 read_le32(const guchar* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static gdouble fix_to_double(guint32 fix) {
    return (gdouble)((gint32)fix) / 1048576.0; /* 2^20 */
}

void tfm_info_free(TfmInfo* info) {
    if (info == NULL) return;
    g_free(info->font_family);
    g_free(info->coding_scheme);
    g_free(info->full_name);
    g_free(info->comment);
    g_free(info);
}

static TfmInfo* parse_tfm(const guchar* data, gsize len, GError** error) {
    if (len < 24) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "file too short for TFM");
        return NULL;
    }

    guint16 lf = read_be16(data);
    guint16 lh = read_be16(data + 2);
    guint16 bc = read_be16(data + 4);
    guint16 ec = read_be16(data + 6);
    guint16 nw = read_be16(data + 8);
    guint16 nh = read_be16(data + 10);
    guint16 nd = read_be16(data + 12);
    guint16 ni = read_be16(data + 14);
    guint16 nl = read_be16(data + 16);
    guint16 nk = read_be16(data + 18);
    guint16 ne = read_be16(data + 20);
    guint16 np = read_be16(data + 22);

    if (lf < 6 + lh || ec + 1 < bc) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid TFM header dimensions");
        return NULL;
    }

    TfmInfo* info = g_new0(TfmInfo, 1);
    info->format = "TeX Font Metric (.tfm)";
    info->bc = bc;
    info->ec = ec;
    info->n_chars = ec >= bc ? (ec - bc + 1) : 0;
    info->nw = nw;
    info->nh = nh;
    info->nd = nd;
    info->ni = ni;
    info->nl = nl;
    info->nk = nk;
    info->ne = ne;
    info->np = np;

    /* Header block at byte 24 */
    if (lh >= 2 && len >= 32) {
        info->checksum = read_be32(data + 24);
        info->design_size = fix_to_double(read_be32(data + 28));
    }

    /* Coding scheme (BCPL string at word 2 = byte 32) */
    if (lh >= 12 && len >= 72) {
        guint slen = data[32];
        if (slen > 39) slen = 39;
        gchar* raw = g_strndup((const gchar*)data + 33, slen);
        info->coding_scheme = g_utf8_make_valid(raw, (gssize)slen);
        g_free(raw);
    }

    /* Font family (BCPL string at word 12 = byte 72) */
    if (lh >= 17 && len >= 92) {
        guint flen = data[72];
        if (flen > 19) flen = 19;
        gchar* raw = g_strndup((const gchar*)data + 73, flen);
        info->font_family = g_utf8_make_valid(raw, (gssize)flen);
        g_free(raw);
    }

    /* Font parameters table */
    gsize param_word = 6 + lh + info->n_chars + nw + nh + nd + ni + nl + nk + ne;
    if (np > 0 && (param_word + np) * 4 <= len) {
        info->has_params = TRUE;
        gsize p_off = param_word * 4;
        if (np >= 1) info->slant = fix_to_double(read_be32(data + p_off));
        if (np >= 2) info->space = fix_to_double(read_be32(data + p_off + 4));
        if (np >= 3) info->space_stretch = fix_to_double(read_be32(data + p_off + 8));
        if (np >= 4) info->space_shrink = fix_to_double(read_be32(data + p_off + 12));
        if (np >= 5) info->x_height = fix_to_double(read_be32(data + p_off + 16));
        if (np >= 6) info->quad = fix_to_double(read_be32(data + p_off + 20));
        if (np >= 7) info->extra_space = fix_to_double(read_be32(data + p_off + 24));
    }

    return info;
}

static TfmInfo* parse_vf(const guchar* data, gsize len, GError** error) {
    if (len < 11 || data[0] != 0xF7 || data[1] != 202) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a TeX virtual font");
        return NULL;
    }

    guint k = data[2];
    if (3 + k + 8 > len) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "truncated VF header");
        return NULL;
    }

    TfmInfo* info = g_new0(TfmInfo, 1);
    info->format = "TeX Virtual Font (.vf)";
    if (k > 0) {
        gchar* raw = g_strndup((const gchar*)data + 3, k);
        info->comment = g_utf8_make_valid(raw, (gssize)k);
        g_free(raw);
    }

    info->checksum = read_be32(data + 3 + k);
    info->design_size = fix_to_double(read_be32(data + 7 + k));
    return info;
}

static gchar* extract_ps_key(const gchar* text, const gchar* key) {
    const gchar* p = strstr(text, key);
    if (p == NULL) return NULL;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '(' || *p == '/') p++;
    const gchar* end = p;
    while (*end && *end != ')' && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') end++;
    if (end > p) {
        return g_strndup(p, end - p);
    }
    return NULL;
}

static TfmInfo* parse_pfb(const guchar* data, gsize len, GError** error) {
    if (len < 16 || data[0] != 0x80 || data[1] != 0x01) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a PostScript Type 1 binary font");
        return NULL;
    }

    guint32 seg_len = read_le32(data + 2);
    if (6 + seg_len > len) seg_len = (guint32)(len - 6);

    gchar* ascii = g_strndup((const gchar*)data + 6, seg_len);
    TfmInfo* info = g_new0(TfmInfo, 1);
    info->format = "PostScript Type 1 (.pfb)";
    info->font_family = extract_ps_key(ascii, "/FamilyName");
    if (info->font_family == NULL) {
        info->font_family = extract_ps_key(ascii, "/FontName");
    }
    info->full_name = extract_ps_key(ascii, "/FullName");
    info->coding_scheme = extract_ps_key(ascii, "/Encoding");
    g_free(ascii);

    return info;
}

TfmInfo* tfm_info_parse(const guchar* data, gsize len, GError** error) {
    if (data == NULL || len < 6) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "data too short");
        return NULL;
    }

    if (data[0] == 0x80 && data[1] == 0x01) {
        return parse_pfb(data, len, error);
    }
    if (data[0] == 0xF7 && data[1] == 202) {
        return parse_vf(data, len, error);
    }
    return parse_tfm(data, len, error);
}

gboolean tfm_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error) {
    g_autoptr(TfmInfo) info = tfm_info_parse(data, len, error);
    if (info == NULL) return FALSE;

    doc->monospace = TRUE;
    doc->wrap = FALSE;

    preview_doc_field(doc, "Format", info->format);

    if (info->font_family != NULL) {
        preview_doc_field(doc, "Family", info->font_family);
    }
    if (info->full_name != NULL) {
        preview_doc_field(doc, "Full name", info->full_name);
    }
    if (info->comment != NULL && info->comment[0] != '\0') {
        preview_doc_field(doc, "Comment", info->comment);
    }
    if (info->coding_scheme != NULL) {
        preview_doc_field(doc, "Coding scheme", info->coding_scheme);
    }

    if (info->design_size > 0.0) {
        gchar* ds_str = g_strdup_printf("%.2f pt", info->design_size);
        preview_doc_field(doc, "Design size", ds_str);
        g_free(ds_str);
    }

    if (info->checksum != 0) {
        gchar* cs_str = g_strdup_printf("0x%08X", info->checksum);
        preview_doc_field(doc, "Checksum", cs_str);
        g_free(cs_str);
    }

    if (info->n_chars > 0) {
        gchar* ch_str = g_strdup_printf("%u glyphs (code %u .. %u)", info->n_chars, info->bc, info->ec);
        preview_doc_field(doc, "Characters", ch_str);
        g_free(ch_str);
    }

    if (info->has_params) {
        preview_doc_append_len(doc, "\n", 1);
        preview_doc_section(doc, "Font Metric Parameters");

        gchar* sp_str = g_strdup_printf("%.4f (stretch %.4f, shrink %.4f)",
                                        info->space, info->space_stretch, info->space_shrink);
        preview_doc_field(doc, "Normal space", sp_str);
        g_free(sp_str);

        gchar* ex_str = g_strdup_printf("%.4f", info->x_height);
        preview_doc_field(doc, "x-height (ex)", ex_str);
        g_free(ex_str);

        gchar* q_str = g_strdup_printf("%.4f", info->quad);
        preview_doc_field(doc, "Quad width (em)", q_str);
        g_free(q_str);

        if (info->slant != 0.0) {
            gchar* sl_str = g_strdup_printf("%.4f", info->slant);
            preview_doc_field(doc, "Slant ratio", sl_str);
            g_free(sl_str);
        }
    }

    if (info->nw > 0 || info->nh > 0 || info->nd > 0) {
        preview_doc_append_len(doc, "\n", 1);
        preview_doc_section(doc, "Metric Tables");

        g_autoptr(PreviewTable) table = preview_table_new(3);
        const gchar* header[] = { "Table", "Words", "Description" };
        preview_table_set_header(table, header);
        preview_table_set_right_aligned(table, 1);
        preview_table_set_max_cell_width(table, 50);

        struct { const gchar* name; guint words; const gchar* desc; } tbls[] = {
            { "Width (nw)", info->nw, "Character widths" },
            { "Height (nh)", info->nh, "Character heights" },
            { "Depth (nd)", info->nd, "Character depths" },
            { "Italic (ni)", info->ni, "Italic corrections" },
            { "Lig/Kern (nl)", info->nl, "Ligature and kerning steps" },
            { "Kern (nk)", info->nk, "Kerning distances" },
            { "Extensible (ne)", info->ne, "Extensible character recipes" },
            { "Param (np)", info->np, "Font parameter words" }
        };

        for (guint i = 0; i < G_N_ELEMENTS(tbls); i++) {
            if (tbls[i].words > 0) {
                gchar* w_str = g_strdup_printf("%u", tbls[i].words);
                const gchar* row[] = { tbls[i].name, w_str, tbls[i].desc };
                preview_table_add_row(table, row, 3);
                g_free(w_str);
            }
        }
        preview_doc_add_table(doc, table);
    }

    return TRUE;
}
