/* Formatting of Word documents: properties, styles, theme fonts, numbering (see docx_styles.h) */
#include "docx_styles.h"

#include <gio/gio.h>
#include <string.h>

#include "formats/zip_xml.h"

#define MAX_PART (16u * 1024 * 1024)
#define MAX_LEVELS 9
#define MAX_STYLE_CHAIN 16
#define TWIPS_PER_PT 20.0

/* ---- Attributes ---- */

static const gchar* attr(const gchar** names, const gchar** values, const gchar* key) {
    for (gsize i = 0; names[i] != NULL; i++) {
        if (strcmp(names[i], key) == 0) {
            return values[i];
        }
    }
    return NULL;
}

/* On/off property such as <w:b/> or <w:b w:val="0"/> */
static gboolean attr_on(const gchar** names, const gchar** values) {
    const gchar* v = attr(names, values, "w:val");
    return v == NULL || !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0);
}

static gboolean attr_number(const gchar** names, const gchar** values, const gchar* key, gdouble* out) {
    const gchar* v = attr(names, values, key);
    if (v == NULL || v[0] == '\0') {
        return FALSE;
    }
    gchar* end = NULL;
    gdouble n = g_ascii_strtod(v, &end);
    if (end == v) {
        return FALSE;
    }
    *out = CLAMP(n, -1e6, 1e6);
    return TRUE;
}

/* Twips attribute in points */
static gboolean attr_twips(const gchar** names, const gchar** values, const gchar* key, gdouble* out) {
    gdouble v = 0;
    if (!attr_number(names, values, key, &v)) {
        return FALSE;
    }
    *out = v / TWIPS_PER_PT;
    return TRUE;
}

/* ---- Properties ---- */

void docx_run_props_clear(DocxRunProps* p) {
    g_free(p->font);
    memset(p, 0, sizeof *p);
    p->color = DOC_COLOR_NONE;
    p->highlight = DOC_COLOR_NONE;
}

void docx_run_props_merge(DocxRunProps* dst, const DocxRunProps* src) {
    guint s = src->set;
    if (s & DOCX_RP_BOLD) dst->bold = src->bold;
    if (s & DOCX_RP_ITALIC) dst->italic = src->italic;
    if (s & DOCX_RP_UNDERLINE) dst->underline = src->underline;
    if (s & DOCX_RP_STRIKE) dst->strike = src->strike;
    if (s & DOCX_RP_CAPS) dst->caps = src->caps;
    if (s & DOCX_RP_HIDDEN) dst->hidden = src->hidden;
    if (s & DOCX_RP_VERT) dst->vert = src->vert;
    if (s & DOCX_RP_SIZE) dst->size = src->size;
    if (s & DOCX_RP_COLOR) dst->color = src->color;
    if (s & DOCX_RP_HIGHLIGHT) dst->highlight = src->highlight;
    if (s & DOCX_RP_FONT) {
        g_free(dst->font);
        dst->font = g_strdup(src->font);
    }
    dst->set |= s;
}

void docx_para_props_merge(DocxParaProps* dst, const DocxParaProps* src) {
    guint s = src->set;
    if (s & DOCX_PP_ALIGN) dst->align = src->align;
    if (s & DOCX_PP_LEFT) dst->left = src->left;
    if (s & DOCX_PP_RIGHT) dst->right = src->right;
    if (s & DOCX_PP_FIRST) dst->first = src->first;
    if (s & DOCX_PP_BEFORE) dst->before = src->before;
    if (s & DOCX_PP_AFTER) dst->after = src->after;
    if (s & DOCX_PP_NUM) dst->num_id = src->num_id;
    if (s & DOCX_PP_LEVEL) dst->ilvl = src->ilvl;
    if (s & DOCX_PP_PAGE_BREAK) dst->page_break_before = src->page_break_before;
    if (s & DOCX_PP_OUTLINE) dst->outline = src->outline;
    dst->set |= s;
}

void docx_run_props_to_run(const DocxRunProps* p, DocRun* run) {
    doc_run_init(run);
    run->font = p->font;
    run->size = p->size;
    run->bold = p->bold;
    run->italic = p->italic;
    run->underline = p->underline;
    run->strike = p->strike;
    run->caps = p->caps;
    run->vert = p->vert;
    run->color = p->color;
    run->highlight = p->highlight;
}

void docx_para_props_to_para(const DocxParaProps* p, DocPara* para) {
    doc_para_init(para);
    para->align = p->align;
    para->left = p->left;
    para->right = p->right;
    para->first = p->first;
    para->before = p->before;
    para->after = p->after;
    para->outline = p->outline;
}

/* Highlighter colours of w:highlight */
static gboolean highlight_color(const gchar* name, guint32* rgb) {
    static const struct { const gchar* name; guint32 rgb; } COLORS[] = {
        { "yellow", 0xFFFF00 }, { "green", 0x00FF00 }, { "cyan", 0x00FFFF }, { "magenta", 0xFF00FF },
        { "blue", 0x0000FF }, { "red", 0xFF0000 }, { "darkBlue", 0x000080 }, { "darkCyan", 0x008080 },
        { "darkGreen", 0x008000 }, { "darkMagenta", 0x800080 }, { "darkRed", 0x800000 },
        { "darkYellow", 0x808000 }, { "darkGray", 0x808080 }, { "lightGray", 0xC0C0C0 },
        { "black", 0x000000 }, { "white", 0xFFFFFF },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(COLORS); i++) {
        if (g_strcmp0(name, COLORS[i].name) == 0) {
            *rgb = COLORS[i].rgb;
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- Styles and theme ---- */

typedef struct {
    gchar* based_on;
    gint heading;          /* 1-9 from the name or outline level, 10 for "Title", 0 */
    DocxRunProps rpr;
    DocxParaProps ppr;
} Style;

struct DocxStyles {
    GHashTable* styles;    /* styleId -> Style* */
    gchar* default_para;   /* styleId of the default paragraph style */
    DocxRunProps rpr;      /* document defaults */
    DocxParaProps ppr;
    gchar* major_font;     /* theme fonts (headings, body) */
    gchar* minor_font;
};

static void style_free(gpointer data) {
    Style* s = data;
    g_free(s->based_on);
    docx_run_props_clear(&s->rpr);
    g_free(s);
}

gboolean docx_run_property(const DocxStyles* styles, DocxRunProps* p, const gchar* name, const gchar** names,
                           const gchar** values) {
    const gchar* val = attr(names, values, "w:val");
    gboolean on = attr_on(names, values);
    gdouble n = 0;
    guint32 rgb = 0;
    if (strcmp(name, "w:b") == 0) {
        p->bold = on;
        p->set |= DOCX_RP_BOLD;
    } else if (strcmp(name, "w:i") == 0) {
        p->italic = on;
        p->set |= DOCX_RP_ITALIC;
    } else if (strcmp(name, "w:u") == 0) {
        p->underline = val == NULL || strcmp(val, "none") != 0;
        p->set |= DOCX_RP_UNDERLINE;
    } else if (strcmp(name, "w:strike") == 0 || strcmp(name, "w:dstrike") == 0) {
        p->strike = on;
        p->set |= DOCX_RP_STRIKE;
    } else if (strcmp(name, "w:caps") == 0 || strcmp(name, "w:smallCaps") == 0) {
        p->caps = on;
        p->set |= DOCX_RP_CAPS;
    } else if (strcmp(name, "w:vanish") == 0 || strcmp(name, "w:webHidden") == 0) {
        p->hidden = on;
        p->set |= DOCX_RP_HIDDEN;
    } else if (strcmp(name, "w:vertAlign") == 0) {
        p->vert = g_strcmp0(val, "superscript") == 0 ? 1 : g_strcmp0(val, "subscript") == 0 ? -1 : 0;
        p->set |= DOCX_RP_VERT;
    } else if (strcmp(name, "w:sz") == 0 && attr_number(names, values, "w:val", &n) && n > 0) {
        p->size = n / 2.0;
        p->set |= DOCX_RP_SIZE;
    } else if (strcmp(name, "w:color") == 0 && val != NULL) {
        p->color = preview_color_parse(val, &rgb) ? rgb : DOC_COLOR_NONE;
        p->set |= DOCX_RP_COLOR;
    } else if (strcmp(name, "w:highlight") == 0 && val != NULL) {
        p->highlight = highlight_color(val, &rgb) ? rgb : DOC_COLOR_NONE;
        p->set |= DOCX_RP_HIGHLIGHT;
    } else if (strcmp(name, "w:shd") == 0) {
        const gchar* fill = attr(names, values, "w:fill");
        if (fill != NULL && preview_color_parse(fill, &rgb) && !(p->set & DOCX_RP_HIGHLIGHT)) {
            p->highlight = rgb;
            p->set |= DOCX_RP_HIGHLIGHT;
        }
    } else if (strcmp(name, "w:rFonts") == 0) {
        const gchar* font = attr(names, values, "w:ascii");
        if (font == NULL) {
            font = attr(names, values, "w:hAnsi");
        }
        const gchar* theme = attr(names, values, "w:asciiTheme");
        if (font == NULL && theme != NULL && styles != NULL) {
            font = g_str_has_prefix(theme, "major") ? styles->major_font : styles->minor_font;
        }
        if (font != NULL && font[0] != '\0') {
            g_free(p->font);
            p->font = g_strdup(font);
            p->set |= DOCX_RP_FONT;
        }
    } else {
        return FALSE;
    }
    return TRUE;
}

gboolean docx_para_property(DocxParaProps* p, const gchar* name, const gchar** names, const gchar** values) {
    const gchar* val = attr(names, values, "w:val");
    gdouble n = 0;
    if (strcmp(name, "w:jc") == 0 && val != NULL) {
        p->align = (strcmp(val, "center") == 0) ? PREVIEW_ALIGN_CENTER
                 : (strcmp(val, "right") == 0 || strcmp(val, "end") == 0) ? PREVIEW_ALIGN_RIGHT
                 : (strcmp(val, "both") == 0 || strcmp(val, "distribute") == 0 || strcmp(val, "justify") == 0)
                       ? PREVIEW_ALIGN_JUSTIFY
                       : PREVIEW_ALIGN_LEFT;
        p->set |= DOCX_PP_ALIGN;
    } else if (strcmp(name, "w:ind") == 0) {
        if (attr_twips(names, values, "w:left", &n) || attr_twips(names, values, "w:start", &n)) {
            p->left = n;
            p->set |= DOCX_PP_LEFT;
        }
        if (attr_twips(names, values, "w:right", &n) || attr_twips(names, values, "w:end", &n)) {
            p->right = n;
            p->set |= DOCX_PP_RIGHT;
        }
        if (attr_twips(names, values, "w:hanging", &n)) {
            p->first = -n;
            p->set |= DOCX_PP_FIRST;
        } else if (attr_twips(names, values, "w:firstLine", &n)) {
            p->first = n;
            p->set |= DOCX_PP_FIRST;
        }
    } else if (strcmp(name, "w:spacing") == 0) {
        if (attr_twips(names, values, "w:before", &n)) {
            p->before = n;
            p->set |= DOCX_PP_BEFORE;
        }
        if (attr_twips(names, values, "w:after", &n)) {
            p->after = n;
            p->set |= DOCX_PP_AFTER;
        }
    } else if (strcmp(name, "w:numId") == 0 && attr_number(names, values, "w:val", &n)) {
        p->num_id = (gint)n;
        p->set |= DOCX_PP_NUM;
    } else if (strcmp(name, "w:ilvl") == 0 && attr_number(names, values, "w:val", &n)) {
        p->ilvl = CLAMP((gint)n, 0, MAX_LEVELS - 1);
        p->set |= DOCX_PP_LEVEL;
    } else if (strcmp(name, "w:pageBreakBefore") == 0) {
        p->page_break_before = attr_on(names, values);
        p->set |= DOCX_PP_PAGE_BREAK;
    } else if (strcmp(name, "w:outlineLvl") == 0 && attr_number(names, values, "w:val", &n)) {
        /* 0-8: Heading 1-9, 9: body text */
        p->outline = n >= 0 && n < 9 ? (gint)n + 1 : 0;
        p->set |= DOCX_PP_OUTLINE;
    } else {
        return FALSE;
    }
    return TRUE;
}

typedef struct {
    DocxStyles* styles;
    Style* cur;
    gboolean in_rpr_default, in_ppr_default, in_ppr, in_rpr;
    gint skip;
} StylesParse;

static void styles_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names, const gchar** values,
                         gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    StylesParse* sp = data;
    if (sp->skip > 0 || strcmp(name, "w:tblStylePr") == 0 || strcmp(name, "w:tblPr") == 0 ||
        strcmp(name, "w:trPr") == 0 || strcmp(name, "w:tcPr") == 0) {
        sp->skip++;
        return;
    }
    if (strcmp(name, "w:rPrDefault") == 0) {
        sp->in_rpr_default = TRUE;
    } else if (strcmp(name, "w:pPrDefault") == 0) {
        sp->in_ppr_default = TRUE;
    } else if (strcmp(name, "w:style") == 0) {
        const gchar* id = attr(names, values, "w:styleId");
        sp->cur = NULL;
        if (id != NULL) {
            sp->cur = g_new0(Style, 1);
            docx_run_props_clear(&sp->cur->rpr);
            g_hash_table_replace(sp->styles->styles, g_strdup(id), sp->cur);
            if (g_strcmp0(attr(names, values, "w:type"), "paragraph") == 0 &&
                g_strcmp0(attr(names, values, "w:default"), "1") == 0 && sp->styles->default_para == NULL) {
                sp->styles->default_para = g_strdup(id);
            }
        }
    } else if (strcmp(name, "w:name") == 0 && sp->cur != NULL) {
        const gchar* v = attr(names, values, "w:val");
        if (g_ascii_strcasecmp(v != NULL ? v : "", "title") == 0) {
            sp->cur->heading = 10;
        } else if (v != NULL && g_ascii_strncasecmp(v, "heading ", 8) == 0 && v[8] >= '1' && v[8] <= '9') {
            sp->cur->heading = v[8] - '0';
        }
    } else if (sp->in_ppr && sp->cur != NULL && strcmp(name, "w:outlineLvl") == 0) {
        const gchar* v = attr(names, values, "w:val");
        gint lvl = v != NULL ? (gint)g_ascii_strtoll(v, NULL, 10) : 9;
        if (sp->cur->heading == 0 && lvl >= 0 && lvl < 9) {
            sp->cur->heading = lvl + 1;
        }
    } else if (strcmp(name, "w:basedOn") == 0 && sp->cur != NULL) {
        g_free(sp->cur->based_on);
        sp->cur->based_on = g_strdup(attr(names, values, "w:val"));
    } else if (strcmp(name, "w:pPr") == 0) {
        sp->in_ppr = TRUE;
    } else if (strcmp(name, "w:rPr") == 0) {
        sp->in_rpr = TRUE;
    } else if (sp->in_rpr) {
        DocxRunProps* p = sp->cur != NULL ? &sp->cur->rpr : sp->in_rpr_default ? &sp->styles->rpr : NULL;
        if (p != NULL) {
            docx_run_property(sp->styles, p, name, names, values);
        }
    } else if (sp->in_ppr) {
        DocxParaProps* p = sp->cur != NULL ? &sp->cur->ppr : sp->in_ppr_default ? &sp->styles->ppr : NULL;
        if (p != NULL) {
            docx_para_property(p, name, names, values);
        }
    }
}

static void styles_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    StylesParse* sp = data;
    if (sp->skip > 0) {
        sp->skip--;
        return;
    }
    if (strcmp(name, "w:style") == 0) {
        sp->cur = NULL;
    } else if (strcmp(name, "w:rPrDefault") == 0) {
        sp->in_rpr_default = FALSE;
    } else if (strcmp(name, "w:pPrDefault") == 0) {
        sp->in_ppr_default = FALSE;
    } else if (strcmp(name, "w:pPr") == 0) {
        sp->in_ppr = FALSE;
    } else if (strcmp(name, "w:rPr") == 0) {
        sp->in_rpr = FALSE;
    }
}

typedef struct {
    DocxStyles* styles;
    gint in_major;   /* 1 major, 2 minor, 0 elsewhere */
} ThemeParse;

static void theme_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names, const gchar** values,
                        gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    ThemeParse* tp = data;
    const gchar* local = zip_xml_local(name);
    if (strcmp(local, "majorFont") == 0) {
        tp->in_major = 1;
    } else if (strcmp(local, "minorFont") == 0) {
        tp->in_major = 2;
    } else if (strcmp(local, "latin") == 0 && tp->in_major > 0) {
        const gchar* face = zip_xml_attr(names, values, "typeface");
        gchar** slot = tp->in_major == 1 ? &tp->styles->major_font : &tp->styles->minor_font;
        if (face != NULL && face[0] != '\0' && *slot == NULL) {
            *slot = g_strdup(face);
        }
    }
}

static void theme_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    const gchar* local = zip_xml_local(name);
    if (strcmp(local, "majorFont") == 0 || strcmp(local, "minorFont") == 0) {
        ((ThemeParse*)data)->in_major = 0;
    }
}

DocxStyles* docx_styles_read(ZipReader* zip, const gchar* styles_part, const gchar* theme_part) {
    DocxStyles* s = g_new0(DocxStyles, 1);
    s->styles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, style_free);
    docx_run_props_clear(&s->rpr);
    if (theme_part != NULL) {
        ThemeParse tp = { s, 0 };
        static const GMarkupParser theme_parser = { theme_start, theme_end, NULL, NULL, NULL };
        zip_xml_parse(zip, theme_part, MAX_PART, TRUE, &theme_parser, &tp, NULL);
    }
    if (styles_part != NULL) {
        StylesParse sp = { s, NULL, FALSE, FALSE, FALSE, FALSE, 0 };
        static const GMarkupParser styles_parser = { styles_start, styles_end, NULL, NULL, NULL };
        zip_xml_parse(zip, styles_part, MAX_PART, TRUE, &styles_parser, &sp, NULL);
    }
    if (!(s->rpr.set & DOCX_RP_FONT) && s->minor_font != NULL) {
        s->rpr.font = g_strdup(s->minor_font);
        s->rpr.set |= DOCX_RP_FONT;
    }
    return s;
}

void docx_styles_free(DocxStyles* s) {
    if (s == NULL) {
        return;
    }
    g_hash_table_destroy(s->styles);
    g_free(s->default_para);
    docx_run_props_clear(&s->rpr);
    g_free(s->major_font);
    g_free(s->minor_font);
    g_free(s);
}

/* Styles from the base of the chain to id itself */
static GPtrArray* style_chain(const DocxStyles* s, const gchar* id) {
    GPtrArray* chain = g_ptr_array_new();
    const Style* st = id != NULL ? g_hash_table_lookup(s->styles, id) : NULL;
    for (gint i = 0; st != NULL && i < MAX_STYLE_CHAIN; i++) {
        g_ptr_array_insert(chain, 0, (gpointer)st);
        st = st->based_on != NULL ? g_hash_table_lookup(s->styles, st->based_on) : NULL;
    }
    return chain;
}

void docx_styles_para(const DocxStyles* s, const gchar* para_style, DocxParaProps* out) {
    memset(out, 0, sizeof *out);
    docx_para_props_merge(out, &s->ppr);
    GPtrArray* chain = style_chain(s, para_style != NULL ? para_style : s->default_para);
    gint heading = 0;
    for (guint i = 0; i < chain->len; i++) {
        const Style* st = g_ptr_array_index(chain, i);
        docx_para_props_merge(out, &st->ppr);
        heading = st->heading > 0 ? st->heading : heading;
    }
    g_ptr_array_free(chain, TRUE);
    /* Title (10) is no outline level in Word */
    if (heading > 0 && heading <= 9) {
        out->outline = heading;
        out->set |= DOCX_PP_OUTLINE;
    }
}

void docx_styles_run(const DocxStyles* s, const gchar* para_style, const gchar* run_style, DocxRunProps* out) {
    memset(out, 0, sizeof *out);
    docx_run_props_clear(out);
    docx_run_props_merge(out, &s->rpr);
    const gchar* ids[2] = { para_style != NULL ? para_style : s->default_para, run_style };
    for (gint k = 0; k < 2; k++) {
        GPtrArray* chain = style_chain(s, ids[k]);
        guint styled = 0;
        gint heading = 0;
        for (guint i = 0; i < chain->len; i++) {
            const Style* st = g_ptr_array_index(chain, i);
            docx_run_props_merge(out, &st->rpr);
            styled |= st->rpr.set;
            heading = st->heading > 0 ? st->heading : heading;
        }
        g_ptr_array_free(chain, TRUE);
        /* A heading style without formatting of its own: Word's default look */
        if (k == 0 && heading > 0 && !(styled & (DOCX_RP_SIZE | DOCX_RP_BOLD))) {
            static const gdouble SIZES[] = { 0, 16, 13, 12, 11, 11, 11, 11, 11, 11, 28 };
            out->size = MAX(SIZES[heading], out->size);
            out->bold = heading <= 9;
            out->set |= DOCX_RP_SIZE | DOCX_RP_BOLD;
        }
    }
}

/* ---- Numbering ---- */

typedef struct {
    gint start;
    gchar* format;        /* numFmt: decimal, bullet, lowerLetter... */
    gchar* text;          /* lvlText: "%1.", "•" */
    DocxParaProps ppr;    /* indents */
    DocxRunProps rpr;     /* label font */
} Level;

typedef struct {
    Level levels[MAX_LEVELS];
} AbstractNum;

typedef struct {
    gint abstract_id;
    gint start_override[MAX_LEVELS];  /* -1: none */
    gint counts[MAX_LEVELS];          /* items seen per level */
} Num;

struct DocxNumbering {
    GHashTable* abstracts;  /* abstractNumId -> AbstractNum* */
    GHashTable* nums;       /* numId -> Num* */
};

static void abstract_free(gpointer data) {
    AbstractNum* a = data;
    for (gint l = 0; l < MAX_LEVELS; l++) {
        g_free(a->levels[l].format);
        g_free(a->levels[l].text);
        docx_run_props_clear(&a->levels[l].rpr);
    }
    g_free(a);
}

typedef struct {
    DocxNumbering* num;
    const DocxStyles* styles;
    AbstractNum* abs;
    gint lvl;
    Num* cur_num;
    gint override_lvl;
    gboolean in_ppr, in_rpr;
} NumParse;

static void num_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names, const gchar** values,
                      gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    NumParse* np = data;
    gdouble n = 0;
    const gchar* val = attr(names, values, "w:val");
    if (strcmp(name, "w:abstractNum") == 0) {
        np->abs = NULL;
        np->lvl = -1;
        if (attr_number(names, values, "w:abstractNumId", &n)) {
            np->abs = g_new0(AbstractNum, 1);
            for (gint l = 0; l < MAX_LEVELS; l++) {
                np->abs->levels[l].start = 1;
                docx_run_props_clear(&np->abs->levels[l].rpr);
            }
            g_hash_table_replace(np->num->abstracts, GINT_TO_POINTER((gint)n + 1), np->abs);
        }
    } else if (np->abs != NULL && strcmp(name, "w:lvl") == 0) {
        np->lvl = attr_number(names, values, "w:ilvl", &n) && n >= 0 && n < MAX_LEVELS ? (gint)n : -1;
    } else if (np->abs != NULL && np->lvl >= 0) {
        Level* l = &np->abs->levels[np->lvl];
        if (strcmp(name, "w:pPr") == 0) {
            np->in_ppr = TRUE;
        } else if (strcmp(name, "w:rPr") == 0) {
            np->in_rpr = TRUE;
        } else if (np->in_ppr) {
            docx_para_property(&l->ppr, name, names, values);
        } else if (np->in_rpr) {
            docx_run_property(np->styles, &l->rpr, name, names, values);
        } else if (strcmp(name, "w:start") == 0 && attr_number(names, values, "w:val", &n)) {
            l->start = (gint)n;
        } else if (strcmp(name, "w:numFmt") == 0 && val != NULL) {
            g_free(l->format);
            l->format = g_strdup(val);
        } else if (strcmp(name, "w:lvlText") == 0 && val != NULL) {
            g_free(l->text);
            l->text = g_strdup(val);
        }
    } else if (strcmp(name, "w:num") == 0 && attr_number(names, values, "w:numId", &n)) {
        np->cur_num = g_new0(Num, 1);
        np->cur_num->abstract_id = -1;
        for (gint l = 0; l < MAX_LEVELS; l++) {
            np->cur_num->start_override[l] = -1;
        }
        g_hash_table_replace(np->num->nums, GINT_TO_POINTER((gint)n), np->cur_num);
    } else if (np->cur_num != NULL && strcmp(name, "w:abstractNumId") == 0 && attr_number(names, values, "w:val", &n)) {
        np->cur_num->abstract_id = (gint)n;
    } else if (np->cur_num != NULL && strcmp(name, "w:lvlOverride") == 0) {
        np->override_lvl = attr_number(names, values, "w:ilvl", &n) && n >= 0 && n < MAX_LEVELS ? (gint)n : -1;
    } else if (np->cur_num != NULL && np->override_lvl >= 0 && strcmp(name, "w:startOverride") == 0 &&
               attr_number(names, values, "w:val", &n)) {
        np->cur_num->start_override[np->override_lvl] = (gint)n;
    }
}

static void num_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    NumParse* np = data;
    if (strcmp(name, "w:abstractNum") == 0) {
        np->abs = NULL;
    } else if (strcmp(name, "w:lvl") == 0) {
        np->lvl = -1;
    } else if (strcmp(name, "w:num") == 0) {
        np->cur_num = NULL;
    } else if (strcmp(name, "w:lvlOverride") == 0) {
        np->override_lvl = -1;
    } else if (strcmp(name, "w:pPr") == 0) {
        np->in_ppr = FALSE;
    } else if (strcmp(name, "w:rPr") == 0) {
        np->in_rpr = FALSE;
    }
}

DocxNumbering* docx_numbering_read(ZipReader* zip, const gchar* part, const DocxStyles* styles) {
    DocxNumbering* num = g_new0(DocxNumbering, 1);
    num->abstracts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, abstract_free);
    num->nums = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    if (part != NULL) {
        NumParse np = { num, styles, NULL, -1, NULL, -1, FALSE, FALSE };
        static const GMarkupParser parser = { num_start, num_end, NULL, NULL, NULL };
        zip_xml_parse(zip, part, MAX_PART, TRUE, &parser, &np, NULL);
    }
    return num;
}

void docx_numbering_free(DocxNumbering* num) {
    if (num == NULL) {
        return;
    }
    g_hash_table_destroy(num->abstracts);
    g_hash_table_destroy(num->nums);
    g_free(num);
}

static void append_letters(GString* out, gint n, gchar base) {
    /* a..z, then aa..zz like Word */
    n = MAX(n, 1);
    gint repeat = (n - 1) / 26 + 1;
    for (gint i = 0; i < MIN(repeat, 8); i++) {
        g_string_append_c(out, (gchar)(base + (n - 1) % 26));
    }
}

static void append_roman(GString* out, gint n, gboolean upper) {
    static const struct { gint value; const gchar* digits; } ROMAN[] = {
        { 1000, "m" }, { 900, "cm" }, { 500, "d" }, { 400, "cd" }, { 100, "c" }, { 90, "xc" },
        { 50, "l" }, { 40, "xl" }, { 10, "x" }, { 9, "ix" }, { 5, "v" }, { 4, "iv" }, { 1, "i" },
    };
    n = CLAMP(n, 1, 3999);
    for (gsize i = 0; i < G_N_ELEMENTS(ROMAN); i++) {
        while (n >= ROMAN[i].value) {
            for (const gchar* d = ROMAN[i].digits; *d != '\0'; d++) {
                g_string_append_c(out, upper ? g_ascii_toupper(*d) : *d);
            }
            n -= ROMAN[i].value;
        }
    }
}

static void append_number(GString* out, gint n, const gchar* format) {
    if (g_strcmp0(format, "lowerLetter") == 0) {
        append_letters(out, n, 'a');
    } else if (g_strcmp0(format, "upperLetter") == 0) {
        append_letters(out, n, 'A');
    } else if (g_strcmp0(format, "lowerRoman") == 0) {
        append_roman(out, n, FALSE);
    } else if (g_strcmp0(format, "upperRoman") == 0) {
        append_roman(out, n, TRUE);
    } else if (g_strcmp0(format, "decimalZero") == 0) {
        g_string_append_printf(out, "%02d", n);
    } else if (g_strcmp0(format, "none") != 0) {
        g_string_append_printf(out, "%d", n);
    }
}

gchar* docx_numbering_label(DocxNumbering* num, gint num_id, gint ilvl, DocxParaProps* para,
                            DocxRunProps* label_run) {
    Num* n = num != NULL ? g_hash_table_lookup(num->nums, GINT_TO_POINTER(num_id)) : NULL;
    AbstractNum* a = n != NULL ? g_hash_table_lookup(num->abstracts, GINT_TO_POINTER(n->abstract_id + 1)) : NULL;
    if (a == NULL) {
        return NULL;
    }
    ilvl = CLAMP(ilvl, 0, MAX_LEVELS - 1);
    n->counts[ilvl]++;
    for (gint l = ilvl + 1; l < MAX_LEVELS; l++) {
        n->counts[l] = 0;
    }
    const Level* level = &a->levels[ilvl];
    docx_para_props_merge(para, &level->ppr);
    docx_run_props_merge(label_run, &level->rpr);
    if (g_strcmp0(level->format, "bullet") == 0) {
        return preview_symbol_text(level->text, level->rpr.font);
    }
    gchar fallback[4] = { '%', (gchar)('1' + ilvl), '.', '\0' };
    const gchar* text = level->text != NULL ? level->text : fallback;
    GString* out = g_string_new(NULL);
    for (const gchar* p = text; *p != '\0'; p++) {
        if (p[0] == '%' && p[1] >= '1' && p[1] <= '9') {
            gint l = p[1] - '1';
            gint start = n->start_override[l] >= 0 ? n->start_override[l] : a->levels[l].start;
            append_number(out, start + MAX(n->counts[l], 1) - 1, a->levels[l].format);
            p++;
        } else {
            g_string_append_c(out, *p);
        }
    }
    if (out->len == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}
