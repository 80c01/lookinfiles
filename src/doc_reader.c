/* Word 97-2003 documents shown like Word (see doc_reader.h) */
#include "doc_reader.h"
#include "doc_writer.h"
#include <gio/gio.h>
#include <string.h>

#define WORD_MAX_STREAM (64 * 1024 * 1024)
#define TABLE_MAX_STREAM (32 * 1024 * 1024)
#define FKP_SIZE 512
#define MAX_STYLE_CHAIN 16
#define MAX_HEADER_CHARS 4096
#define UNSET G_MININT
#define NO_STYLE 0x0FFF
#define LINE_BREAK 0x2028
#define TWIPS_PER_PT 20.0

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

/* Colours of the ico index (sprmCIco, sprmCHighlight); 0 is automatic */
static const guint32 ICO[17] = {
    DOC_COLOR_NONE, 0x000000, 0x0000FF, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0xFFFF00, 0xFFFFFF,
    0x000080, 0x008080, 0x008000, 0x800080, 0x800000, 0x808000, 0x808080, 0xC0C0C0,
};

typedef struct {
    gunichar c;
    guint32 fc;   /* position of the character in the WordDocument stream */
} DocChar;

/* Character properties; flags keep Word's values (0, 1, 0x80 as the
 * style, 0x81 the opposite of the style) */
typedef struct {
    gint bold, italic, underline, strike, caps, hidden;
    gint vert;      /* 0 baseline, 1 superscript, 2 subscript */
    gint size;      /* half-points */
    gint font;      /* index in the font table */
    gint64 color;   /* 0xRRGGBB or DOC_COLOR_NONE; -1 unset */
    gint64 highlight;
    gint istd;      /* character style */
} CharProps;

typedef struct {
    gboolean in_table, ttp;
    gint ilfo, ilvl, outline;
    gint jc, left, right, first, before, after, page_break;  /* twips; UNSET */
} ParaProps;

typedef struct {
    guint32 fc_start, fc_end;
    guint16 istd;
    ParaProps p;
} ParaRun;

typedef struct {
    guint32 fc_start, fc_end;
    CharProps p;
} CharRun;

typedef struct {
    guint16 sti;    /* built-in style id */
    guint16 base;   /* istd of the base style, NO_STYLE: none */
    ParaProps pp;
    CharProps cp;
} Style;

static void char_props_init(CharProps* p) {
    p->bold = p->italic = p->underline = p->strike = p->caps = p->hidden = UNSET;
    p->vert = p->size = p->font = p->istd = UNSET;
    p->color = p->highlight = -1;
}

static void para_props_init(ParaProps* p) {
    memset(p, 0, sizeof *p);
    p->outline = 9;
    p->jc = p->left = p->right = p->first = p->before = p->after = p->page_break = UNSET;
}

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
    ParaProps* r = data;
    gint v = size >= 2 ? (gint16)u16(op) : 0;
    switch (sprm) {
        case 0x2416: r->in_table = op[0] != 0; break;
        case 0x2417: r->ttp = op[0] != 0; break;
        case 0x460B: if (size >= 2) r->ilfo = v; break;
        case 0x260A: r->ilvl = op[0]; break;
        case 0x2640: r->outline = op[0]; break;
        case 0x6649: if (size >= 4 && u32(op) > 0) r->in_table = TRUE; break;
        case 0x2461: case 0x2403: r->jc = op[0]; break;
        case 0x845E: case 0x840F: if (size >= 2) r->left = v; break;
        case 0x845D: case 0x840E: if (size >= 2) r->right = v; break;
        case 0x8460: case 0x8411: if (size >= 2) r->first = v; break;
        case 0xA413: if (size >= 2) r->before = (gint)u16(op); break;
        case 0xA414: if (size >= 2) r->after = (gint)u16(op); break;
        case 0x2407: r->page_break = op[0] != 0; break;
        default: break;
    }
}

static void char_sprm(guint16 sprm, const guint8* op, gint size, gpointer data) {
    CharProps* r = data;
    switch (sprm) {
        case 0x0835: r->bold = op[0]; break;
        case 0x0836: r->italic = op[0]; break;
        case 0x0837: case 0x2A53: r->strike = op[0]; break;
        case 0x083A: case 0x083B: r->caps = op[0]; break;
        case 0x083C: r->hidden = op[0]; break;
        case 0x2A3E: r->underline = op[0] != 0; break;
        case 0x2A48: r->vert = op[0]; break;
        case 0x4A43: if (size >= 2) r->size = u16(op); break;
        case 0x4A4F: if (size >= 2) r->font = u16(op); break;
        case 0x4A30: if (size >= 2) r->istd = u16(op); break;
        case 0x2A42: r->color = op[0] < 17 ? ICO[op[0]] : DOC_COLOR_NONE; break;
        case 0x2A0C: r->highlight = op[0] < 17 ? ICO[op[0]] : DOC_COLOR_NONE; break;
        case 0x6870:
            if (size >= 4) {
                r->color = op[3] == 0xFF ? DOC_COLOR_NONE : ((guint32)op[0] << 16) | ((guint32)op[1] << 8) | op[2];
            }
            break;
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
                ParaRun r = { fcs, fce, 0, { 0 } };
                para_props_init(&r.p);
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
                        each_sprm(d + 2, len - 2, para_sprm, &r.p);
                    }
                }
                g_array_append_val(out, r);
            } else {
                CharRun r = { fcs, fce, { 0 } };
                char_props_init(&r.p);
                if (b != 0 && (gsize)b * 2 + 1 < FKP_SIZE) {
                    const guint8* cx = page + b * 2;
                    gsize len = cx[0];
                    if ((gsize)(cx - page) + 1 + len <= FKP_SIZE) each_sprm(cx + 1, len, char_sprm, &r.p);
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

/* ---- Styles and fonts ---- */

typedef struct {
    GArray* styles;       /* Style, by istd */
    gint default_font;    /* ftcAsci of the style sheet */
    GPtrArray* fonts;     /* names, by font index */
} Sheet;

/* Style sheet (STSH): each style's base and the formatting of its UPXs */
static void read_styles(Sheet* sheet, const guint8* table, gsize table_len, guint32 fc, guint32 lcb) {
    if (lcb < 4 || (guint64)fc + lcb > table_len) return;
    const guint8* stsh = table + fc;
    guint16 cb_stshi = u16(stsh);
    if ((guint32)cb_stshi + 2 > lcb || cb_stshi < 4) return;
    guint16 cstd = u16(stsh + 2), cb_base = u16(stsh + 4);
    if (cb_stshi >= 14) sheet->default_font = u16(stsh + 2 + 12);
    gsize off = 2 + (gsize)cb_stshi;
    for (guint i = 0; i < cstd && off + 2 <= lcb; i++) {
        guint16 cb = u16(stsh + off);
        off += 2;
        Style st = { 0x0FFE, NO_STYLE, { 0 }, { 0 } };
        para_props_init(&st.pp);
        char_props_init(&st.cp);
        if (cb >= 10 && cb_base >= 10 && off + cb <= lcb) {
            const guint8* std = stsh + off;
            st.sti = u16(std) & 0x0FFF;
            guint stk = u16(std + 2) & 0xF;
            st.base = u16(std + 2) >> 4;
            guint cupx = u16(std + 4) & 0xF;
            /* Name (Xstz: count, UTF-16, terminator), then the UPXs at even offsets */
            gsize p = cb_base;
            if (p + 2 <= cb) p += 2 + 2 * (gsize)u16(std + p) + 2;
            for (guint k = 0; k < cupx; k++) {
                if (p % 2 != 0) p++;
                if (p + 2 > cb) break;
                gsize n = u16(std + p);
                const guint8* upx = std + p + 2;
                if (p + 2 + n > cb) break;
                if (stk == 1 && k == 0 && n >= 2) each_sprm(upx + 2, n - 2, para_sprm, &st.pp);
                else if ((stk == 1 && k == 1) || (stk == 2 && k == 0)) each_sprm(upx, n, char_sprm, &st.cp);
                p += 2 + n;
            }
        }
        g_array_append_val(sheet->styles, st);
        off += cb;
    }
}

/* Font table (SttbfFfn): FFN records, the name after 40 bytes */
static void read_fonts(Sheet* sheet, const guint8* table, gsize table_len, guint32 fc, guint32 lcb) {
    if (lcb < 4 || (guint64)fc + lcb > table_len) return;
    const guint8* p = table + fc;
    guint n = u16(p);
    gsize off = 4;
    for (guint i = 0; i < n && off < lcb; i++) {
        gsize size = (gsize)p[off] + 1;
        if (off + size > lcb) break;
        GString* name = g_string_new(NULL);
        for (gsize k = off + 40; k + 1 < off + size; k += 2) {
            gunichar c = u16(p + k);
            if (c == 0) break;
            if (c < 0xD800 || c >= 0xE000) g_string_append_unichar(name, c);
        }
        g_ptr_array_add(sheet->fonts, g_string_free(name, FALSE));
        off += size;
    }
}

static const Style* style_at(const Sheet* sheet, guint istd) {
    return istd < sheet->styles->len ? &g_array_index(sheet->styles, Style, istd) : NULL;
}

/* Styles from the base of the chain to istd */
static GPtrArray* style_chain(const Sheet* sheet, guint istd) {
    GPtrArray* chain = g_ptr_array_new();
    const Style* st = style_at(sheet, istd);
    for (gint i = 0; st != NULL && i < MAX_STYLE_CHAIN; i++) {
        g_ptr_array_insert(chain, 0, (gpointer)st);
        st = st->base != NO_STYLE ? style_at(sheet, st->base) : NULL;
    }
    return chain;
}

static gint merge_flag(gint base, gint v) {
    if (v == UNSET || v == 0x80) return base;
    if (v == 0x81) return !base;
    return v != 0;
}

static void merge_char(CharProps* dst, const CharProps* src) {
    dst->bold = merge_flag(dst->bold, src->bold);
    dst->italic = merge_flag(dst->italic, src->italic);
    dst->underline = merge_flag(dst->underline, src->underline);
    dst->strike = merge_flag(dst->strike, src->strike);
    dst->caps = merge_flag(dst->caps, src->caps);
    dst->hidden = merge_flag(dst->hidden, src->hidden);
    if (src->vert != UNSET) dst->vert = src->vert;
    if (src->size != UNSET) dst->size = src->size;
    if (src->font != UNSET) dst->font = src->font;
    if (src->color >= 0) dst->color = src->color;
    if (src->highlight >= 0) dst->highlight = src->highlight;
}

static void merge_para(ParaProps* dst, const ParaProps* src) {
    if (src->jc != UNSET) dst->jc = src->jc;
    if (src->left != UNSET) dst->left = src->left;
    if (src->right != UNSET) dst->right = src->right;
    if (src->first != UNSET) dst->first = src->first;
    if (src->before != UNSET) dst->before = src->before;
    if (src->after != UNSET) dst->after = src->after;
    if (src->page_break != UNSET) dst->page_break = src->page_break;
}

/* Character properties of a run in a paragraph of style istd */
static void resolve_char(const Sheet* sheet, guint istd, const CharProps* direct, CharProps* out) {
    char_props_init(out);
    out->bold = out->italic = out->underline = out->strike = out->caps = out->hidden = 0;
    out->vert = 0;
    out->size = 20;
    out->font = sheet->default_font;
    out->color = out->highlight = DOC_COLOR_NONE;
    GPtrArray* chain = style_chain(sheet, istd);
    gboolean styled = FALSE;
    guint16 sti = 0x0FFE;
    for (guint i = 0; i < chain->len; i++) {
        const Style* st = g_ptr_array_index(chain, i);
        merge_char(out, &st->cp);
        styled |= st->cp.size != UNSET || st->cp.bold != UNSET;
        sti = st->sti;
    }
    g_ptr_array_free(chain, TRUE);
    /* A heading style without formatting of its own: Word's default look */
    if (!styled && sti >= 1 && sti <= 3) {
        static const gint SIZES[] = { 0, 32, 26, 24 };
        out->size = MAX(out->size, SIZES[sti]);
        out->bold = TRUE;
    }
    if (direct != NULL && direct->istd != UNSET) {
        chain = style_chain(sheet, (guint)direct->istd);
        for (guint i = 0; i < chain->len; i++) {
            merge_char(out, &((const Style*)g_ptr_array_index(chain, i))->cp);
        }
        g_ptr_array_free(chain, TRUE);
    }
    if (direct != NULL) merge_char(out, direct);
}

static void resolve_para(const Sheet* sheet, const ParaRun* run, ParaProps* out) {
    para_props_init(out);
    if (run == NULL) return;
    GPtrArray* chain = style_chain(sheet, run->istd);
    for (guint i = 0; i < chain->len; i++) {
        merge_para(out, &((const Style*)g_ptr_array_index(chain, i))->pp);
    }
    g_ptr_array_free(chain, TRUE);
    merge_para(out, &run->p);
    out->in_table = run->p.in_table;
    out->ttp = run->p.ttp;
    out->ilfo = run->p.ilfo;
    out->ilvl = run->p.ilvl;
}

static gdouble twips(gint v) {
    return v == UNSET ? 0.0 : v / TWIPS_PER_PT;
}

/* ---- Output ---- */

typedef struct {
    gboolean in_code;
    GString* code;
    gboolean link;
} Field;

typedef struct {
    DocWriter* w;
    const Sheet* sheet;
    GArray* paras;
    GArray* runs;
    gsize max_chars;
    gboolean truncated;
    gboolean table_open, row_open, cell_open;
    gboolean link;
    GString* pending;     /* text of the current run */
    CharProps pending_props;
    gboolean has_pending;
    const ParaRun* last_para;  /* cache of the last resolved properties */
    const CharRun* last_run;
    CharProps last_props;
    gboolean has_last;
} Out;

static void run_from_props(const Out* o, const CharProps* p, DocRun* run) {
    doc_run_init(run);
    run->bold = p->bold == 1;
    run->italic = p->italic == 1;
    run->underline = p->underline == 1;
    run->strike = p->strike == 1;
    run->caps = p->caps == 1;
    run->vert = p->vert == 1 ? 1 : p->vert == 2 ? -1 : 0;
    run->size = p->size > 0 ? p->size / 2.0 : 0;
    run->font = p->font >= 0 && (guint)p->font < o->sheet->fonts->len ? g_ptr_array_index(o->sheet->fonts, p->font)
                                                                       : NULL;
    run->color = p->color >= 0 ? (guint32)p->color : DOC_COLOR_NONE;
    run->highlight = p->highlight >= 0 ? (guint32)p->highlight : DOC_COLOR_NONE;
}

static void flush(Out* o) {
    if (!o->has_pending) return;
    if (o->pending->len > 0) {
        DocRun run;
        run_from_props(o, &o->pending_props, &run);
        run.link = o->link;
        doc_writer_text(o->w, o->pending->str, (gssize)o->pending->len, &run);
    }
    g_string_truncate(o->pending, 0);
    o->has_pending = FALSE;
}

/* Table structure for a paragraph in or out of a table */
static void ensure_structure(Out* o, gboolean in_table) {
    if (!in_table) {
        if (o->table_open) {
            doc_writer_table_end(o->w);
            o->table_open = o->row_open = o->cell_open = FALSE;
        }
        return;
    }
    if (!o->table_open) {
        doc_writer_table_start(o->w);
        o->table_open = TRUE;
    }
    if (!o->row_open) {
        doc_writer_row_start(o->w, FALSE);
        o->row_open = TRUE;
    }
    if (!o->cell_open) {
        doc_writer_cell_start(o->w, 1);
        o->cell_open = TRUE;
    }
}

static gboolean same_props(const CharProps* a, const CharProps* b) {
    return a->bold == b->bold && a->italic == b->italic && a->underline == b->underline && a->strike == b->strike &&
           a->caps == b->caps && a->hidden == b->hidden && a->vert == b->vert && a->size == b->size &&
           a->font == b->font && a->color == b->color && a->highlight == b->highlight;
}

static void append_char(Out* o, const DocChar* dc, gunichar c, const ParaProps* para) {
    ensure_structure(o, para->in_table);
    const CharRun* run = char_at(o->runs, dc->fc);
    const ParaRun* pr = para_at(o->paras, dc->fc);
    if (!o->has_last || pr != o->last_para || run != o->last_run) {
        resolve_char(o->sheet, pr != NULL ? pr->istd : 0, run != NULL ? &run->p : NULL, &o->last_props);
        o->last_para = pr;
        o->last_run = run;
        o->has_last = TRUE;
    }
    CharProps props = o->last_props;
    if (props.hidden == 1) return;
    if (o->has_pending && !same_props(&props, &o->pending_props)) flush(o);
    o->pending_props = props;
    o->has_pending = TRUE;
    g_string_append_unichar(o->pending, c);
}

static void end_paragraph(Out* o, const ParaProps* p) {
    flush(o);
    ensure_structure(o, p->in_table);
    DocPara para;
    doc_para_init(&para);
    para.align = p->jc == 1 ? PREVIEW_ALIGN_CENTER : p->jc == 2 ? PREVIEW_ALIGN_RIGHT
               : (p->jc == 3 || p->jc == 4) ? PREVIEW_ALIGN_JUSTIFY : PREVIEW_ALIGN_LEFT;
    para.left = twips(p->left);
    para.right = twips(p->right);
    para.first = twips(p->first);
    para.before = twips(p->before);
    para.after = twips(p->after);
    const gchar* label = NULL;
    if (p->ilfo > 0) {
        label = "•";
        if (p->left == UNSET) {
            para.left = 18.0 * (MIN(p->ilvl, 8) + 1);
            para.first = -18.0;
        }
    }
    doc_writer_end_paragraph(o->w, &para, label, NULL);
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

static void render(Out* o, GArray* chars) {
    GArray* fields = g_array_new(FALSE, FALSE, sizeof(Field));
    ParaProps para;
    para_props_init(&para);
    const ParaRun* cur = NULL;
    gboolean have_para = FALSE;
    for (guint k = 0; k < chars->len; k++) {
        const DocChar* dc = &g_array_index(chars, DocChar, k);
        gunichar c = dc->c;
        const ParaRun* pr = para_at(o->paras, dc->fc);
        if (!have_para || pr != cur) {
            cur = pr;
            resolve_para(o->sheet, pr, &para);
            have_para = TRUE;
        }
        Field* top = fields->len > 0 ? &g_array_index(fields, Field, fields->len - 1) : NULL;
        if (c == 0x13) {
            Field f = { TRUE, g_string_new(NULL), FALSE };
            g_array_append_val(fields, f);
            continue;
        }
        if (c == 0x14 && top != NULL) {
            top->in_code = FALSE;
            gchar* url = hyperlink_target(top->code->str);
            if (url != NULL) {
                flush(o);
                o->link = TRUE;
                top->link = TRUE;
                g_free(url);
            }
            continue;
        }
        if (c == 0x15 && top != NULL) {
            if (top->link) {
                flush(o);
                o->link = FALSE;
            }
            g_string_free(top->code, TRUE);
            g_array_set_size(fields, fields->len - 1);
            continue;
        }
        if (top != NULL && top->in_code) {
            g_string_append_unichar(top->code, c);
            continue;
        }
        if (c == 0x07 && para.ttp) {
            /* End of a row */
            flush(o);
            if (o->row_open) doc_writer_row_end(o->w);
            o->row_open = o->cell_open = FALSE;
            continue;
        }
        if (c == 0x0D || c == 0x07) {
            end_paragraph(o, &para);
            if (c == 0x07 && o->cell_open) {
                doc_writer_cell_end(o->w);
                o->cell_open = FALSE;
            }
            if (doc_writer_length(o->w) >= o->max_chars) {
                o->truncated = TRUE;
                break;
            }
            continue;
        }
        if (c == 0x0C) {
            end_paragraph(o, &para);
            if (!para.in_table) doc_writer_page_break(o->w);
            continue;
        }
        if (c == 0x0B) c = LINE_BREAK;
        if (c == 0x1E) c = '-';
        if ((c < 0x20 && c != 0x09) || c == 0x1F || c == 0xFFFD) continue; /* pictures, footnote marks, optional hyphens */
        if (para.page_break == 1 && !o->has_pending && !para.in_table) {
            doc_writer_page_break(o->w);
            para.page_break = 0;
        }
        append_char(o, dc, c, &para);
    }
    for (guint k = 0; k < fields->len; k++) g_string_free(g_array_index(fields, Field, k).code, TRUE);
    g_array_free(fields, TRUE);
    flush(o);
    ensure_structure(o, FALSE);
}

/* ---- Text ---- */

/* Characters of the CP range [cp_from, cp_to) from the piece table (Clx) */
static gboolean read_pieces(const guint8* word, gsize word_len, const guint8* clx, gsize lcb, guint32 cp_from,
                            guint32 cp_to, gsize limit, GArray* out) {
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
        if (cp_start >= cp_to) break;
        if (cp_end > cp_to) cp_end = cp_to;
        guint32 skip = cp_from > cp_start ? cp_from - cp_start : 0;
        if (cp_end <= cp_start + skip) continue;
        const guint8* pcd = plc + 4 * (n + 1) + 8 * k;
        guint32 fc = u32(pcd + 2);
        gboolean compressed = (fc & 0x40000000u) != 0;
        guint32 count = cp_end - cp_start;
        if (compressed) {
            fc = (fc & ~0x40000000u) / 2;
            if ((guint64)fc + count > word_len) return FALSE;
            for (guint32 j = skip; j < count && out->len < limit; j++) {
                DocChar dc = { from_cp1252(word[fc + j]), fc + j };
                g_array_append_val(out, dc);
            }
        } else {
            if ((guint64)fc + 2ULL * count > word_len) return FALSE;
            for (guint32 j = skip; j < count && out->len < limit; j++) {
                gunichar c = u16(word + fc + 2 * j);
                /* Surrogate pairs */
                if (c >= 0xD800 && c < 0xDC00 && j + 1 < count) {
                    gunichar lo = u16(word + fc + 2 * (j + 1));
                    if (lo >= 0xDC00 && lo < 0xE000) {
                        DocChar dc = { 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00), fc + 2 * j };
                        g_array_append_val(out, dc);
                        j++;
                        continue;
                    }
                }
                DocChar dc = { c >= 0xD800 && c < 0xE000 ? 0xFFFD : c, fc + 2 * j };
                g_array_append_val(out, dc);
            }
        }
    }
    return TRUE;
}

typedef struct {
    const guint8* word;
    gsize word_len;
    const guint8* table;
    gsize table_len;
    guint32 fc_clx, lcb_clx;
    Sheet sheet;
    GArray* paras;
    GArray* runs;
} Doc;

/* A story (header, footer) written alone; NULL when it is empty */
static PreviewDoc* render_story(Doc* doc, guint32 cp_from, guint32 cp_to, const gchar* font, gdouble size) {
    if (cp_to <= cp_from + 1) return NULL;
    GArray* chars = g_array_new(FALSE, FALSE, sizeof(DocChar));
    read_pieces(doc->word, doc->word_len, doc->table + doc->fc_clx, doc->lcb_clx, cp_from, cp_to, MAX_HEADER_CHARS,
                chars);
    Out o = { doc_writer_new(font, size), &doc->sheet, doc->paras, doc->runs, MAX_HEADER_CHARS, FALSE,
              FALSE, FALSE, FALSE, FALSE, g_string_new(NULL), { 0 }, FALSE, NULL, NULL, { 0 }, FALSE };
    render(&o, chars);
    g_string_free(o.pending, TRUE);
    g_array_free(chars, TRUE);
    return doc_writer_finish(o.w);
}

PreviewDoc* doc_reader_preview(OleReader* ole, const gchar* title, const gchar* subtitle, gsize max_text,
                               gboolean* encrypted, GError** error) {
    if (encrypted != NULL) *encrypted = FALSE;
    GBytes* wb = ole_reader_read(ole, "WordDocument", WORD_MAX_STREAM, error);
    if (wb == NULL) return NULL;
    Doc doc = { 0 };
    doc.word = g_bytes_get_data(wb, &doc.word_len);
    if (doc.word_len < 0x20) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the document header is truncated");
        g_bytes_unref(wb);
        return NULL;
    }
    const guint8* word = doc.word;
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
    doc.paras = g_array_new(FALSE, FALSE, sizeof(ParaRun));
    doc.runs = g_array_new(FALSE, FALSE, sizeof(CharRun));
    doc.sheet.styles = g_array_new(FALSE, TRUE, sizeof(Style));
    doc.sheet.fonts = g_ptr_array_new_with_free_func(g_free);
    doc.sheet.default_font = UNSET;
    GBytes* tb = NULL;
    gboolean ok = FALSE;
    guint32 ccp_text = 0, ccp_ftn = 0, ccp_hdd = 0, fc_hdd = 0, lcb_hdd = 0;
    if (nfib < 0xC0) {
        /* Word 6 / 95: one 8-bit piece from fcMin to fcMac */
        guint32 fc_min = u32(word + 0x18), fc_mac = u32(word + 0x1C);
        if (fc_min < fc_mac && fc_mac <= doc.word_len) {
            for (guint32 fc = fc_min; fc < fc_mac && chars->len < limit; fc++) {
                DocChar dc = { from_cp1252(word[fc]), fc };
                g_array_append_val(chars, dc);
            }
            ok = TRUE;
        }
    } else if (doc.word_len >= 64) {
        /* Word 97+: FibBase, then the counted fibRgW, fibRgLw and fibRgFcLcb */
        guint16 csw = u16(word + 32);
        gsize lw = 32 + 2 + (gsize)csw * 2;
        guint16 cslw = lw + 2 <= doc.word_len ? u16(word + lw) : 0;
        gsize rg_lw = lw + 2;
        gsize fclcb = rg_lw + (gsize)cslw * 4;
        guint16 cb_fclcb = fclcb + 2 <= doc.word_len ? u16(word + fclcb) : 0;
        gsize rg = fclcb + 2;
        if (cslw >= 4 && cb_fclcb >= 68 && rg + 68 * 4 <= doc.word_len) {
            ccp_text = u32(word + rg_lw + 12);
            if (cslw >= 6) {
                ccp_ftn = u32(word + rg_lw + 16);
                ccp_hdd = u32(word + rg_lw + 20);
            }
#define FCLCB(i) u32(word + rg + 4 * (i))
            tb = ole_reader_read(ole, (flags & 0x0200) ? "1Table" : "0Table", TABLE_MAX_STREAM, error);
            if (tb != NULL) {
                doc.table = g_bytes_get_data(tb, &doc.table_len);
                doc.fc_clx = FCLCB(66);
                doc.lcb_clx = FCLCB(67);
                if ((guint64)doc.fc_clx + doc.lcb_clx <= doc.table_len && doc.lcb_clx > 0) {
                    ok = read_pieces(word, doc.word_len, doc.table + doc.fc_clx, doc.lcb_clx, 0, ccp_text, limit,
                                     chars);
                } else {
                    doc.lcb_clx = 0;
                }
                if (!ok && error != NULL && *error == NULL) {
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "damaged piece table");
                }
                guint32 fc_papx = FCLCB(26), lcb_papx = FCLCB(27);
                if ((guint64)fc_papx + lcb_papx <= doc.table_len) {
                    read_fkps(word, doc.word_len, doc.table + fc_papx, lcb_papx, TRUE, doc.paras);
                }
                guint32 fc_chpx = FCLCB(24), lcb_chpx = FCLCB(25);
                if ((guint64)fc_chpx + lcb_chpx <= doc.table_len) {
                    read_fkps(word, doc.word_len, doc.table + fc_chpx, lcb_chpx, FALSE, doc.runs);
                }
                read_styles(&doc.sheet, doc.table, doc.table_len, FCLCB(2), FCLCB(3));
                read_fonts(&doc.sheet, doc.table, doc.table_len, FCLCB(30), FCLCB(31));
                fc_hdd = FCLCB(22);
                lcb_hdd = FCLCB(23);
            }
#undef FCLCB
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the document header is damaged");
        }
    }
    PreviewDoc* result = NULL;
    if (ok) {
        /* Normal text at the preview's size */
        CharProps normal;
        resolve_char(&doc.sheet, 0, NULL, &normal);
        gdouble size = normal.size > 0 ? normal.size / 2.0 : 10.0;
        const gchar* font = normal.font >= 0 && (guint)normal.font < doc.sheet.fonts->len
                                ? g_ptr_array_index(doc.sheet.fonts, normal.font) : NULL;
        Out o = { doc_writer_new(font, size), &doc.sheet, doc.paras, doc.runs, MAX(max_text, 1), FALSE,
                  FALSE, FALSE, FALSE, FALSE, g_string_new(NULL), { 0 }, FALSE, NULL, NULL, { 0 }, FALSE };
        render(&o, chars);
        g_string_free(o.pending, TRUE);
        PreviewDoc* body = doc_writer_finish(o.w);
        /* Header and footer stories: 6 separators, then even / odd header,
         * even / odd footer, first header / footer of the first section */
        PreviewDoc* header = NULL;
        PreviewDoc* footer = NULL;
        guint32 n_hdd = lcb_hdd >= 4 ? lcb_hdd / 4 : 0;
        if (doc.lcb_clx > 0 && ccp_hdd > 0 && n_hdd >= 11 && (guint64)fc_hdd + lcb_hdd <= doc.table_len) {
            const guint8* plc = doc.table + fc_hdd;
            guint32 base = ccp_text + ccp_ftn;
#define STORY(i) base + MIN(u32(plc + 4 * (i)), ccp_hdd), base + MIN(u32(plc + 4 * ((i) + 1)), ccp_hdd)
            header = render_story(&doc, STORY(7), font, size);
            if (header == NULL && n_hdd >= 12) header = render_story(&doc, STORY(10), font, size);
            footer = render_story(&doc, STORY(9), font, size);
#undef STORY
        }
        result = doc_writer_page(title, subtitle, header, body, footer,
                                 o.truncated ? "The document continues; the preview stops here." : NULL);
        preview_doc_free(body);
        if (header != NULL) preview_doc_free(header);
        if (footer != NULL) preview_doc_free(footer);
    } else if (error != NULL && *error == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the text of the document cannot be found");
    }
    if (tb != NULL) g_bytes_unref(tb);
    g_array_free(chars, TRUE);
    g_array_free(doc.paras, TRUE);
    g_array_free(doc.runs, TRUE);
    g_array_free(doc.sheet.styles, TRUE);
    g_ptr_array_free(doc.sheet.fonts, TRUE);
    g_bytes_unref(wb);
    return result;
}
