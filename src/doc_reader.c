/* Word 97-2003 documents to HTML (see doc_reader.h) */
#include "doc_reader.h"
#include <gio/gio.h>
#include <string.h>

#define WORD_MAX_STREAM (64 * 1024 * 1024)
#define TABLE_MAX_STREAM (32 * 1024 * 1024)
#define FKP_SIZE 512

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

/* Windows-1252 bytes 0x80-0x9F (the others are Latin-1) */
static const gunichar CP1252[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

static gunichar from_cp1252(guint8 b) {
    return b >= 0x80 && b < 0xA0 ? CP1252[b - 0x80] : (gunichar)b;
}

typedef struct {
    gunichar c;
    guint32 fc;   /* position of the character in the WordDocument stream */
} DocChar;

typedef struct {
    guint32 fc_start, fc_end;
    guint16 istd;
    gboolean in_table, ttp;
    gint ilfo, ilvl, outline;
} ParaRun;

typedef struct {
    guint32 fc_start, fc_end;
    gint bold, italic, underline, strike, vert;  /* -1: not set */
} CharRun;

/* Operand size of a sprm at p (after its 2-byte code); -1 when unknown */
static gint sprm_operand(guint16 sprm, const guint8* p, gsize left) {
    switch (sprm >> 13) {
        case 0: case 1: return 1;
        case 2: case 4: case 5: return 2;
        case 3: return 4;
        case 7: return 3;
        default: break;
    }
    if (left < 1) return -1;
    if (sprm == 0xD608 || sprm == 0xD606) {
        /* sprmTDefTable: 2-byte size, counted from after it, plus one */
        if (left < 2) return -1;
        return 2 + (gint)u16(p) - 1;
    }
    if (sprm == 0xC615 && p[0] == 255) return -1; /* sprmPChgTabs, complex form: stop */
    return 1 + p[0];
}

/* Iterate the sprms of a grpprl */
typedef void (*SprmFunc)(guint16 sprm, const guint8* operand, gint size, gpointer data);

static void each_sprm(const guint8* grpprl, gsize len, SprmFunc func, gpointer data) {
    gsize i = 0;
    while (i + 2 <= len) {
        guint16 sprm = u16(grpprl + i);
        i += 2;
        gint size = sprm_operand(sprm, grpprl + i, len - i);
        if (size < 0 || i + (gsize)size > len) return;
        func(sprm, grpprl + i, size, data);
        i += (gsize)size;
    }
}

static void para_sprm(guint16 sprm, const guint8* op, gint size, gpointer data) {
    ParaRun* r = data;
    switch (sprm) {
        case 0x2416: r->in_table = op[0] != 0; break;
        case 0x2417: r->ttp = op[0] != 0; break;
        case 0x460B: if (size >= 2) r->ilfo = (gint16)u16(op); break;
        case 0x260A: r->ilvl = op[0]; break;
        case 0x2640: r->outline = op[0]; break;
        case 0x6649: if (size >= 4 && u32(op) > 0) r->in_table = TRUE; break;
        default: break;
    }
}

static gint char_flag(guint8 v) {
    /* 0 off, 1 on, 0x80 as the style, 0x81 the opposite of the style */
    return v == 1 || v == 0x81 ? 1 : 0;
}

static void char_sprm(guint16 sprm, const guint8* op, gint size, gpointer data) {
    (void)size;
    CharRun* r = data;
    switch (sprm) {
        case 0x0835: r->bold = char_flag(op[0]); break;
        case 0x0836: r->italic = char_flag(op[0]); break;
        case 0x0837: case 0x2A53: r->strike = char_flag(op[0]); break;
        case 0x2A3E: r->underline = op[0] != 0; break;
        case 0x2A48: r->vert = op[0]; break;
        default: break;
    }
}

/* Runs of the FKPs of a bin table (PlcBtePapx / PlcBteChpx) */
static void read_fkps(const guint8* word, gsize word_len, const guint8* plc, gsize lcb, gboolean papx, GArray* out) {
    if (lcb < 4 || (lcb - 4) % 8 != 0) return;
    guint n = (guint)((lcb - 4) / 8);
    for (guint i = 0; i < n; i++) {
        guint32 pn = u32(plc + 4 * (n + 1) + 4 * i) & 0x3FFFFF;
        gsize off = (gsize)pn * FKP_SIZE;
        if (off + FKP_SIZE > word_len) continue;
        const guint8* page = word + off;
        guint crun = page[FKP_SIZE - 1];
        gsize rgb = 4 * ((gsize)crun + 1);
        if (rgb + (gsize)crun * (papx ? 13 : 1) > FKP_SIZE - 1) continue;
        for (guint k = 0; k < crun; k++) {
            guint32 fcs = u32(page + 4 * k), fce = u32(page + 4 * (k + 1));
            guint8 b = page[rgb + k * (papx ? 13 : 1)];
            if (papx) {
                ParaRun r = {fcs, fce, 0, FALSE, FALSE, 0, 0, 9};
                if (b != 0 && (gsize)b * 2 + 2 < FKP_SIZE) {
                    const guint8* px = page + b * 2;
                    gsize len;
                    const guint8* d;
                    if (px[0] != 0) {
                        len = 2 * (gsize)px[0] - 1;
                        d = px + 1;
                    } else {
                        len = 2 * (gsize)px[1];
                        d = px + 2;
                    }
                    if (len >= 2 && (gsize)(d - page) + len <= FKP_SIZE) {
                        r.istd = u16(d);
                        each_sprm(d + 2, len - 2, para_sprm, &r);
                    }
                }
                g_array_append_val(out, r);
            } else {
                CharRun r = {fcs, fce, -1, -1, -1, -1, -1};
                if (b != 0 && (gsize)b * 2 + 1 < FKP_SIZE) {
                    const guint8* cx = page + b * 2;
                    gsize len = cx[0];
                    if ((gsize)(cx - page) + 1 + len <= FKP_SIZE) each_sprm(cx + 1, len, char_sprm, &r);
                }
                g_array_append_val(out, r);
            }
        }
    }
}

static const ParaRun* para_at(GArray* runs, guint32 fc) {
    guint lo = 0, hi = runs->len;
    while (lo < hi) {
        guint mid = (lo + hi) / 2;
        const ParaRun* r = &g_array_index(runs, ParaRun, mid);
        if (fc < r->fc_start) hi = mid;
        else if (fc >= r->fc_end) lo = mid + 1;
        else return r;
    }
    return NULL;
}

static const CharRun* char_at(GArray* runs, guint32 fc) {
    guint lo = 0, hi = runs->len;
    while (lo < hi) {
        guint mid = (lo + hi) / 2;
        const CharRun* r = &g_array_index(runs, CharRun, mid);
        if (fc < r->fc_start) hi = mid;
        else if (fc >= r->fc_end) lo = mid + 1;
        else return r;
    }
    return NULL;
}

/* Built-in style id (sti) of each style index (istd) */
static GArray* read_styles(const guint8* table, gsize table_len, guint32 fc, guint32 lcb) {
    GArray* stis = g_array_new(FALSE, TRUE, sizeof(guint16));
    if (lcb < 4 || (guint64)fc + lcb > table_len) return stis;
    const guint8* stsh = table + fc;
    guint16 cb_stshi = u16(stsh);
    if ((guint32)cb_stshi + 2 > lcb || cb_stshi < 2) return stis;
    guint16 cstd = u16(stsh + 2);
    gsize off = 2 + (gsize)cb_stshi;
    for (guint i = 0; i < cstd && off + 2 <= lcb; i++) {
        guint16 cb = u16(stsh + off);
        off += 2;
        guint16 sti = 0x0FFE; /* none */
        if (cb >= 2 && off + 2 <= lcb) sti = u16(stsh + off) & 0x0FFF;
        g_array_append_val(stis, sti);
        off += cb;
    }
    return stis;
}

/* --- HTML output --- */

typedef struct {
    GString* html;
    GString* para;        /* current paragraph (inline HTML) */
    GString* cell;        /* paragraphs of the current table cell */
    gboolean table_open, row_open;
    gint list_depth;
    gint b, i, u, s, vert; /* inline formatting open in para */
    gsize chars;
    gsize max_chars;
    gboolean truncated;
    GArray* fields;       /* Field */
} Out;

typedef struct {
    gboolean in_code;
    GString* code;
    gboolean link;
} Field;

static void close_inline(Out* o) {
    if (o->vert == 1) g_string_append(o->para, "</sup>");
    if (o->vert == 2) g_string_append(o->para, "</sub>");
    if (o->s) g_string_append(o->para, "</s>");
    if (o->u) g_string_append(o->para, "</u>");
    if (o->i) g_string_append(o->para, "</i>");
    if (o->b) g_string_append(o->para, "</b>");
    o->b = o->i = o->u = o->s = o->vert = 0;
}

static void set_inline(Out* o, const CharRun* r) {
    gint b = r != NULL && r->bold > 0, i = r != NULL && r->italic > 0, u = r != NULL && r->underline > 0;
    gint s = r != NULL && r->strike > 0, vert = r != NULL && r->vert > 0 ? r->vert : 0;
    if (b == o->b && i == o->i && u == o->u && s == o->s && vert == o->vert) return;
    close_inline(o);
    if (b) g_string_append(o->para, "<b>");
    if (i) g_string_append(o->para, "<i>");
    if (u) g_string_append(o->para, "<u>");
    if (s) g_string_append(o->para, "<s>");
    if (vert == 1) g_string_append(o->para, "<sup>");
    if (vert == 2) g_string_append(o->para, "<sub>");
    o->b = b;
    o->i = i;
    o->u = u;
    o->s = s;
    o->vert = vert == 1 || vert == 2 ? vert : 0;
}

static void set_lists(Out* o, gint depth) {
    while (o->list_depth < depth) {
        g_string_append(o->html, "<ul>\n");
        o->list_depth++;
    }
    while (o->list_depth > depth) {
        g_string_append(o->html, "</ul>\n");
        o->list_depth--;
    }
}

static void close_table(Out* o) {
    if (o->row_open) g_string_append(o->html, "</tr>\n");
    if (o->table_open) g_string_append(o->html, "</table>\n");
    o->row_open = o->table_open = FALSE;
}

static gboolean blank(const gchar* s) {
    for (; *s != '\0'; s++) {
        if (!g_ascii_isspace(*s)) return FALSE;
    }
    return TRUE;
}

/* End of a paragraph outside tables */
static void end_paragraph(Out* o, const ParaRun* run, GArray* stis) {
    close_inline(o);
    close_table(o);
    gint heading = 0;
    if (run != NULL) {
        guint16 sti = run->istd < stis->len ? g_array_index(stis, guint16, run->istd) : 0x0FFE;
        if (sti >= 1 && sti <= 9) heading = sti;
        else if (sti == 62) heading = 1; /* Title */
        else if (run->outline < 9) heading = run->outline + 1;
    }
    gboolean item = run != NULL && run->ilfo > 0;
    if (blank(o->para->str)) {
        g_string_truncate(o->para, 0);
        return;
    }
    if (item) {
        set_lists(o, MIN(run->ilvl, 8) + 1);
        g_string_append_printf(o->html, "<li>%s</li>\n", o->para->str);
    } else {
        set_lists(o, 0);
        if (heading > 0) g_string_append_printf(o->html, "<h%d>%s</h%d>\n", MIN(heading, 6), o->para->str, MIN(heading, 6));
        else g_string_append_printf(o->html, "<p>%s</p>\n", o->para->str);
    }
    g_string_truncate(o->para, 0);
}

static void end_cell(Out* o) {
    close_inline(o);
    set_lists(o, 0);
    if (!o->table_open) {
        g_string_append(o->html, "<table>\n");
        o->table_open = TRUE;
    }
    if (!o->row_open) {
        g_string_append(o->html, "<tr>");
        o->row_open = TRUE;
    }
    if (o->cell->len > 0 && o->para->len > 0) g_string_append(o->cell, "<br>");
    g_string_append(o->cell, o->para->str);
    g_string_append_printf(o->html, "<td>%s</td>", o->cell->str);
    g_string_truncate(o->cell, 0);
    g_string_truncate(o->para, 0);
}

static void end_row(Out* o) {
    close_inline(o);
    g_string_truncate(o->para, 0);
    g_string_truncate(o->cell, 0);
    if (o->row_open) g_string_append(o->html, "</tr>\n");
    o->row_open = FALSE;
}

/* URL of a HYPERLINK field code, NULL otherwise */
static gchar* hyperlink_target(const gchar* code) {
    const gchar* p = code;
    while (*p == ' ') p++;
    if (g_ascii_strncasecmp(p, "HYPERLINK", 9) != 0) return NULL;
    const gchar* q = strchr(p, '"');
    if (q == NULL) return NULL;
    const gchar* e = strchr(q + 1, '"');
    return e != NULL ? g_strndup(q + 1, (gsize)(e - q - 1)) : NULL;
}

static void append_char(Out* o, gunichar c) {
    switch (c) {
        case '<': g_string_append(o->para, "&lt;"); break;
        case '>': g_string_append(o->para, "&gt;"); break;
        case '&': g_string_append(o->para, "&amp;"); break;
        case '"': g_string_append(o->para, "&quot;"); break;
        default: g_string_append_unichar(o->para, c); break;
    }
    o->chars++;
}

static gchar* render(GArray* chars, GArray* paras, GArray* runs, GArray* stis, const gchar* head, gsize max_text) {
    Out o = {0};
    o.html = g_string_new("<html>");
    g_string_append(o.html, head != NULL ? head : "<head></head>");
    g_string_append(o.html, "<body>\n");
    o.para = g_string_new(NULL);
    o.cell = g_string_new(NULL);
    o.fields = g_array_new(FALSE, FALSE, sizeof(Field));
    o.max_chars = max_text;
    for (guint k = 0; k < chars->len; k++) {
        const DocChar* dc = &g_array_index(chars, DocChar, k);
        gunichar c = dc->c;
        Field* top = o.fields->len > 0 ? &g_array_index(o.fields, Field, o.fields->len - 1) : NULL;
        if (c == 0x13) {
            Field f = {TRUE, g_string_new(NULL), FALSE};
            g_array_append_val(o.fields, f);
            continue;
        }
        if (c == 0x14 && top != NULL) {
            top->in_code = FALSE;
            gchar* url = hyperlink_target(top->code->str);
            if (url != NULL) {
                close_inline(&o);
                gchar* esc = g_markup_escape_text(url, -1);
                g_string_append_printf(o.para, "<a href=\"%s\">", esc);
                g_free(esc);
                g_free(url);
                top->link = TRUE;
            }
            continue;
        }
        if (c == 0x15 && top != NULL) {
            if (top->link) {
                close_inline(&o);
                g_string_append(o.para, "</a>");
            }
            g_string_free(top->code, TRUE);
            g_array_set_size(o.fields, o.fields->len - 1);
            continue;
        }
        if (top != NULL && top->in_code) {
            g_string_append_unichar(top->code, c);
            continue;
        }
        if (c == 0x0D || c == 0x0C) {
            const ParaRun* run = para_at(paras, dc->fc);
            if (run != NULL && run->in_table) {
                close_inline(&o);
                if (o.cell->len > 0 && o.para->len > 0) g_string_append(o.cell, "<br>");
                g_string_append(o.cell, o.para->str);
                g_string_truncate(o.para, 0);
            } else {
                end_paragraph(&o, run, stis);
            }
            if (o.chars >= o.max_chars) {
                o.truncated = TRUE;
                break;
            }
            continue;
        }
        if (c == 0x07) {
            const ParaRun* run = para_at(paras, dc->fc);
            if (run != NULL && run->ttp) end_row(&o);
            else end_cell(&o);
            continue;
        }
        if (c == 0x0B) {
            g_string_append(o.para, "<br>");
            continue;
        }
        if (c == 0x09) {
            g_string_append(o.para, " ");
            continue;
        }
        if (c == 0x1E) c = '-';
        if (c == 0xA0) c = ' ';
        if (c < 0x20 || c == 0x1F || c == 0xFFFD) continue; /* pictures, footnote marks, optional hyphens */
        set_inline(&o, char_at(runs, dc->fc));
        append_char(&o, c);
    }
    for (guint k = 0; k < o.fields->len; k++) g_string_free(g_array_index(o.fields, Field, k).code, TRUE);
    g_array_free(o.fields, TRUE);
    end_paragraph(&o, NULL, stis);
    if (o.cell->len > 0) end_cell(&o);
    close_table(&o);
    set_lists(&o, 0);
    if (o.truncated) g_string_append(o.html, "<p><i>The document continues; the preview stops here.</i></p>\n");
    g_string_append(o.html, "</body></html>");
    g_string_free(o.para, TRUE);
    g_string_free(o.cell, TRUE);
    return g_string_free(o.html, FALSE);
}

/* --- Text --- */

/* Characters of the main text from the piece table (Clx) */
static gboolean read_pieces(const guint8* word, gsize word_len, const guint8* clx, gsize lcb, guint32 ccp,
                            gsize limit, GArray* out) {
    gsize i = 0;
    while (i < lcb && clx[i] == 0x01) {
        if (i + 3 > lcb) return FALSE;
        i += 3 + u16(clx + i + 1); /* Prc: property modifiers, skipped */
    }
    if (i + 5 > lcb || clx[i] != 0x02) return FALSE;
    guint32 plc_len = u32(clx + i + 1);
    const guint8* plc = clx + i + 5;
    if (plc_len < 4 || plc_len > lcb - i - 5 || (plc_len - 4) % 12 != 0) return FALSE;
    guint n = (plc_len - 4) / 12;
    for (guint k = 0; k < n && out->len < limit; k++) {
        guint32 cp_start = u32(plc + 4 * k), cp_end = u32(plc + 4 * (k + 1));
        if (cp_start >= ccp) break;
        if (cp_end > ccp) cp_end = ccp;
        if (cp_end <= cp_start) continue;
        const guint8* pcd = plc + 4 * (n + 1) + 8 * k;
        guint32 fc = u32(pcd + 2);
        gboolean compressed = (fc & 0x40000000u) != 0;
        guint32 count = cp_end - cp_start;
        if (compressed) {
            fc = (fc & ~0x40000000u) / 2;
            if ((guint64)fc + count > word_len) return FALSE;
            for (guint32 j = 0; j < count && out->len < limit; j++) {
                DocChar dc = {from_cp1252(word[fc + j]), fc + j};
                g_array_append_val(out, dc);
            }
        } else {
            if ((guint64)fc + 2ULL * count > word_len) return FALSE;
            for (guint32 j = 0; j < count && out->len < limit; j++) {
                gunichar c = u16(word + fc + 2 * j);
                /* Surrogate pairs */
                if (c >= 0xD800 && c < 0xDC00 && j + 1 < count) {
                    gunichar lo = u16(word + fc + 2 * (j + 1));
                    if (lo >= 0xDC00 && lo < 0xE000) {
                        DocChar dc = {0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00), fc + 2 * j};
                        g_array_append_val(out, dc);
                        j++;
                        continue;
                    }
                }
                DocChar dc = {c >= 0xD800 && c < 0xE000 ? 0xFFFD : c, fc + 2 * j};
                g_array_append_val(out, dc);
            }
        }
    }
    return TRUE;
}

gchar* doc_reader_to_html(OleReader* ole, const gchar* head, gsize max_text, gboolean* encrypted, GError** error) {
    if (encrypted != NULL) *encrypted = FALSE;
    GBytes* wb = ole_reader_read(ole, "WordDocument", WORD_MAX_STREAM, error);
    if (wb == NULL) return NULL;
    gsize word_len = 0;
    const guint8* word = g_bytes_get_data(wb, &word_len);
    gchar* html = NULL;
    if (word_len < 0x20) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the document header is truncated");
        g_bytes_unref(wb);
        return NULL;
    }
    guint16 ident = u16(word), nfib = u16(word + 2), flags = u16(word + 0x0A);
    if (ident != 0xA5EC && ident != 0xA5DC) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a Word document");
        g_bytes_unref(wb);
        return NULL;
    }
    if (flags & 0x0100) {
        if (encrypted != NULL) *encrypted = TRUE;
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "the document is encrypted");
        g_bytes_unref(wb);
        return NULL;
    }
    gsize limit = max_text * 2 + 1024; /* characters read (field codes are skipped) */
    GArray* chars = g_array_new(FALSE, FALSE, sizeof(DocChar));
    GArray* paras = g_array_new(FALSE, FALSE, sizeof(ParaRun));
    GArray* runs = g_array_new(FALSE, FALSE, sizeof(CharRun));
    GArray* stis = g_array_new(FALSE, TRUE, sizeof(guint16));
    GBytes* tb = NULL;
    gboolean ok = FALSE;
    if (nfib < 0xC0) {
        /* Word 6 / 95: one 8-bit piece from fcMin to fcMac */
        guint32 fc_min = u32(word + 0x18), fc_mac = u32(word + 0x1C);
        if (fc_min < fc_mac && fc_mac <= word_len) {
            for (guint32 fc = fc_min; fc < fc_mac && chars->len < limit; fc++) {
                DocChar dc = {from_cp1252(word[fc]), fc};
                g_array_append_val(chars, dc);
            }
            ok = TRUE;
        }
    } else if (word_len >= 64) {
        /* Word 97+: FibBase, then the counted fibRgW, fibRgLw and fibRgFcLcb */
        guint16 csw = u16(word + 32);
        gsize lw = 32 + 2 + (gsize)csw * 2;
        guint16 cslw = lw + 2 <= word_len ? u16(word + lw) : 0;
        gsize rg_lw = lw + 2;
        gsize fclcb = rg_lw + (gsize)cslw * 4;
        guint16 cb_fclcb = fclcb + 2 <= word_len ? u16(word + fclcb) : 0;
        gsize rg = fclcb + 2;
        if (cslw >= 4 && cb_fclcb >= 68 && rg + 68 * 4 <= word_len) {
            guint32 ccp_text = u32(word + rg_lw + 12);
#define FCLCB(i) u32(word + rg + 4 * (i))
            tb = ole_reader_read(ole, (flags & 0x0200) ? "1Table" : "0Table", TABLE_MAX_STREAM, error);
            if (tb != NULL) {
                gsize table_len = 0;
                const guint8* table = g_bytes_get_data(tb, &table_len);
                guint32 fc_clx = FCLCB(66), lcb_clx = FCLCB(67);
                if ((guint64)fc_clx + lcb_clx <= table_len && lcb_clx > 0) {
                    ok = read_pieces(word, word_len, table + fc_clx, lcb_clx, ccp_text, limit, chars);
                }
                if (!ok && error != NULL && *error == NULL) {
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "damaged piece table");
                }
                guint32 fc_papx = FCLCB(26), lcb_papx = FCLCB(27);
                if ((guint64)fc_papx + lcb_papx <= table_len) {
                    read_fkps(word, word_len, table + fc_papx, lcb_papx, TRUE, paras);
                }
                guint32 fc_chpx = FCLCB(24), lcb_chpx = FCLCB(25);
                if ((guint64)fc_chpx + lcb_chpx <= table_len) {
                    read_fkps(word, word_len, table + fc_chpx, lcb_chpx, FALSE, runs);
                }
                g_array_free(stis, TRUE);
                stis = read_styles(table, table_len, FCLCB(2), FCLCB(3));
            }
#undef FCLCB
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the document header is damaged");
        }
    }
    if (ok) {
        html = render(chars, paras, runs, stis, head, max_text);
    } else if (error != NULL && *error == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the text of the document cannot be found");
    }
    if (tb != NULL) g_bytes_unref(tb);
    g_array_free(chars, TRUE);
    g_array_free(paras, TRUE);
    g_array_free(runs, TRUE);
    g_array_free(stis, TRUE);
    g_bytes_unref(wb);
    return html;
}
