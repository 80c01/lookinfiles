/* PowerPoint 97-2003 presentations read into slides (see ppt_reader.h) */
#include "ppt_reader.h"
#include "core/i18n.h"
#include "preview_doc.h"
#include <gio/gio.h>
#include <string.h>

#define PPT_MAX_STREAM (64 * 1024 * 1024)
#define PICTURES_MAX_STREAM (64 * 1024 * 1024)
#define MAX_DEPTH 16
#define MAX_SLIDES 2000
#define MASTER_PER_PT 8.0      /* master units: 576 per inch */
#define EMU_PER_PT 12700.0
#define LEVELS 5
#define TEXT_TYPES 9

#define RT_DOCUMENT 0x03E8
#define RT_DOCUMENT_ATOM 0x03E9
#define RT_SLIDE 0x03EE
#define RT_SLIDE_ATOM 0x03EF
#define RT_NOTES 0x03F0
#define RT_ENVIRONMENT 0x03F2
#define RT_SLIDE_PERSIST 0x03F3
#define RT_MAIN_MASTER 0x03F8
#define RT_PPDRAWING_GROUP 0x040B
#define RT_PPDRAWING 0x040C
#define RT_COLOR_SCHEME 0x07F0
#define RT_FONT_COLLECTION 0x07D5
#define RT_FONT_ENTITY 0x0FB7
#define RT_OUTLINE_TEXT_REF 0x0F9E
#define RT_TEXT_HEADER 0x0F9F
#define RT_TEXT_CHARS 0x0FA0
#define RT_STYLE_TEXT_PROP 0x0FA1
#define RT_TX_MASTER_STYLE 0x0FA3
#define RT_TEXT_BYTES 0x0FA8
#define RT_SLIDE_LIST_WITH_TEXT 0x0FF0
#define RT_USER_EDIT 0x0FF5
#define RT_PERSIST_DIRECTORY 0x1772
#define RT_PLACEHOLDER 0x0BC3
#define RT_DOCUMENT_ENCRYPTION 0x2F14

#define ART_DGG 0xF000
#define ART_BSTORE 0xF001
#define ART_DG 0xF002
#define ART_SPGR 0xF003
#define ART_SP 0xF004
#define ART_FBSE 0xF007
#define ART_FSPGR 0xF009
#define ART_FSP 0xF00A
#define ART_OPT 0xF00B
#define ART_TEXTBOX 0xF00D
#define ART_CHILD_ANCHOR 0xF00F
#define ART_CLIENT_ANCHOR 0xF010
#define ART_CLIENT_DATA 0xF011

/* Text types of TextHeaderAtom */
#define TT_TITLE 0
#define TT_BODY 1
#define TT_NOTES 2
#define TT_OTHER 4
#define TT_CENTER_BODY 5
#define TT_CENTER_TITLE 6

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static const gunichar CP1252[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

/* Record header at p of [p, end): FALSE when it does not fit */
typedef struct {
    guint ver, inst, type;
    gsize body, len;     /* offset and length of the body */
} Rec;

static gboolean rec_at(const guint8* d, gsize p, gsize end, Rec* r) {
    if (p + 8 > end) return FALSE;
    guint16 vi = u16(d + p);
    r->ver = vi & 0xF;
    r->inst = vi >> 4;
    r->type = u16(d + p + 2);
    r->len = u32(d + p + 4);
    r->body = p + 8;
    return r->len <= end - p - 8;
}

/* ---- Text styles: TextPFException / TextCFException ---- */

typedef struct {
    guint32 masks;
    guint16 bullet_flags;
    gunichar bullet_char;
    guint16 bullet_font;
    gint16 bullet_size;
    guint32 bullet_color;
    guint16 align;
    gint16 line_spacing, space_before, space_after;
    gint16 left, indent;
} Pf;

typedef struct {
    guint32 masks;
    guint16 style;
    guint16 font;
    guint16 size;
    guint32 color;
    gint16 position;
} Cf;

/* Bytes read, -1 when it does not fit */
static gint parse_pf(const guint8* p, gsize len, Pf* pf) {
    memset(pf, 0, sizeof *pf);
    if (len < 4) return -1;
    guint32 m = pf->masks = u32(p);
    gsize i = 4;
#define NEED(n) do { if (i + (n) > len) return -1; } while (0)
    if (m & 0x0F) { NEED(2); pf->bullet_flags = u16(p + i); i += 2; }
    if (m & 0x80) { NEED(2); pf->bullet_char = u16(p + i); i += 2; }
    if (m & 0x10) { NEED(2); pf->bullet_font = u16(p + i); i += 2; }
    if (m & 0x40) { NEED(2); pf->bullet_size = (gint16)u16(p + i); i += 2; }
    if (m & 0x20) { NEED(4); pf->bullet_color = u32(p + i); i += 4; }
    if (m & 0x800) { NEED(2); pf->align = u16(p + i); i += 2; }
    if (m & 0x1000) { NEED(2); pf->line_spacing = (gint16)u16(p + i); i += 2; }
    if (m & 0x2000) { NEED(2); pf->space_before = (gint16)u16(p + i); i += 2; }
    if (m & 0x4000) { NEED(2); pf->space_after = (gint16)u16(p + i); i += 2; }
    if (m & 0x100) { NEED(2); pf->left = (gint16)u16(p + i); i += 2; }
    if (m & 0x400) { NEED(2); pf->indent = (gint16)u16(p + i); i += 2; }
    if (m & 0x8000) { NEED(2); i += 2; }
    if (m & 0x100000) { NEED(2); i += 2 + 4 * (gsize)u16(p + i); NEED(0); }
    if (m & 0x10000) { NEED(2); i += 2; }
    if (m & 0xE0000) { NEED(2); i += 2; }
    if (m & 0x200000) { NEED(2); i += 2; }
#undef NEED
    return (gint)i;
}

static gint parse_cf(const guint8* p, gsize len, Cf* cf) {
    memset(cf, 0, sizeof *cf);
    if (len < 4) return -1;
    guint32 m = cf->masks = u32(p);
    gsize i = 4;
#define NEED(n) do { if (i + (n) > len) return -1; } while (0)
    if (m & 0x3EB7) { NEED(2); cf->style = u16(p + i); i += 2; }
    if (m & 0x10000) { NEED(2); cf->font = u16(p + i); i += 2; }
    if (m & 0x200000) { NEED(2); i += 2; }
    if (m & 0x400000) { NEED(2); i += 2; }
    if (m & 0x800000) { NEED(2); i += 2; }
    if (m & 0x20000) { NEED(2); cf->size = u16(p + i); i += 2; }
    if (m & 0x40000) { NEED(4); cf->color = u32(p + i); i += 4; }
    if (m & 0x80000) { NEED(2); cf->position = (gint16)u16(p + i); i += 2; }
    if (m & 0x100000) { NEED(4); i += 4; }
    if (m & 0x1000000) { NEED(2); i += 2; }
    if (m & 0x2000000) { NEED(2); i += 2; }
    if (m & 0x4000000) { NEED(4); i += 4; }
#undef NEED
    return (gint)i;
}

/* Effective style of a level */
typedef struct {
    gboolean bullet;
    gunichar bullet_char;
    guint32 bullet_color;   /* raw ColorIndexStruct, 0: the text's */
    gint align;
    gdouble left, indent;   /* points */
    gdouble size;
    gboolean bold, italic, underline;
    guint32 color;          /* raw ColorIndexStruct */
    gint font;              /* index in the font collection, -1 */
} Look;

static void apply_pf(Look* l, const Pf* pf) {
    guint32 m = pf->masks;
    if (m & 0x01) l->bullet = (pf->bullet_flags & 1) != 0;
    if ((m & 0x80) && pf->bullet_char != 0) l->bullet_char = pf->bullet_char;
    if ((m & 0x04) && (pf->bullet_flags & 4) && (m & 0x20)) l->bullet_color = pf->bullet_color;
    if (m & 0x800) l->align = pf->align;
    if (m & 0x100) l->left = pf->left / MASTER_PER_PT;
    if (m & 0x400) l->indent = pf->indent / MASTER_PER_PT;
}

static void apply_cf(Look* l, const Cf* cf) {
    guint32 m = cf->masks;
    if (m & 0x01) l->bold = (cf->style & 0x01) != 0;
    if (m & 0x02) l->italic = (cf->style & 0x02) != 0;
    if (m & 0x04) l->underline = (cf->style & 0x04) != 0;
    if ((m & 0x20000) && cf->size > 0) l->size = cf->size;
    if (m & 0x40000) l->color = cf->color | 0x80000000u; /* high bit: set */
    if (m & 0x10000) l->font = cf->font;
}

/* ---- Presentation ---- */

typedef struct {
    gint type;
    GArray* chars;        /* gunichar, CR between paragraphs */
    const guint8* style;  /* StyleTextPropAtom body, NULL if none */
    gsize style_len;
} TextBlock;

static void block_clear(gpointer p) {
    TextBlock* b = p;
    g_array_free(b->chars, TRUE);
}

typedef struct {
    const guint8* d;
    gsize len;
    gboolean encrypted;
    gdouble width, height;
    GPtrArray* fonts;          /* names */
    guint32 master_scheme[8];
    gboolean has_master_scheme;
    Look styles[TEXT_TYPES][LEVELS];
    GHashTable* persist;       /* persist id -> stream offset */
    GArray* slide_refs;        /* persist ids of the slides, in order (SLWT 0) */
    GPtrArray* slide_texts;    /* GArray of TextBlock per slide */
    GArray* notes_refs;        /* SLWT 2 */
    gsize master_offset;       /* main master container, 0: none */
    GArray* slide_offsets;     /* slide containers in stream order (fallback) */
    GArray* notes_offsets;
    /* Pictures */
    GPtrArray* blip_offsets;   /* foDelay of each BStore entry (as pointers) */
    GBytes* pictures;
} Ppt;

static gchar* font_name(const Ppt* ppt, gint font) {
    return font >= 0 && (guint)font < ppt->fonts->len ? g_strdup(g_ptr_array_index(ppt->fonts, font)) : NULL;
}

/* Text of a TextCharsAtom / TextBytesAtom */
static GArray* atom_chars(guint type, const guint8* d, gsize len) {
    GArray* out = g_array_new(FALSE, FALSE, sizeof(gunichar));
    gsize n = type == RT_TEXT_CHARS ? len / 2 : len;
    for (gsize i = 0; i < n; i++) {
        gunichar c = type == RT_TEXT_CHARS ? u16(d + 2 * i) : d[i];
        if (type == RT_TEXT_BYTES && c >= 0x80 && c < 0xA0) c = CP1252[c - 0x80];
        if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        g_array_append_val(out, c);
    }
    return out;
}

static void default_styles(Ppt* ppt) {
    static const gdouble BODY[LEVELS] = { 32, 28, 24, 20, 20 };
    for (gint t = 0; t < TEXT_TYPES; t++) {
        for (gint l = 0; l < LEVELS; l++) {
            Look* look = &ppt->styles[t][l];
            memset(look, 0, sizeof *look);
            look->font = 0;
            look->bullet_char = 0x2022;
            gboolean title = t == TT_TITLE || t == TT_CENTER_TITLE;
            gboolean body = t == TT_BODY || t == TT_CENTER_BODY || t == 7 || t == 8;
            look->size = title ? 44 : body ? BODY[l] : t == TT_NOTES ? 12 : 18;
            look->bullet = t == TT_BODY && TRUE;
            look->align = t == TT_CENTER_TITLE || t == TT_CENTER_BODY ? 1 : 0;
            look->left = body ? 27.0 * (l + 1) : 0;
            look->indent = body ? 27.0 * l : 0;
            look->color = 0x80000000u | (title ? 0x03000000u : 0x01000000u); /* scheme: title text, text */
        }
    }
}

/* TxMasterStyleAtom: the levels of one text type */
static void read_master_style(Ppt* ppt, guint inst, const guint8* d, gsize len) {
    if (inst >= TEXT_TYPES || len < 2) return;
    guint levels = MIN(u16(d), LEVELS);
    gsize i = 2;
    for (guint l = 0; l < levels; l++) {
        if (inst >= 5) {
            if (i + 2 > len) return;
            i += 2;
        }
        Pf pf;
        Cf cf;
        gint n = parse_pf(d + i, len - i, &pf);
        if (n < 0) return;
        i += (gsize)n;
        n = parse_cf(d + i, len - i, &cf);
        if (n < 0) return;
        i += (gsize)n;
        /* Each level starts from the one above it */
        if (l > 0) ppt->styles[inst][l] = ppt->styles[inst][l - 1];
        apply_pf(&ppt->styles[inst][l], &pf);
        apply_cf(&ppt->styles[inst][l], &cf);
    }
    /* The variants without their own styles follow title and body */
    if (inst == TT_TITLE) memcpy(ppt->styles[TT_CENTER_TITLE], ppt->styles[TT_TITLE], sizeof ppt->styles[0]);
    if (inst == TT_BODY) {
        for (gint t = 7; t <= 8; t++) memcpy(ppt->styles[t], ppt->styles[TT_BODY], sizeof ppt->styles[0]);
    }
}

static void read_scheme(const guint8* d, gsize len, guint32 out[8]) {
    for (gint i = 0; i < 8 && (gsize)(i + 1) * 4 <= len; i++) {
        out[i] = ((guint32)d[4 * i] << 16) | ((guint32)d[4 * i + 1] << 8) | d[4 * i + 2];
    }
}

/* Environment: fonts and default text styles */
static void read_environment(Ppt* ppt, gsize p, gsize end) {
    Rec r;
    for (; rec_at(ppt->d, p, end, &r); p = r.body + r.len) {
        if (r.type == RT_FONT_COLLECTION) {
            Rec f;
            for (gsize q = r.body; rec_at(ppt->d, q, r.body + r.len, &f); q = f.body + f.len) {
                if (f.type != RT_FONT_ENTITY || f.len < 64) continue;
                GString* name = g_string_new(NULL);
                for (gsize k = 0; k < 32; k++) {
                    gunichar c = u16(ppt->d + f.body + 2 * k);
                    if (c == 0) break;
                    if (c < 0xD800 || c >= 0xE000) g_string_append_unichar(name, c);
                }
                g_ptr_array_add(ppt->fonts, g_string_free(name, FALSE));
            }
        } else if (r.type == RT_TX_MASTER_STYLE) {
            read_master_style(ppt, r.inst, ppt->d + r.body, r.len);
        }
    }
}

/* SlideListWithText: persist ids and text blocks of each slide */
static void read_slide_list(Ppt* ppt, gsize p, gsize end, GArray* refs, GPtrArray* texts) {
    Rec r;
    GArray* cur = NULL;
    gint pending = TT_OTHER;
    for (; rec_at(ppt->d, p, end, &r); p = r.body + r.len) {
        const guint8* d = ppt->d + r.body;
        if (r.type == RT_SLIDE_PERSIST && r.len >= 4 && refs->len < MAX_SLIDES) {
            guint32 id = u32(d);
            g_array_append_val(refs, id);
            cur = NULL;
            if (texts != NULL) {
                cur = g_array_new(FALSE, FALSE, sizeof(TextBlock));
                g_array_set_clear_func(cur, block_clear);
                g_ptr_array_add(texts, cur);
            }
            pending = TT_OTHER;
        } else if (r.type == RT_TEXT_HEADER && r.len >= 4) {
            pending = (gint)u32(d);
        } else if ((r.type == RT_TEXT_CHARS || r.type == RT_TEXT_BYTES) && cur != NULL) {
            TextBlock b = { pending, atom_chars(r.type, d, r.len), NULL, 0 };
            g_array_append_val(cur, b);
        } else if (r.type == RT_STYLE_TEXT_PROP && cur != NULL && cur->len > 0) {
            TextBlock* b = &g_array_index(cur, TextBlock, cur->len - 1);
            b->style = d;
            b->style_len = r.len;
        }
    }
}

static void read_document(Ppt* ppt, gsize p, gsize end) {
    Rec r;
    for (; rec_at(ppt->d, p, end, &r); p = r.body + r.len) {
        const guint8* d = ppt->d + r.body;
        if (r.type == RT_DOCUMENT_ATOM && r.len >= 8) {
            gint32 w = (gint32)u32(d), h = (gint32)u32(d + 4);
            if (w > 0 && h > 0) {
                ppt->width = w / MASTER_PER_PT;
                ppt->height = h / MASTER_PER_PT;
            }
        } else if (r.type == RT_ENVIRONMENT) {
            read_environment(ppt, r.body, r.body + r.len);
        } else if (r.type == RT_SLIDE_LIST_WITH_TEXT && r.inst == 0) {
            read_slide_list(ppt, r.body, r.body + r.len, ppt->slide_refs, ppt->slide_texts);
        } else if (r.type == RT_SLIDE_LIST_WITH_TEXT && r.inst == 2) {
            read_slide_list(ppt, r.body, r.body + r.len, ppt->notes_refs, NULL);
        } else if (r.type == RT_DOCUMENT_ENCRYPTION) {
            ppt->encrypted = TRUE;
        } else if (r.type == RT_PPDRAWING_GROUP) {
            /* BStore: where each picture is in the Pictures stream */
            Rec g, b, e;
            for (gsize q = r.body; rec_at(ppt->d, q, r.body + r.len, &g); q = g.body + g.len) {
                if (g.type != ART_DGG) continue;
                for (gsize s = g.body; rec_at(ppt->d, s, g.body + g.len, &b); s = b.body + b.len) {
                    if (b.type != ART_BSTORE) continue;
                    for (gsize t = b.body; rec_at(ppt->d, t, b.body + b.len, &e); t = e.body + e.len) {
                        gsize off = e.type == ART_FBSE && e.len >= 36 ? u32(ppt->d + e.body + 28) : G_MAXUINT32;
                        g_ptr_array_add(ppt->blip_offsets, GSIZE_TO_POINTER(off));
                    }
                }
            }
        }
    }
}

/* Persist directory of the last edit (and older ones for missing ids) */
static void read_persist(Ppt* ppt, OleReader* ole) {
    GBytes* cu = ole_reader_read(ole, "Current User", 4096, NULL);
    if (cu == NULL) return;
    gsize cu_len = 0;
    const guint8* c = g_bytes_get_data(cu, &cu_len);
    gsize edit = cu_len >= 20 ? u32(c + 16) : G_MAXSIZE;
    g_bytes_unref(cu);
    for (gint hops = 0; hops < 64 && edit + 8 + 16 <= ppt->len; hops++) {
        Rec r;
        if (!rec_at(ppt->d, edit, ppt->len, &r) || r.type != RT_USER_EDIT || r.len < 16) break;
        gsize last = u32(ppt->d + r.body + 8), dir = u32(ppt->d + r.body + 12);
        Rec pd;
        if (rec_at(ppt->d, dir, ppt->len, &pd) && pd.type == RT_PERSIST_DIRECTORY) {
            for (gsize i = pd.body; i + 4 <= pd.body + pd.len;) {
                guint32 h = u32(ppt->d + i), id = h & 0xFFFFF, n = h >> 20;
                i += 4;
                for (guint32 k = 0; k < n && i + 4 <= pd.body + pd.len; k++, i += 4) {
                    gpointer key = GUINT_TO_POINTER(id + k);
                    if (!g_hash_table_contains(ppt->persist, key)) {
                        g_hash_table_insert(ppt->persist, key, GSIZE_TO_POINTER((gsize)u32(ppt->d + i)));
                    }
                }
            }
        }
        if (last == 0 || last == edit) break;
        edit = last;
    }
}

/* ---- Pictures ---- */

static GBytes* picture(const Ppt* ppt, guint32 pib) {
    if (pib == 0 || pib > ppt->blip_offsets->len || ppt->pictures == NULL) return NULL;
    gsize off = GPOINTER_TO_SIZE(g_ptr_array_index(ppt->blip_offsets, pib - 1));
    gsize len = 0;
    const guint8* d = g_bytes_get_data(ppt->pictures, &len);
    Rec r;
    if (off >= len || !rec_at(d, off, len, &r)) return NULL;
    gsize uid = (r.inst & 1) ? 32 : 16;
    if (r.type == 0xF01D || r.type == 0xF02A || r.type == 0xF01E || r.type == 0xF029) {
        if (r.len < uid + 1) return NULL;
        return g_bytes_new(d + r.body + uid + 1, r.len - uid - 1);
    }
    if (r.type == 0xF01F && r.len > uid + 1 + 40) {
        /* DIB: a bitmap file without its file header */
        const guint8* dib = d + r.body + uid + 1;
        gsize n = r.len - uid - 1;
        guint32 header = u32(dib), bits = u16(dib + 14), used = u32(dib + 32);
        guint32 colors = used != 0 ? used : bits <= 8 ? 1u << bits : 0;
        guint8 fh[14] = { 'B', 'M' };
        guint32 total = (guint32)(14 + n), offset = 14 + header + 4 * colors;
        memcpy(fh + 2, &(guint32){ GUINT32_TO_LE(total) }, 4);
        memcpy(fh + 10, &(guint32){ GUINT32_TO_LE(offset) }, 4);
        GByteArray* out = g_byte_array_new();
        g_byte_array_append(out, fh, 14);
        g_byte_array_append(out, dib, (guint)n);
        return g_byte_array_free_to_bytes(out);
    }
    return NULL; /* metafiles: not drawn */
}

/* ---- Drawings ---- */

typedef struct {
    gdouble sx, sy, tx, ty;   /* points = t + raw * s */
} Xform;

typedef struct {
    const Ppt* ppt;
    const guint32* scheme;
    GArray* blocks;           /* text of the slide in the slide list, or NULL */
    Slide* slide;
    gboolean background_found;
    gboolean master_background;  /* the slide follows the master's background */
} DrawCtx;

static guint32 art_color(const DrawCtx* c, guint32 v, guint32 def) {
    guint8 flags = (guint8)(v >> 24);
    if (flags & 0x08) return (v & 0xFF) < 8 ? c->scheme[v & 0xFF] : def;
    if (flags & 0x10) return def;
    return ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
}

static guint32 text_color(const DrawCtx* c, guint32 raw) {
    guint idx = (raw >> 24) & 0x7F;
    if (idx == 0x7E) return ((raw & 0xFF) << 16) | (raw & 0xFF00) | ((raw >> 16) & 0xFF);
    return idx < 8 ? c->scheme[idx] : c->scheme[1];
}

typedef struct {
    gboolean present;
    guint32 value;
} Prop;

#define PROPS 0x400

/* Simple properties of an OPT record (complex data skipped) */
static void read_opt(const guint8* d, gsize len, guint count, Prop* props) {
    for (guint i = 0; i < count && (i + 1) * 6 <= len; i++) {
        guint id = u16(d + 6 * i) & 0x3FFF;
        if (id < PROPS) {
            props[id].present = TRUE;
            props[id].value = u32(d + 6 * i + 2);
        }
    }
}

static SlideGeometry art_geometry(guint type) {
    switch (type) {
    case 2: return SLIDE_GEOM_ROUND_RECT;
    case 3: return SLIDE_GEOM_ELLIPSE;
    case 4: return SLIDE_GEOM_DIAMOND;
    case 5: return SLIDE_GEOM_TRIANGLE;
    case 6: return SLIDE_GEOM_RIGHT_TRIANGLE;
    case 7: return SLIDE_GEOM_PARALLELOGRAM;
    case 9: return SLIDE_GEOM_HEXAGON;
    case 13: return SLIDE_GEOM_RIGHT_ARROW;
    case 15: return SLIDE_GEOM_PENTAGON;
    case 20: case 32: return SLIDE_GEOM_LINE;
    case 55: return SLIDE_GEOM_CHEVRON;
    case 66: return SLIDE_GEOM_LEFT_ARROW;
    case 67: return SLIDE_GEOM_DOWN_ARROW;
    case 68: return SLIDE_GEOM_UP_ARROW;
    default: return SLIDE_GEOM_RECT;
    }
}

static gint style_type(gint t) {
    return t >= 0 && t < TEXT_TYPES ? t : TT_OTHER;
}

/* Paragraphs of a text block with its style runs over the master styles */
static void build_text(const DrawCtx* c, const TextBlock* b, GPtrArray* out) {
    const Ppt* ppt = c->ppt;
    gint type = style_type(b->type);
    gsize n = b->chars->len;
    /* Paragraph and character runs of the StyleTextPropAtom */
    GArray* pruns = g_array_new(FALSE, FALSE, sizeof(guint32) * 2 + sizeof(Pf));
    GArray* cruns = g_array_new(FALSE, FALSE, sizeof(guint32) + sizeof(Cf));
    typedef struct { guint32 count; guint32 level; Pf pf; } PRun;
    typedef struct { guint32 count; Cf cf; } CRun;
    if (b->style != NULL) {
        gsize i = 0, covered = 0;
        while (covered <= n && i + 6 <= b->style_len) {
            PRun r = { u32(b->style + i), u16(b->style + i + 4), { 0 } };
            gint k = parse_pf(b->style + i + 6, b->style_len - i - 6, &r.pf);
            if (k < 0 || r.count == 0) break;
            i += 6 + (gsize)k;
            g_array_append_val(pruns, r);
            covered += r.count;
        }
        covered = 0;
        while (covered <= n && i + 4 <= b->style_len) {
            CRun r = { u32(b->style + i), { 0 } };
            gint k = parse_cf(b->style + i + 4, b->style_len - i - 4, &r.cf);
            if (k < 0 || r.count == 0) break;
            i += 4 + (gsize)k;
            g_array_append_val(cruns, r);
            covered += r.count;
        }
    }
    gsize start = 0;
    guint pi = 0, ci = 0;
    gsize p_end = pruns->len > 0 ? ((PRun*)pruns->data)[0].count : G_MAXSIZE;
    gsize c_end = cruns->len > 0 ? ((CRun*)cruns->data)[0].count : G_MAXSIZE;
    while (start <= n) {
        gsize end = start;
        while (end < n && g_array_index(b->chars, gunichar, end) != 0x0D) end++;
        while (start >= p_end && pi + 1 < pruns->len) p_end += ((PRun*)pruns->data)[++pi].count;
        const PRun* pr = pi < pruns->len ? &((PRun*)pruns->data)[pi] : NULL;
        gint level = pr != NULL ? (gint)MIN(pr->level, LEVELS - 1) : 0;
        Look look = ppt->styles[type][level];
        if (pr != NULL) apply_pf(&look, &pr->pf);
        SlideParagraph* para = slide_paragraph_new();
        para->align = look.align == 1 ? SLIDE_ALIGN_CENTER : look.align == 2 ? SLIDE_ALIGN_RIGHT
                    : look.align == 3 ? SLIDE_ALIGN_JUSTIFY : SLIDE_ALIGN_LEFT;
        para->left = look.left;
        para->indent = look.indent - look.left;
        para->size = look.size;
        gboolean has_text = FALSE;
        for (gsize k = start; k < end;) {
            while (k >= c_end && ci + 1 < cruns->len) c_end += ((CRun*)cruns->data)[++ci].count;
            gsize seg_end = MIN(end, c_end);
            if (seg_end <= k) seg_end = end;
            Look rl = look;
            if (ci < cruns->len) apply_cf(&rl, &((CRun*)cruns->data)[ci].cf);
            GString* text = g_string_new(NULL);
            for (gsize j = k; j < seg_end; j++) {
                gunichar ch = g_array_index(b->chars, gunichar, j);
                if (ch == 0x0B) ch = '\n';
                else if ((ch < 0x20 && ch != '\t') || ch == 0xFFFD) continue;
                g_string_append_unichar(text, ch);
            }
            if (text->len > 0) {
                SlideRun* run = slide_paragraph_add_run(para, text->str, (gssize)text->len);
                run->font = font_name(ppt, rl.font);
                run->size = rl.size;
                run->bold = rl.bold;
                run->italic = rl.italic;
                run->underline = rl.underline;
                run->color = text_color(c, rl.color);
                has_text = TRUE;
            }
            g_string_free(text, TRUE);
            k = seg_end;
        }
        if (look.bullet && has_text) {
            gchar utf8[8] = { 0 };
            g_unichar_to_utf8(look.bullet_char != 0 ? look.bullet_char : 0x2022, utf8);
            para->bullet = preview_symbol_text(utf8, NULL);
            if (look.bullet_color != 0) para->bullet_color = text_color(c, look.bullet_color | 0x80000000u);
            if (para->indent >= 0) para->indent = -MIN(para->left, 18.0);
        }
        g_ptr_array_add(out, para);
        start = end + 1;
    }
    /* A final empty paragraph (the closing CR) is not shown */
    if (out->len > 1) {
        SlideParagraph* last = g_ptr_array_index(out, out->len - 1);
        if (last->runs->len == 0) g_ptr_array_remove_index(out, out->len - 1);
    }
    g_array_free(pruns, TRUE);
    g_array_free(cruns, TRUE);
}

/* One SpContainer: a shape on the slide */
static void read_shape(DrawCtx* c, gsize p, gsize end, const Xform* xf, gboolean* group_box, Xform* group_out) {
    const Ppt* ppt = c->ppt;
    Rec r;
    guint type = 0;
    guint32 flags = 0;
    Prop props[PROPS] = { { FALSE, 0 } };
    gdouble box[4] = { 0 };
    gboolean anchored = FALSE, placeholder = FALSE;
    gint32 grp[4] = { 0 };
    gboolean has_grp = FALSE;
    TextBlock own = { TT_OTHER, NULL, NULL, 0 };
    const TextBlock* text = NULL;
    for (; rec_at(ppt->d, p, end, &r); p = r.body + r.len) {
        const guint8* d = ppt->d + r.body;
        if (r.type == ART_FSP && r.len >= 8) {
            type = r.inst;
            flags = u32(d + 4);
        } else if (r.type == ART_FSPGR && r.len >= 16) {
            for (gint i = 0; i < 4; i++) grp[i] = (gint32)u32(d + 4 * i);
            has_grp = TRUE;
        } else if (r.type == ART_OPT || r.type == 0xF122) {
            read_opt(d, r.len, r.inst, props);
        } else if (r.type == ART_CLIENT_ANCHOR && (r.len == 8 || r.len >= 16)) {
            /* top, left, right, bottom */
            gdouble v[4];
            for (gint i = 0; i < 4; i++) v[i] = r.len == 8 ? (gint16)u16(d + 2 * i) : (gint32)u32(d + 4 * i);
            box[0] = v[1];
            box[1] = v[0];
            box[2] = v[2] - v[1];
            box[3] = v[3] - v[0];
            anchored = TRUE;
        } else if (r.type == ART_CHILD_ANCHOR && r.len >= 16) {
            gdouble v[4];
            for (gint i = 0; i < 4; i++) v[i] = (gint32)u32(d + 4 * i);
            box[0] = v[0];
            box[1] = v[1];
            box[2] = v[2] - v[0];
            box[3] = v[3] - v[1];
            anchored = TRUE;
        } else if (r.type == ART_CLIENT_DATA) {
            Rec k;
            for (gsize q = r.body; rec_at(ppt->d, q, r.body + r.len, &k); q = k.body + k.len) {
                if (k.type == RT_PLACEHOLDER) placeholder = TRUE;
            }
        } else if (r.type == ART_TEXTBOX) {
            Rec k;
            for (gsize q = r.body; rec_at(ppt->d, q, r.body + r.len, &k); q = k.body + k.len) {
                const guint8* kd = ppt->d + k.body;
                if (k.type == RT_TEXT_HEADER && k.len >= 4) {
                    own.type = (gint)u32(kd);
                } else if ((k.type == RT_TEXT_CHARS || k.type == RT_TEXT_BYTES) && own.chars == NULL) {
                    own.chars = atom_chars(k.type, kd, k.len);
                    text = &own;
                } else if (k.type == RT_STYLE_TEXT_PROP) {
                    own.style = kd;
                    own.style_len = k.len;
                } else if (k.type == RT_OUTLINE_TEXT_REF && k.len >= 4 && c->blocks != NULL &&
                           u32(kd) < c->blocks->len) {
                    text = &g_array_index(c->blocks, TextBlock, u32(kd));
                }
            }
        }
    }
    if ((!anchored && !(flags & 0x400) /* background */) || (flags & 0x08) /* deleted */) {
        if (own.chars != NULL) g_array_free(own.chars, TRUE);
        return;
    }
    gdouble x = xf->tx + box[0] * xf->sx, y = xf->ty + box[1] * xf->sy;
    gdouble w = box[2] * xf->sx, h = box[3] * xf->sy;
    if (has_grp && group_box != NULL) {
        /* The first shape of a group: its child space mapped onto its box */
        gdouble gw = grp[2] - grp[0], gh = grp[3] - grp[1];
        gdouble rx = gw > 0 ? box[2] / gw : 1.0, ry = gh > 0 ? box[3] / gh : 1.0;
        Xform n = { xf->sx * rx, xf->sy * ry, x - grp[0] * xf->sx * rx, y - grp[1] * xf->sy * ry };
        *group_out = n;
        *group_box = TRUE;
        if (own.chars != NULL) g_array_free(own.chars, TRUE);
        return;
    }
    SlideShape* s = slide_shape_new();
    s->x = x;
    s->y = y;
    s->w = w;
    s->h = h;
    s->geometry = art_geometry(type);
    s->flip_h = (flags & 0x40) != 0;
    s->flip_v = (flags & 0x80) != 0;
    if (props[0x0004].present) s->rotation = (gint32)props[0x0004].value / 65536.0;
    /* Fill and outline: explicit ones; autoshapes default to the spec's */
    gboolean autoshape = type != 0 && type != 75 && type != 202 && !placeholder && s->geometry != SLIDE_GEOM_LINE;
    guint32 fb = props[0x01BF].value, lb = props[0x01FF].value;
    gboolean filled = props[0x01BF].present && (fb & 0x100000) ? (fb & 0x10) != 0
                    : props[0x0181].present || autoshape;
    gboolean lined = props[0x01FF].present && (lb & 0x80000) ? (lb & 0x08) != 0
                   : props[0x01C0].present || autoshape || s->geometry == SLIDE_GEOM_LINE;
    /* LibreOffice writes its default drawing style on the text boxes it
     * exports and does not show it: not drawn either */
    if (type == 202 && props[0x0181].value == 0x00CF9F72 && props[0x01C0].value == 0x00A46534) {
        filled = lined = FALSE;
    }
    if (filled) {
        s->fill = art_color(c, props[0x0181].present ? props[0x0181].value : 0x00FFFFFF, 0xFFFFFF);
        guint ft = props[0x0180].value;
        if (ft >= 4 && ft <= 7) {
            s->fill_end = art_color(c, props[0x0183].present ? props[0x0183].value : 0x00FFFFFF, 0xFFFFFF);
            s->gradient_angle = 90;
        }
    }
    if (lined) {
        s->line = art_color(c, props[0x01C0].present ? props[0x01C0].value : 0, 0);
        s->line_width = (props[0x01CB].present ? props[0x01CB].value : 9525) / EMU_PER_PT;
    }
    if (props[0x0104].present) s->picture = picture(ppt, props[0x0104].value);
    s->inset_left = (props[0x0081].present ? props[0x0081].value : 91440) / EMU_PER_PT;
    s->inset_top = (props[0x0082].present ? props[0x0082].value : 45720) / EMU_PER_PT;
    s->inset_right = (props[0x0083].present ? props[0x0083].value : 91440) / EMU_PER_PT;
    s->inset_bottom = (props[0x0084].present ? props[0x0084].value : 45720) / EMU_PER_PT;
    guint anchor = props[0x0087].value;
    s->anchor = anchor == 1 || anchor == 4 || anchor == 7 ? SLIDE_ANCHOR_MIDDLE
              : anchor == 2 || anchor == 5 || anchor == 8 ? SLIDE_ANCHOR_BOTTOM : SLIDE_ANCHOR_TOP;
    s->no_wrap = props[0x0085].present && props[0x0085].value == 2;
    if (text != NULL && text->chars != NULL) {
        build_text(c, text, s->paragraphs);
        if (c->slide->title == NULL && (text->type == TT_TITLE || text->type == TT_CENTER_TITLE)) {
            gchar* t = slide_shape_text(s);
            if (t[0] != '\0') c->slide->title = t;
            else g_free(t);
        }
    }
    if ((flags & 0x400) && c->master_background) {
        slide_shape_free(s);
    } else if (flags & 0x400) {
        /* The background shape */
        if (s->fill != SLIDE_NO_COLOR) {
            c->slide->background = s->fill;
            c->slide->background_end = s->fill_end;
        }
        if (s->picture != NULL) c->slide->background_picture = g_bytes_ref(s->picture);
        else if (props[0x0186].present) c->slide->background_picture = picture(ppt, props[0x0186].value);
        c->background_found = TRUE;
        slide_shape_free(s);
    } else {
        g_ptr_array_add(c->slide->shapes, s);
    }
    if (own.chars != NULL) g_array_free(own.chars, TRUE);
}

/* SpgrContainer: shapes and nested groups */
static void read_group(DrawCtx* c, gsize p, gsize end, const Xform* xf, gint depth) {
    Rec r;
    Xform inner = *xf;
    gboolean first = TRUE;
    for (; rec_at(c->ppt->d, p, end, &r); p = r.body + r.len) {
        if (r.type == ART_SP) {
            gboolean is_group = FALSE;
            read_shape(c, r.body, r.body + r.len, xf, first && depth > 0 ? &is_group : NULL, &inner);
            if (is_group) xf = &inner;
            first = FALSE;
        } else if (r.type == ART_SPGR && depth < MAX_DEPTH) {
            read_group(c, r.body, r.body + r.len, xf, depth + 1);
            first = FALSE;
        }
    }
}

/* Drawing of a slide, notes or master container */
static void read_drawing(DrawCtx* c, gsize p, gsize end) {
    Rec r, g;
    Xform root = { 1 / MASTER_PER_PT, 1 / MASTER_PER_PT, 0, 0 };
    for (; rec_at(c->ppt->d, p, end, &r); p = r.body + r.len) {
        if (r.type != RT_PPDRAWING) continue;
        for (gsize q = r.body; rec_at(c->ppt->d, q, r.body + r.len, &g); q = g.body + g.len) {
            if (g.type != ART_DG) continue;
            Rec k;
            for (gsize s = g.body; rec_at(c->ppt->d, s, g.body + g.len, &k); s = k.body + k.len) {
                if (k.type == ART_SPGR) read_group(c, k.body, k.body + k.len, &root, 0);
                else if (k.type == ART_SP) read_shape(c, k.body, k.body + k.len, &root, NULL, NULL);
            }
        }
    }
}

/* Container's own colour scheme and SlideAtom flags */
static guint16 read_slide_header(const Ppt* ppt, gsize p, gsize end, guint32 scheme[8], gboolean* has_scheme) {
    Rec r;
    guint16 flags = 0x07;
    *has_scheme = FALSE;
    for (; rec_at(ppt->d, p, end, &r); p = r.body + r.len) {
        if (r.type == RT_SLIDE_ATOM && r.len >= 22) {
            flags = u16(ppt->d + r.body + 20);
        } else if (r.type == RT_COLOR_SCHEME && r.inst == 1 && r.len >= 32 && !*has_scheme) {
            read_scheme(ppt->d + r.body, r.len, scheme);
            *has_scheme = TRUE;
        }
    }
    return flags;
}

/* Text boxes nobody positioned: the Office title and body boxes */
static void add_default_text(DrawCtx* c, GArray* blocks, gdouble w, gdouble h) {
    gdouble y_body = 0.25;
    for (guint i = 0; i < blocks->len; i++) {
        const TextBlock* b = &g_array_index(blocks, TextBlock, i);
        gboolean title = b->type == TT_TITLE || b->type == TT_CENTER_TITLE;
        SlideShape* s = slide_shape_new();
        s->x = 0.05 * w;
        s->w = 0.9 * w;
        s->y = title ? 0.04 * h : y_body * h;
        s->h = title ? 0.17 * h : (0.95 - y_body) * h;
        s->anchor = title ? SLIDE_ANCHOR_MIDDLE : SLIDE_ANCHOR_TOP;
        build_text(c, b, s->paragraphs);
        if (title && c->slide->title == NULL) c->slide->title = slide_shape_text(s);
        g_ptr_array_add(c->slide->shapes, s);
    }
}

static gchar* notes_text(const Ppt* ppt, gsize p, gsize end, const guint32* scheme) {
    Slide* tmp = slide_new();
    DrawCtx c = { ppt, scheme, NULL, tmp, FALSE, FALSE };
    read_drawing(&c, p, end);
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < tmp->shapes->len; i++) {
        gchar* t = slide_shape_text(g_ptr_array_index(tmp->shapes, i));
        if (t[0] != '\0' && strcmp(t, "*") != 0) {
            if (out->len > 0) g_string_append_c(out, '\n');
            g_string_append(out, t);
        }
        g_free(t);
    }
    slide_free(tmp);
    if (out->len == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

static gsize container_end(const Ppt* ppt, gsize off, guint type) {
    Rec r;
    return rec_at(ppt->d, off, ppt->len, &r) && r.type == type ? r.body + r.len : 0;
}

SlideDeck* ppt_reader_read(OleReader* ole, gboolean* encrypted, GError** error) {
    if (encrypted != NULL) *encrypted = FALSE;
    GBytes* stream = ole_reader_read(ole, "PowerPoint Document", PPT_MAX_STREAM, error);
    if (stream == NULL) return NULL;
    Ppt ppt;
    memset(&ppt, 0, sizeof ppt);
    ppt.d = g_bytes_get_data(stream, &ppt.len);
    ppt.width = 720;
    ppt.height = 540;
    ppt.fonts = g_ptr_array_new_with_free_func(g_free);
    ppt.persist = g_hash_table_new(g_direct_hash, g_direct_equal);
    ppt.slide_refs = g_array_new(FALSE, FALSE, sizeof(guint32));
    ppt.notes_refs = g_array_new(FALSE, FALSE, sizeof(guint32));
    ppt.slide_texts = g_ptr_array_new_with_free_func((GDestroyNotify)g_array_unref);
    ppt.slide_offsets = g_array_new(FALSE, FALSE, sizeof(gsize));
    ppt.notes_offsets = g_array_new(FALSE, FALSE, sizeof(gsize));
    ppt.blip_offsets = g_ptr_array_new();
    static const guint32 OFFICE_SCHEME[8] = { 0xFFFFFF, 0x000000, 0x808080, 0x000000, 0xBBE0E3, 0x333399,
                                              0x009999, 0x99CC00 };
    memcpy(ppt.master_scheme, OFFICE_SCHEME, sizeof ppt.master_scheme);
    default_styles(&ppt);

    /* Top-level containers in stream order */
    gsize p = 0;
    gboolean any = FALSE;
    Rec r;
    for (; rec_at(ppt.d, p, ppt.len, &r); p = r.body + r.len) {
        any = TRUE;
        if (r.type == RT_DOCUMENT) {
            read_document(&ppt, r.body, r.body + r.len);
        } else if (r.type == RT_MAIN_MASTER && ppt.master_offset == 0) {
            ppt.master_offset = p;
            Rec k;
            for (gsize q = r.body; rec_at(ppt.d, q, r.body + r.len, &k); q = k.body + k.len) {
                if (k.type == RT_TX_MASTER_STYLE) read_master_style(&ppt, k.inst, ppt.d + k.body, k.len);
                if (k.type == RT_COLOR_SCHEME && k.inst == 1 && k.len >= 32) {
                    read_scheme(ppt.d + k.body, k.len, ppt.master_scheme);
                    ppt.has_master_scheme = TRUE;
                }
            }
        } else if (r.type == RT_SLIDE) {
            g_array_append_val(ppt.slide_offsets, p);
        } else if (r.type == RT_NOTES && ppt.slide_offsets->len > 0) {
            g_array_append_val(ppt.notes_offsets, p);
        } else if (r.type == RT_DOCUMENT_ENCRYPTION) {
            ppt.encrypted = TRUE;
        }
    }
    read_persist(&ppt, ole);
    ppt.pictures = ole_reader_has(ole, "Pictures") ? ole_reader_read(ole, "Pictures", PICTURES_MAX_STREAM, NULL) : NULL;

    SlideDeck* deck = NULL;
    if (ppt.encrypted) {
        if (encrypted != NULL) *encrypted = TRUE;
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("the presentation is encrypted"));
    } else if (!any) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("no presentation records"));
    } else {
        deck = slide_deck_new(ppt.width, ppt.height);
        deck->font = ppt.fonts->len > 0 ? g_strdup(g_ptr_array_index(ppt.fonts, 0)) : NULL;
        /* Slides: through the persist directory, else in stream order */
        guint n = ppt.slide_refs->len > 0 ? ppt.slide_refs->len : ppt.slide_offsets->len;
        for (guint i = 0; i < n; i++) {
            gsize off = 0;
            if (ppt.slide_refs->len > 0) {
                gpointer v = g_hash_table_lookup(ppt.persist, GUINT_TO_POINTER(g_array_index(ppt.slide_refs, guint32, i)));
                off = GPOINTER_TO_SIZE(v);
            }
            gsize end = off != 0 ? container_end(&ppt, off, RT_SLIDE) : 0;
            if (end == 0 && i < ppt.slide_offsets->len) {
                off = g_array_index(ppt.slide_offsets, gsize, i);
                end = container_end(&ppt, off, RT_SLIDE);
            }
            Slide* slide = slide_new();
            guint32 scheme[8];
            memcpy(scheme, ppt.master_scheme, sizeof scheme);
            gboolean own_scheme = FALSE;
            guint16 flags = end != 0 ? read_slide_header(&ppt, off + 8, end, scheme, &own_scheme) : 0x07;
            if (!own_scheme || (flags & 0x02)) memcpy(scheme, ppt.master_scheme, sizeof scheme);
            slide->background = scheme[0];
            GArray* blocks = i < ppt.slide_texts->len ? g_ptr_array_index(ppt.slide_texts, i) : NULL;
            DrawCtx c = { &ppt, scheme, blocks, slide, FALSE, (flags & 0x04) != 0 };
            /* Master background and shapes under the slide's */
            gsize mend = ppt.master_offset != 0 ? container_end(&ppt, ppt.master_offset, RT_MAIN_MASTER) : 0;
            if (mend != 0) {
                Slide* master = slide_new();
                DrawCtx mc = { &ppt, scheme, NULL, master, FALSE, FALSE };
                read_drawing(&mc, ppt.master_offset + 8, mend);
                if (mc.background_found) {
                    slide->background = master->background;
                    slide->background_end = master->background_end;
                    if (master->background_picture != NULL)
                        slide->background_picture = g_bytes_ref(master->background_picture);
                }
                if (flags & 0x01) {
                    /* Master objects: not the placeholder prompts (their text) */
                    while (master->shapes->len > 0) {
                        SlideShape* s = g_ptr_array_steal_index(master->shapes, 0);
                        if (s->paragraphs->len == 0) g_ptr_array_add(slide->shapes, s);
                        else slide_shape_free(s);
                    }
                }
                slide_free(master);
            }
            guint drawn = slide->shapes->len;
            if (end != 0) {
                read_drawing(&c, off + 8, end);
            }
            gboolean has_text = FALSE;
            for (guint k = drawn; k < slide->shapes->len; k++) {
                has_text |= ((SlideShape*)g_ptr_array_index(slide->shapes, k))->paragraphs->len > 0;
            }
            if (!has_text && blocks != NULL) add_default_text(&c, blocks, deck->width, deck->height);
            /* Notes, in order */
            gsize noff = 0, nend = 0;
            if (i < ppt.notes_refs->len) {
                noff = GPOINTER_TO_SIZE(g_hash_table_lookup(ppt.persist,
                                                            GUINT_TO_POINTER(g_array_index(ppt.notes_refs, guint32, i))));
                nend = noff != 0 ? container_end(&ppt, noff, RT_NOTES) : 0;
            }
            if (nend == 0 && i < ppt.notes_offsets->len) {
                noff = g_array_index(ppt.notes_offsets, gsize, i);
                nend = container_end(&ppt, noff, RT_NOTES);
            }
            if (nend != 0) slide->notes = notes_text(&ppt, noff + 8, nend, scheme);
            /* No title placeholder: the first text names the slide */
            for (guint k = drawn; slide->title == NULL && k < slide->shapes->len; k++) {
                gchar* t = slide_shape_text(g_ptr_array_index(slide->shapes, k));
                gchar* nl = strchr(t, '\n');
                if (nl != NULL) *nl = '\0';
                if (t[0] != '\0') slide->title = t;
                else g_free(t);
            }
            g_ptr_array_add(deck->slides, slide);
        }
    }
    if (ppt.pictures != NULL) g_bytes_unref(ppt.pictures);
    g_ptr_array_free(ppt.blip_offsets, TRUE);
    g_array_free(ppt.slide_offsets, TRUE);
    g_array_free(ppt.notes_offsets, TRUE);
    g_ptr_array_unref(ppt.slide_texts);
    g_array_free(ppt.slide_refs, TRUE);
    g_array_free(ppt.notes_refs, TRUE);
    g_hash_table_destroy(ppt.persist);
    g_ptr_array_free(ppt.fonts, TRUE);
    g_bytes_unref(stream);
    return deck;
}
