/* DrawingML shapes of PowerPoint presentations (see pptx_shapes.h) */
#include "pptx_shapes.h"
#include "pptx_shapes_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "preview/preview_doc.h"
#include "zip_xml.h"

#define MAX_PART (32u * 1024 * 1024)
#define MAX_PICTURE (16u * 1024 * 1024)
#define MAX_DEPTH 96

/* ---- Theme and colours ---- */

static const gchar* const SCHEME_NAMES[C_COUNT] = {
    "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink",
};

/* Logical colours of the colour map, in the order of SCHEME_NAMES for the
 * last eight */
static const gchar* const MAP_NAMES[C_COUNT] = {
    "tx1", "bg1", "tx2", "bg2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink",
};

/* The Office theme */
static const guint32 OFFICE_COLORS[C_COUNT] = {
    0x000000, 0xFFFFFF, 0x44546A, 0xE7E6E6, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47,
    0x0563C1, 0x954F72,
};

typedef struct {
    PptxTheme* theme;
    gint scheme;     /* colour being read, -1 */
    gint font;       /* 1 major, 2 minor, 0 */
} ThemeParse;

static void theme_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names, const gchar** values,
                        gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    ThemeParse* tp = data;
    const gchar* local = zip_xml_local(name);
    for (gint i = 0; i < C_COUNT; i++) {
        if (strcmp(local, SCHEME_NAMES[i]) == 0) {
            tp->scheme = i;
            return;
        }
    }
    guint32 rgb = 0;
    if (tp->scheme >= 0 && strcmp(local, "srgbClr") == 0 && preview_color_parse(zip_xml_attr(names, values, "val"), &rgb)) {
        tp->theme->colors[tp->scheme] = rgb;
    } else if (tp->scheme >= 0 && strcmp(local, "sysClr") == 0 &&
               preview_color_parse(zip_xml_attr(names, values, "lastClr"), &rgb)) {
        tp->theme->colors[tp->scheme] = rgb;
    } else if (strcmp(local, "majorFont") == 0) {
        tp->font = 1;
    } else if (strcmp(local, "minorFont") == 0) {
        tp->font = 2;
    } else if (strcmp(local, "latin") == 0 && tp->font > 0) {
        const gchar* face = zip_xml_attr(names, values, "typeface");
        gchar** slot = tp->font == 1 ? &tp->theme->major : &tp->theme->minor;
        if (face != NULL && face[0] != '\0' && *slot == NULL) {
            *slot = g_strdup(face);
        }
    }
}

static void theme_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    ThemeParse* tp = data;
    const gchar* local = zip_xml_local(name);
    if (tp->scheme >= 0 && strcmp(local, SCHEME_NAMES[tp->scheme]) == 0) {
        tp->scheme = -1;
    } else if (strcmp(local, "majorFont") == 0 || strcmp(local, "minorFont") == 0) {
        tp->font = 0;
    }
}

PptxTheme* pptx_theme_read(ZipReader* zip, const gchar* part) {
    PptxTheme* t = g_new0(PptxTheme, 1);
    memcpy(t->colors, OFFICE_COLORS, sizeof t->colors);
    if (part != NULL) {
        ThemeParse tp = { t, -1, 0 };
        static const GMarkupParser parser = { theme_start, theme_end, NULL, NULL, NULL };
        zip_xml_parse(zip, part, MAX_PART, TRUE, &parser, &tp, NULL);
    }
    if (t->major == NULL) t->major = g_strdup("Calibri Light");
    if (t->minor == NULL) t->minor = g_strdup("Calibri");
    return t;
}

void pptx_theme_free(PptxTheme* theme) {
    if (theme == NULL) {
        return;
    }
    g_free(theme->major);
    g_free(theme->minor);
    g_free(theme);
}

const gchar* pptx_theme_font(const PptxTheme* theme) {
    return theme != NULL ? theme->minor : NULL;
}

/* HSL luminance changes of lumMod / lumOff */
static guint32 change_luminance(guint32 rgb, gdouble mod, gdouble off) {
    gdouble r = ((rgb >> 16) & 0xFF) / 255.0, g = ((rgb >> 8) & 0xFF) / 255.0, b = (rgb & 0xFF) / 255.0;
    gdouble hi = MAX(r, MAX(g, b)), lo = MIN(r, MIN(g, b));
    gdouble l = (hi + lo) / 2, h = 0, s = 0;
    if (hi > lo) {
        gdouble d = hi - lo;
        s = l > 0.5 ? d / (2 - hi - lo) : d / (hi + lo);
        h = hi == r ? (g - b) / d + (g < b ? 6 : 0) : hi == g ? (b - r) / d + 2 : (r - g) / d + 4;
        h /= 6;
    }
    l = CLAMP(l * mod + off, 0.0, 1.0);
    gdouble out[3];
    if (s == 0) {
        out[0] = out[1] = out[2] = l;
    } else {
        gdouble q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        for (gint i = 0; i < 3; i++) {
            gdouble t = h + (1 - i) / 3.0;
            t = t < 0 ? t + 1 : t > 1 ? t - 1 : t;
            out[i] = t < 1 / 6.0 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2 / 3.0 ? p + (q - p) * (2 / 3.0 - t) * 6 : p;
        }
    }
    return ((guint32)(out[0] * 255 + 0.5) << 16) | ((guint32)(out[1] * 255 + 0.5) << 8) | (guint32)(out[2] * 255 + 0.5);
}

guint32 pptx_tint(guint32 rgb, gdouble v) {
    guint32 out = 0;
    for (gint sh = 16; sh >= 0; sh -= 8) {
        gdouble c = (rgb >> sh) & 0xFF;
        out |= (guint32)CLAMP(c + (255 - c) * (1 - v) + 0.5, 0, 255) << sh;
    }
    return out;
}

static guint32 shade(guint32 rgb, gdouble v) {
    guint32 out = 0;
    for (gint sh = 16; sh >= 0; sh -= 8) {
        out |= (guint32)CLAMP(((rgb >> sh) & 0xFF) * v + 0.5, 0, 255) << sh;
    }
    return out;
}

/* ---- Text styles ---- */

void pptx_ls_clear(LevelStyle* s) {
    g_free(s->font);
    g_free(s->bullet);
    g_free(s->bullet_font);
    memset(s, 0, sizeof *s);
}

void pptx_ls_merge(LevelStyle* d, const LevelStyle* s) {
    guint f = s->set;
    if (f & LS_MARL) d->marl = s->marl;
    if (f & LS_INDENT) d->indent = s->indent;
    if (f & LS_ALIGN) d->align = s->align;
    if (f & LS_SIZE) d->size = s->size;
    if (f & LS_BOLD) d->bold = s->bold;
    if (f & LS_ITALIC) d->italic = s->italic;
    if (f & LS_UNDERLINE) d->underline = s->underline;
    if (f & LS_STRIKE) d->strike = s->strike;
    if (f & LS_BASELINE) d->baseline = s->baseline;
    if (f & LS_COLOR) d->color = s->color;
    if (f & LS_FONT) {
        g_free(d->font);
        d->font = g_strdup(s->font);
    }
    if (f & LS_BULLET) {
        g_free(d->bullet);
        d->bullet = g_strdup(s->bullet);
    }
    if (f & LS_BULLET_FONT) {
        g_free(d->bullet_font);
        d->bullet_font = g_strdup(s->bullet_font);
    }
    if (f & LS_BULLET_COLOR) d->bullet_color = s->bullet_color;
    if (f & LS_BULLET_SCALE) d->bullet_scale = s->bullet_scale;
    if (f & LS_BEFORE) d->before = s->before;
    if (f & LS_AFTER) d->after = s->after;
    if (f & LS_LINE) d->line = s->line;
    d->set |= f;
}

static void raw_run_free(gpointer data) {
    RawRun* r = data;
    g_free(r->text);
    pptx_ls_clear(&r->rpr);
    g_free(r);
}

static void raw_para_free(gpointer data) {
    RawPara* p = data;
    pptx_ls_clear(&p->ppr);
    pptx_ls_clear(&p->end);
    g_ptr_array_free(p->runs, TRUE);
    g_free(p);
}

void pptx_body_merge(BodyProps* d, const BodyProps* s) {
    if (s->set & B_ANCHOR) d->anchor = s->anchor;
    if (s->set & B_LEFT) d->left = s->left;
    if (s->set & B_TOP) d->top = s->top;
    if (s->set & B_RIGHT) d->right = s->right;
    if (s->set & B_BOTTOM) d->bottom = s->bottom;
    if (s->set & B_WRAP) d->no_wrap = s->no_wrap;
    if (s->set & B_SCALE) d->font_scale = s->font_scale;
    d->set |= s->set;
}

static void raw_cell_free(gpointer data) {
    RawCell* c = data;
    g_ptr_array_free(c->paras, TRUE);
    g_free(c);
}

static PShape* pshape_new(void) {
    PShape* s = g_new0(PShape, 1);
    s->shape = slide_shape_new();
    s->ref_fill = s->ref_line = s->ref_font = SLIDE_NO_COLOR;
    s->ph_idx = -1;
    s->paras = g_ptr_array_new_with_free_func(raw_para_free);
    return s;
}

static void pshape_free(gpointer data) {
    PShape* s = data;
    slide_shape_free(s->shape);
    g_free(s->ph_type);
    for (gint i = 0; i < MAX_LEVELS; i++) {
        pptx_ls_clear(&s->lst[i]);
    }
    g_ptr_array_free(s->paras, TRUE);
    if (s->grid != NULL) g_array_free(s->grid, TRUE);
    if (s->heights != NULL) g_array_free(s->heights, TRUE);
    if (s->rows != NULL) g_ptr_array_free(s->rows, TRUE);
    g_free(s);
}

/* ---- Shape tree parser ---- */

typedef struct {
    gdouble sx, sy, tx, ty;  /* points = t + EMU * s */
} Xform;

typedef struct {
    ZipReader* zip;
    gchar* dir;
    GHashTable* rels;
    const PptxTheme* theme;
    PptxTree* tree;
    gchar* stack[MAX_DEPTH];
    gint depth;
    GArray* xforms;          /* Xform: groups */
    gdouble gx[4], gy[4];    /* group xfrm being read: off, ext, chOff, chExt */
    PShape* cur;
    gint cur_depth;
    LevelStyle* ls;          /* receiving text properties */
    RawPara* para;
    RawRun* run;
    gboolean in_t;
    guint32* color;          /* last colour written (for its modifiers) */
    guint* color_set;
    guint color_bit;
    gint stops;              /* gradient stops seen */
    gdouble path_w, path_h;
    RawCell* cell;
    GPtrArray* row;
    gint bez_filled;         /* points of the curve being read */
} Parse;

static const gchar* up(const Parse* p, gint n) {
    return p->depth - 1 - n >= 0 ? p->stack[p->depth - 1 - n] : "";
}

static gboolean inside(const Parse* p, const gchar* local) {
    for (gint i = 0; i < p->depth; i++) {
        if (strcmp(p->stack[i], local) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static gdouble attr_num(const gchar** names, const gchar** values, const gchar* key, gdouble def) {
    const gchar* v = zip_xml_attr(names, values, key);
    return v != NULL && v[0] != '\0' ? g_ascii_strtod(v, NULL) : def;
}

static gboolean attr_flag(const gchar** names, const gchar** values, const gchar* key) {
    const gchar* v = zip_xml_attr(names, values, key);
    return v != NULL && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0);
}

static const Xform* xform(const Parse* p) {
    return &g_array_index(p->xforms, Xform, p->xforms->len - 1);
}

static gchar* theme_font(const Parse* p, const gchar* face) {
    if (face == NULL || face[0] == '\0') return NULL;
    if (g_str_has_prefix(face, "+mj")) return g_strdup(p->theme->major);
    if (g_str_has_prefix(face, "+mn")) return g_strdup(p->theme->minor);
    return g_strdup(face);
}

static guint32 scheme_color(const Parse* p, const gchar* val) {
    if (val == NULL) return 0;
    for (gint i = 0; i < C_COUNT; i++) {
        if (strcmp(val, MAP_NAMES[i]) == 0) {
            return p->theme->colors[p->tree->clr_map[i]];
        }
        if (strcmp(val, SCHEME_NAMES[i]) == 0) {
            return p->theme->colors[i];
        }
    }
    return p->theme->colors[C_DK1];
}

/* Where a colour element writes, from the elements around it */
static void color_target(Parse* p, guint32** value, guint** set, guint* bit) {
    const gchar* parent = up(p, 1);
    const gchar* gp = up(p, 2);
    *value = NULL;
    *set = NULL;
    *bit = 0;
    PShape* s = p->cur;
    if (strcmp(parent, "solidFill") == 0) {
        if ((strcmp(gp, "rPr") == 0 || strcmp(gp, "defRPr") == 0 || strcmp(gp, "endParaRPr") == 0) && p->ls != NULL) {
            *value = &p->ls->color;
            *set = &p->ls->set;
            *bit = LS_COLOR;
        } else if (strcmp(gp, "ln") == 0 && s != NULL) {
            *value = &s->shape->line;
            s->line_set = TRUE;
        } else if (strcmp(gp, "spPr") == 0 && s != NULL) {
            *value = &s->shape->fill;
            s->fill_set = TRUE;
        } else if (strcmp(gp, "bgPr") == 0) {
            *value = &p->tree->bg;
            p->tree->has_bg = TRUE;
        } else if (strcmp(gp, "tcPr") == 0 && p->cell != NULL) {
            *value = &p->cell->fill;
        }
    } else if (strcmp(parent, "gs") == 0) {
        const gchar* owner = up(p, 4);
        gboolean first = p->stops++ == 0;
        if (strcmp(owner, "spPr") == 0 && s != NULL) {
            *value = first ? &s->shape->fill : &s->shape->fill_end;
            s->fill_set = TRUE;
        } else if (strcmp(owner, "bgPr") == 0) {
            *value = first ? &p->tree->bg : &p->tree->bg_end;
            p->tree->has_bg = TRUE;
        }
    } else if (strcmp(parent, "buClr") == 0 && p->ls != NULL) {
        *value = &p->ls->bullet_color;
        *set = &p->ls->set;
        *bit = LS_BULLET_COLOR;
    } else if (s != NULL && strcmp(parent, "fillRef") == 0) {
        *value = &s->ref_fill;
    } else if (s != NULL && strcmp(parent, "lnRef") == 0) {
        *value = &s->ref_line;
    } else if (s != NULL && strcmp(parent, "fontRef") == 0) {
        *value = &s->ref_font;
    } else if (strcmp(parent, "bgRef") == 0) {
        *value = &p->tree->bg;
        p->tree->has_bg = TRUE;
    }
}

static gboolean start_color(Parse* p, const gchar* local, const gchar** names, const gchar** values) {
    static const struct { const gchar* name; guint32 rgb; } PRESET[] = {
        { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 },
        { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "gray", 0x808080 }, { "orange", 0xFFA500 },
    };
    guint32 rgb = 0;
    gboolean known = TRUE;
    if (strcmp(local, "srgbClr") == 0) {
        known = preview_color_parse(zip_xml_attr(names, values, "val"), &rgb);
    } else if (strcmp(local, "schemeClr") == 0) {
        rgb = scheme_color(p, zip_xml_attr(names, values, "val"));
    } else if (strcmp(local, "sysClr") == 0) {
        known = preview_color_parse(zip_xml_attr(names, values, "lastClr"), &rgb);
    } else if (strcmp(local, "scrgbClr") == 0) {
        rgb = ((guint32)CLAMP(attr_num(names, values, "r", 0) / 100000.0 * 255, 0, 255) << 16) |
              ((guint32)CLAMP(attr_num(names, values, "g", 0) / 100000.0 * 255, 0, 255) << 8) |
              (guint32)CLAMP(attr_num(names, values, "b", 0) / 100000.0 * 255, 0, 255);
    } else if (strcmp(local, "prstClr") == 0) {
        const gchar* v = zip_xml_attr(names, values, "val");
        for (gsize i = 0; i < G_N_ELEMENTS(PRESET); i++) {
            if (g_strcmp0(v, PRESET[i].name) == 0) rgb = PRESET[i].rgb;
        }
    } else {
        return FALSE;
    }
    guint32* value;
    guint* set;
    guint bit;
    color_target(p, &value, &set, &bit);
    p->color = NULL;
    if (value != NULL && known) {
        *value = rgb;
        if (set != NULL) *set |= bit;
        p->color = value;
    }
    return TRUE;
}

static void start_color_modifier(Parse* p, const gchar* local, const gchar** names, const gchar** values) {
    if (p->color == NULL) return;
    gdouble v = attr_num(names, values, "val", 100000) / 100000.0;
    if (strcmp(local, "lumMod") == 0) *p->color = change_luminance(*p->color, v, 0);
    else if (strcmp(local, "lumOff") == 0) *p->color = change_luminance(*p->color, 1, v);
    else if (strcmp(local, "tint") == 0) *p->color = pptx_tint(*p->color, v);
    else if (strcmp(local, "shade") == 0) *p->color = shade(*p->color, v);
}

static SlideGeometry preset_geometry(const gchar* prst) {
    static const struct { const gchar* name; SlideGeometry g; } MAP[] = {
        { "rect", SLIDE_GEOM_RECT }, { "roundRect", SLIDE_GEOM_ROUND_RECT }, { "ellipse", SLIDE_GEOM_ELLIPSE },
        { "line", SLIDE_GEOM_LINE }, { "straightConnector1", SLIDE_GEOM_LINE }, { "triangle", SLIDE_GEOM_TRIANGLE },
        { "rtTriangle", SLIDE_GEOM_RIGHT_TRIANGLE }, { "diamond", SLIDE_GEOM_DIAMOND },
        { "parallelogram", SLIDE_GEOM_PARALLELOGRAM }, { "hexagon", SLIDE_GEOM_HEXAGON },
        { "rightArrow", SLIDE_GEOM_RIGHT_ARROW }, { "leftArrow", SLIDE_GEOM_LEFT_ARROW },
        { "upArrow", SLIDE_GEOM_UP_ARROW }, { "downArrow", SLIDE_GEOM_DOWN_ARROW }, { "chevron", SLIDE_GEOM_CHEVRON },
        { "homePlate", SLIDE_GEOM_PENTAGON }, { "flowChartProcess", SLIDE_GEOM_RECT },
        { "flowChartAlternateProcess", SLIDE_GEOM_ROUND_RECT }, { "flowChartDecision", SLIDE_GEOM_DIAMOND },
        { "snip1Rect", SLIDE_GEOM_RECT }, { "round2SameRect", SLIDE_GEOM_ROUND_RECT },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(MAP); i++) {
        if (g_strcmp0(prst, MAP[i].name) == 0) return MAP[i].g;
    }
    return SLIDE_GEOM_RECT;
}

static gboolean decodable_picture(const gchar* part) {
    const gchar* dot = strrchr(part, '.');
    static const gchar* const exts[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp", ".svg" };
    for (gsize i = 0; dot != NULL && i < G_N_ELEMENTS(exts); i++) {
        if (g_ascii_strcasecmp(dot, exts[i]) == 0) return TRUE;
    }
    return FALSE;
}

static GBytes* read_picture(Parse* p, const gchar* rid) {
    const gchar* part = rid != NULL ? g_hash_table_lookup(p->rels, rid) : NULL;
    if (part == NULL || !decodable_picture(part)) return NULL;
    return zip_reader_read(p->zip, part, MAX_PICTURE, NULL);
}

/* Paragraph / list level properties of a pPr or lvlNpPr element */
static void start_paragraph_props(LevelStyle* ls, const gchar** names, const gchar** values) {
    const gchar* v;
    if ((v = zip_xml_attr(names, values, "marL")) != NULL) {
        ls->marl = g_ascii_strtod(v, NULL) / EMU_PER_PT;
        ls->set |= LS_MARL;
    }
    if ((v = zip_xml_attr(names, values, "indent")) != NULL) {
        ls->indent = g_ascii_strtod(v, NULL) / EMU_PER_PT;
        ls->set |= LS_INDENT;
    }
    if ((v = zip_xml_attr(names, values, "algn")) != NULL) {
        ls->align = strcmp(v, "ctr") == 0 ? SLIDE_ALIGN_CENTER : strcmp(v, "r") == 0 ? SLIDE_ALIGN_RIGHT
                  : (strcmp(v, "just") == 0 || strcmp(v, "dist") == 0) ? SLIDE_ALIGN_JUSTIFY : SLIDE_ALIGN_LEFT;
        ls->set |= LS_ALIGN;
    }
}

/* Character properties of an rPr, defRPr or endParaRPr element */
static void start_run_props(LevelStyle* ls, const gchar** names, const gchar** values) {
    const gchar* v;
    if ((v = zip_xml_attr(names, values, "sz")) != NULL) {
        ls->size = g_ascii_strtod(v, NULL) / 100.0;
        ls->set |= LS_SIZE;
    }
    if ((v = zip_xml_attr(names, values, "b")) != NULL) {
        ls->bold = strcmp(v, "1") == 0 || strcmp(v, "true") == 0;
        ls->set |= LS_BOLD;
    }
    if ((v = zip_xml_attr(names, values, "i")) != NULL) {
        ls->italic = strcmp(v, "1") == 0 || strcmp(v, "true") == 0;
        ls->set |= LS_ITALIC;
    }
    if ((v = zip_xml_attr(names, values, "u")) != NULL) {
        ls->underline = strcmp(v, "none") != 0;
        ls->set |= LS_UNDERLINE;
    }
    if ((v = zip_xml_attr(names, values, "strike")) != NULL) {
        ls->strike = strcmp(v, "noStrike") != 0;
        ls->set |= LS_STRIKE;
    }
    if ((v = zip_xml_attr(names, values, "baseline")) != NULL) {
        gdouble b = g_ascii_strtod(v, NULL);
        ls->baseline = b > 0 ? 1 : b < 0 ? -1 : 0;
        ls->set |= LS_BASELINE;
    }
}

/* Text style elements inside a pPr / lvlNpPr / rPr (ls receives them) */
static gboolean start_text_style(Parse* p, const gchar* local, const gchar** names, const gchar** values) {
    LevelStyle* ls = p->ls;
    if (ls == NULL) return FALSE;
    if (strcmp(local, "defRPr") == 0 || strcmp(local, "rPr") == 0) {
        start_run_props(ls, names, values);
    } else if (strcmp(local, "latin") == 0 && strcmp(up(p, 1), "rPr") != 0 && strcmp(up(p, 1), "defRPr") != 0 &&
               strcmp(up(p, 1), "endParaRPr") != 0) {
        return FALSE;
    } else if (strcmp(local, "latin") == 0) {
        gchar* font = theme_font(p, zip_xml_attr(names, values, "typeface"));
        if (font != NULL) {
            g_free(ls->font);
            ls->font = font;
            ls->set |= LS_FONT;
        }
    } else if (strcmp(local, "buNone") == 0) {
        g_clear_pointer(&ls->bullet, g_free);
        ls->set |= LS_BULLET;
    } else if (strcmp(local, "buChar") == 0) {
        g_free(ls->bullet);
        ls->bullet = g_strdup(zip_xml_attr(names, values, "char"));
        ls->set |= LS_BULLET;
    } else if (strcmp(local, "buAutoNum") == 0) {
        g_free(ls->bullet);
        const gchar* type = zip_xml_attr(names, values, "type");
        ls->bullet = g_strdup_printf("#%s", type != NULL ? type : "arabicPeriod");
        ls->set |= LS_BULLET;
    } else if (strcmp(local, "buFont") == 0) {
        g_free(ls->bullet_font);
        ls->bullet_font = theme_font(p, zip_xml_attr(names, values, "typeface"));
        ls->set |= LS_BULLET_FONT;
    } else if (strcmp(local, "buSzPct") == 0) {
        ls->bullet_scale = attr_num(names, values, "val", 100000) / 100000.0;
        ls->set |= LS_BULLET_SCALE;
    } else if (strcmp(local, "spcPts") == 0 || strcmp(local, "spcPct") == 0) {
        gboolean pts = strcmp(local, "spcPts") == 0;
        gdouble v = attr_num(names, values, "val", 0) / (pts ? 100.0 : 100000.0);
        const gchar* parent = up(p, 1);
        if (strcmp(parent, "spcBef") == 0) {
            ls->before = pts ? v : -v;
            ls->set |= LS_BEFORE;
        } else if (strcmp(parent, "spcAft") == 0) {
            ls->after = pts ? v : -v;
            ls->set |= LS_AFTER;
        } else if (strcmp(parent, "lnSpc") == 0 && !pts) {
            ls->line = v;
            ls->set |= LS_LINE;
        }
    } else {
        return FALSE;
    }
    return TRUE;
}

static void start_xfrm_child(Parse* p, const gchar* local, const gchar** names, const gchar** values) {
    gdouble x = attr_num(names, values, strcmp(local, "off") == 0 || strcmp(local, "chOff") == 0 ? "x" : "cx", 0);
    gdouble y = attr_num(names, values, strcmp(local, "off") == 0 || strcmp(local, "chOff") == 0 ? "y" : "cy", 0);
    gint k = strcmp(local, "off") == 0 ? 0 : strcmp(local, "ext") == 0 ? 1 : strcmp(local, "chOff") == 0 ? 2 : 3;
    if (strcmp(up(p, 2), "grpSpPr") == 0 && strcmp(up(p, 3), "grpSp") == 0) {
        p->gx[k] = x;
        p->gy[k] = y;
        return;
    }
    if (p->cur == NULL || k > 1) return;
    const Xform* t = xform(p);
    SlideShape* s = p->cur->shape;
    if (k == 0) {
        s->x = t->tx + x * t->sx;
        s->y = t->ty + y * t->sy;
    } else {
        s->w = x * t->sx;
        s->h = y * t->sy;
    }
    p->cur->has_xfrm = TRUE;
}

static void start_shape(Parse* p, const gchar* local) {
    p->cur = pshape_new();
    p->cur_depth = p->depth;
    if (strcmp(local, "cxnSp") == 0) {
        p->cur->shape->geometry = SLIDE_GEOM_LINE;
    }
    p->stops = 0;
}

static void start_body_props(Parse* p, const gchar** names, const gchar** values) {
    BodyProps* b = &p->cur->body;
    const gchar* v;
    if ((v = zip_xml_attr(names, values, "anchor")) != NULL) {
        b->anchor = strcmp(v, "ctr") == 0 ? SLIDE_ANCHOR_MIDDLE : strcmp(v, "b") == 0 ? SLIDE_ANCHOR_BOTTOM : SLIDE_ANCHOR_TOP;
        b->set |= B_ANCHOR;
    }
    static const struct { const gchar* key; guint bit; gsize offset; } INSETS[] = {
        { "lIns", B_LEFT, G_STRUCT_OFFSET(BodyProps, left) }, { "tIns", B_TOP, G_STRUCT_OFFSET(BodyProps, top) },
        { "rIns", B_RIGHT, G_STRUCT_OFFSET(BodyProps, right) }, { "bIns", B_BOTTOM, G_STRUCT_OFFSET(BodyProps, bottom) },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(INSETS); i++) {
        if ((v = zip_xml_attr(names, values, INSETS[i].key)) != NULL) {
            *(gdouble*)((guint8*)b + INSETS[i].offset) = g_ascii_strtod(v, NULL) / EMU_PER_PT;
            b->set |= INSETS[i].bit;
        }
    }
    if ((v = zip_xml_attr(names, values, "wrap")) != NULL) {
        b->no_wrap = strcmp(v, "none") == 0;
        b->set |= B_WRAP;
    }
}

static void start_table_element(Parse* p, const gchar* local, const gchar** names, const gchar** values) {
    PShape* s = p->cur;
    if (strcmp(local, "tbl") == 0) {
        s->grid = g_array_new(FALSE, FALSE, sizeof(gdouble));
        s->heights = g_array_new(FALSE, FALSE, sizeof(gdouble));
        s->rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
    } else if (strcmp(local, "tblPr") == 0) {
        s->first_row = attr_flag(names, values, "firstRow");
        s->band_rows = attr_flag(names, values, "bandRow");
    } else if (strcmp(local, "tableStyleId") == 0) {
        s->table_styled = TRUE;
    } else if (strcmp(local, "gridCol") == 0 && s->grid != NULL) {
        gdouble w = attr_num(names, values, "w", 0) * xform(p)->sx;
        g_array_append_val(s->grid, w);
    } else if (strcmp(local, "tr") == 0 && s->rows != NULL && s->rows->len < 512) {
        gdouble h = attr_num(names, values, "h", 0) * xform(p)->sy;
        g_array_append_val(s->heights, h);
        p->row = g_ptr_array_new_with_free_func(raw_cell_free);
        g_ptr_array_add(s->rows, p->row);
    } else if (strcmp(local, "tc") == 0 && p->row != NULL) {
        p->cell = g_new0(RawCell, 1);
        p->cell->paras = g_ptr_array_new_with_free_func(raw_para_free);
        p->cell->fill = SLIDE_NO_COLOR;
        p->cell->col_span = (guint)CLAMP(attr_num(names, values, "gridSpan", 1), 1, 64);
        p->cell->row_span = (guint)CLAMP(attr_num(names, values, "rowSpan", 1), 1, 512);
        p->cell->merged = attr_flag(names, values, "hMerge") || attr_flag(names, values, "vMerge");
        g_ptr_array_add(p->row, p->cell);
    } else if (strcmp(local, "tcPr") == 0 && p->cell != NULL) {
        const gchar* a = zip_xml_attr(names, values, "anchor");
        p->cell->anchor = g_strcmp0(a, "ctr") == 0 ? SLIDE_ANCHOR_MIDDLE : g_strcmp0(a, "b") == 0 ? SLIDE_ANCHOR_BOTTOM
                                                                                              : SLIDE_ANCHOR_TOP;
    }
}

static void start_path_element(Parse* p, const gchar* local, const gchar** names, const gchar** values) {
    SlideShape* s = p->cur->shape;
    if (strcmp(local, "path") == 0) {
        p->path_w = attr_num(names, values, "w", 0);
        p->path_h = attr_num(names, values, "h", 0);
        if (s->path == NULL && p->path_w > 0 && p->path_h > 0) {
            s->path = g_array_new(FALSE, TRUE, sizeof(SlidePathStep));
            s->path_w = p->path_w;
            s->path_h = p->path_h;
            s->geometry = SLIDE_GEOM_PATH;
        }
        return;
    }
    if (s->path == NULL || s->path->len > 20000) return;
    const gchar* parent = up(p, 1);
    if (strcmp(local, "close") == 0) {
        SlidePathStep st = { SLIDE_PATH_CLOSE, { 0 }, { 0 } };
        g_array_append_val(s->path, st);
    } else if (strcmp(local, "pt") == 0) {
        gdouble x = attr_num(names, values, "x", 0), y = attr_num(names, values, "y", 0);
        if (strcmp(parent, "moveTo") == 0 || strcmp(parent, "lnTo") == 0) {
            SlidePathStep st = { strcmp(parent, "moveTo") == 0 ? SLIDE_PATH_MOVE : SLIDE_PATH_LINE, { x }, { y } };
            g_array_append_val(s->path, st);
        } else if (strcmp(parent, "cubicBezTo") == 0) {
            /* Three points make one curve */
            SlidePathStep* last = s->path->len > 0 ? &g_array_index(s->path, SlidePathStep, s->path->len - 1) : NULL;
            gint filled = p->bez_filled;
            if (last == NULL || last->op != SLIDE_PATH_CURVE || filled == 3) {
                SlidePathStep st = { SLIDE_PATH_CURVE, { x, x, x }, { y, y, y } };
                g_array_append_val(s->path, st);
                p->bez_filled = 1;
            } else {
                last->x[filled] = x;
                last->y[filled] = y;
                for (gint k = filled + 1; k < 3; k++) {
                    last->x[k] = x;
                    last->y[k] = y;
                }
                p->bez_filled = filled + 1;
            }
        }
    }
}

static void tree_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names, const gchar** values,
                       gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    Parse* p = data;
    const gchar* local = zip_xml_local(name);
    if (p->depth >= MAX_DEPTH) {
        p->depth++;
        return;
    }
    p->stack[p->depth++] = g_strdup(local);
    if (inside(p, "Fallback") || inside(p, "extLst") || inside(p, "notesStyle")) {
        return;
    }
    PptxTree* t = p->tree;
    PShape* s = p->cur;

    /* Master: colour map and text styles; layout: showMasterSp */
    if (strcmp(local, "clrMap") == 0 && strcmp(up(p, 1), "sldMaster") == 0) {
        for (gint i = 0; i < C_COUNT; i++) {
            const gchar* v = zip_xml_attr(names, values, MAP_NAMES[i]);
            for (gint k = 0; v != NULL && k < C_COUNT; k++) {
                if (strcmp(v, SCHEME_NAMES[k]) == 0) t->clr_map[i] = k;
            }
        }
        return;
    }
    if ((strcmp(local, "sldLayout") == 0 || strcmp(local, "sld") == 0) && zip_xml_attr(names, values, "showMasterSp")) {
        t->show_master_sp = !(g_strcmp0(zip_xml_attr(names, values, "showMasterSp"), "0") == 0 ||
                              g_strcmp0(zip_xml_attr(names, values, "showMasterSp"), "false") == 0);
        return;
    }
    if (g_str_has_prefix(local, "lvl") && g_str_has_suffix(local, "pPr") && local[3] >= '1' && local[3] <= '9') {
        gint lvl = local[3] - '1';
        const gchar* owner = up(p, 1);
        p->ls = NULL;
        if (strcmp(owner, "titleStyle") == 0) p->ls = &t->styles[KIND_TITLE][lvl];
        else if (strcmp(owner, "bodyStyle") == 0) p->ls = &t->styles[KIND_BODY][lvl];
        else if (strcmp(owner, "otherStyle") == 0) p->ls = &t->styles[KIND_OTHER][lvl];
        else if (strcmp(owner, "lstStyle") == 0 && s != NULL && p->cell == NULL) p->ls = &s->lst[lvl];
        if (p->ls != NULL) start_paragraph_props(p->ls, names, values);
        return;
    }
    if (strcmp(local, "bgPr") == 0 || strcmp(local, "bgRef") == 0) {
        p->stops = 0;
        if (strcmp(local, "bgRef") == 0) t->has_bg = TRUE;
    }
    if (start_color(p, local, names, values)) return;
    if (strcmp(local, "lumMod") == 0 || strcmp(local, "lumOff") == 0 || strcmp(local, "tint") == 0 ||
        strcmp(local, "shade") == 0) {
        start_color_modifier(p, local, names, values);
        return;
    }
    if (p->ls != NULL && start_text_style(p, local, names, values)) return;
    if (strcmp(local, "blip") == 0 && inside(p, "bgPr")) {
        GBytes* pic = read_picture(p, zip_xml_attr(names, values, "embed"));
        if (pic != NULL) {
            if (t->bg_picture != NULL) g_bytes_unref(t->bg_picture);
            t->bg_picture = pic;
            t->has_bg = TRUE;
        }
        return;
    }

    if (s == NULL) {
        if (strcmp(local, "sp") == 0 || strcmp(local, "pic") == 0 || strcmp(local, "cxnSp") == 0 ||
            strcmp(local, "graphicFrame") == 0) {
            start_shape(p, local);
        } else if (strcmp(local, "grpSp") == 0) {
            memset(p->gx, 0, sizeof p->gx);
            memset(p->gy, 0, sizeof p->gy);
        } else if (strcmp(local, "off") == 0 || strcmp(local, "ext") == 0 || strcmp(local, "chOff") == 0 ||
                   strcmp(local, "chExt") == 0) {
            start_xfrm_child(p, local, names, values);
        }
        return;
    }

    /* Inside a shape */
    if (strcmp(local, "cNvPr") == 0) {
        s->hidden = attr_flag(names, values, "hidden");
    } else if (strcmp(local, "ph") == 0) {
        s->is_ph = TRUE;
        g_free(s->ph_type);
        s->ph_type = g_strdup(zip_xml_attr(names, values, "type"));
        s->ph_idx = (gint)attr_num(names, values, "idx", -1);
    } else if (strcmp(local, "xfrm") == 0) {
        s->shape->rotation = attr_num(names, values, "rot", 0) / 60000.0;
        s->shape->flip_h = attr_flag(names, values, "flipH");
        s->shape->flip_v = attr_flag(names, values, "flipV");
    } else if (strcmp(local, "off") == 0 || strcmp(local, "ext") == 0) {
        if (strcmp(up(p, 1), "xfrm") == 0) start_xfrm_child(p, local, names, values);
    } else if (strcmp(local, "prstGeom") == 0) {
        s->shape->geometry = preset_geometry(zip_xml_attr(names, values, "prst"));
    } else if (inside(p, "custGeom") && (strcmp(local, "path") == 0 || strcmp(local, "pt") == 0 ||
                                         strcmp(local, "close") == 0)) {
        start_path_element(p, local, names, values);
    } else if (strcmp(local, "noFill") == 0) {
        if (strcmp(up(p, 1), "spPr") == 0) {
            s->shape->fill = SLIDE_NO_COLOR;
            s->fill_set = TRUE;
        } else if (strcmp(up(p, 1), "ln") == 0) {
            s->shape->line = SLIDE_NO_COLOR;
            s->line_set = TRUE;
        }
    } else if (strcmp(local, "ln") == 0 && strcmp(up(p, 1), "spPr") == 0) {
        s->shape->line_width = attr_num(names, values, "w", 9525) / EMU_PER_PT;
    } else if (strcmp(local, "gradFill") == 0) {
        p->stops = 0;
    } else if (strcmp(local, "lin") == 0 && inside(p, "gradFill") && strcmp(up(p, 2), "spPr") == 0) {
        s->shape->gradient_angle = attr_num(names, values, "ang", 0) / 60000.0;
    } else if (strcmp(local, "blip") == 0) {
        GBytes* pic = read_picture(p, zip_xml_attr(names, values, "embed"));
        if (pic != NULL) {
            if (s->shape->picture != NULL) g_bytes_unref(s->shape->picture);
            s->shape->picture = pic;
        }
    } else if (strcmp(local, "graphicData") == 0) {
        const gchar* uri = zip_xml_attr(names, values, "uri");
        s->label = uri != NULL && strstr(uri, "chart") != NULL ? "Chart"
                 : uri != NULL && strstr(uri, "diagram") != NULL ? "Diagram" : NULL;
    } else if (strcmp(local, "tbl") == 0 || strcmp(local, "tblPr") == 0 || strcmp(local, "tableStyleId") == 0 ||
               strcmp(local, "gridCol") == 0 || strcmp(local, "tr") == 0 || strcmp(local, "tc") == 0 ||
               strcmp(local, "tcPr") == 0) {
        start_table_element(p, local, names, values);
    } else if (strcmp(local, "bodyPr") == 0 && p->cell == NULL) {
        start_body_props(p, names, values);
    } else if (strcmp(local, "normAutofit") == 0 && p->cell == NULL) {
        s->body.font_scale = attr_num(names, values, "fontScale", 100000) / 100000.0;
        s->body.set |= B_SCALE;
    } else if (strcmp(local, "p") == 0 && strcmp(up(p, 1), "txBody") == 0) {
        p->para = g_new0(RawPara, 1);
        p->para->runs = g_ptr_array_new_with_free_func(raw_run_free);
        g_ptr_array_add(p->cell != NULL ? p->cell->paras : s->paras, p->para);
        p->ls = &p->para->ppr;
    } else if (strcmp(local, "pPr") == 0 && p->para != NULL) {
        p->para->lvl = CLAMP((gint)attr_num(names, values, "lvl", 0), 0, MAX_LEVELS - 1);
        p->ls = &p->para->ppr;
        start_paragraph_props(p->ls, names, values);
    } else if ((strcmp(local, "r") == 0 || strcmp(local, "fld") == 0) && p->para != NULL) {
        p->run = g_new0(RawRun, 1);
        p->run->text = g_strdup("");
        g_ptr_array_add(p->para->runs, p->run);
        p->ls = &p->run->rpr;
    } else if (strcmp(local, "rPr") == 0 && p->run != NULL) {
        p->ls = &p->run->rpr;
        start_run_props(p->ls, names, values);
    } else if (strcmp(local, "endParaRPr") == 0 && p->para != NULL) {
        p->ls = &p->para->end;
        start_run_props(p->ls, names, values);
    } else if (strcmp(local, "br") == 0 && p->para != NULL) {
        RawRun* br = g_new0(RawRun, 1);
        g_ptr_array_add(p->para->runs, br);
        p->run = NULL;
        p->ls = &br->rpr;
    } else if (strcmp(local, "t") == 0 && p->run != NULL) {
        p->in_t = TRUE;
    }
}

static void end_shape(Parse* p) {
    PShape* s = p->cur;
    p->cur = NULL;
    p->ls = NULL;
    p->para = NULL;
    p->run = NULL;
    p->cell = NULL;
    p->row = NULL;
    if (s->hidden) {
        pshape_free(s);
        return;
    }
    g_ptr_array_add(p->tree->shapes, s);
}

static void tree_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)name;
    (void)error;
    Parse* p = data;
    if (p->depth > MAX_DEPTH) {
        p->depth--;
        return;
    }
    gchar* local = p->stack[--p->depth];
    if (p->cur != NULL && p->depth + 1 == p->cur_depth) {
        end_shape(p);
    } else if (strcmp(local, "grpSpPr") == 0 && strcmp(up(p, 0), "grpSp") == 0 && p->cur == NULL) {
        /* The group's child space mapped onto its box */
        const Xform* t = xform(p);
        gdouble rx = p->gx[3] > 0 && p->gx[1] > 0 ? p->gx[1] / p->gx[3] : 1.0;
        gdouble ry = p->gy[3] > 0 && p->gy[1] > 0 ? p->gy[1] / p->gy[3] : 1.0;
        Xform n = { t->sx * rx, t->sy * ry, t->tx + t->sx * (p->gx[0] - p->gx[2] * rx),
                    t->ty + t->sy * (p->gy[0] - p->gy[2] * ry) };
        g_array_append_val(p->xforms, n);
    } else if (strcmp(local, "grpSp") == 0 && p->xforms->len > 1) {
        g_array_set_size(p->xforms, p->xforms->len - 1);
    } else if (strcmp(local, "t") == 0) {
        p->in_t = FALSE;
    } else if (strcmp(local, "r") == 0 || strcmp(local, "fld") == 0) {
        p->run = NULL;
        p->ls = p->para != NULL ? &p->para->ppr : NULL;
    } else if (strcmp(local, "rPr") == 0 || strcmp(local, "endParaRPr") == 0 || strcmp(local, "br") == 0) {
        p->ls = p->run != NULL ? &p->run->rpr : p->para != NULL ? &p->para->ppr : NULL;
    } else if (strcmp(local, "p") == 0) {
        p->para = NULL;
        p->ls = NULL;
    } else if (g_str_has_prefix(local, "lvl") && g_str_has_suffix(local, "pPr")) {
        p->ls = NULL;
    } else if (strcmp(local, "tc") == 0) {
        p->cell = NULL;
    } else if (strcmp(local, "tr") == 0) {
        p->row = NULL;
    } else if (strcmp(local, "solidFill") == 0 || strcmp(local, "gs") == 0) {
        p->color = NULL;
    }
    g_free(local);
}

static void tree_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    Parse* p = data;
    if (p->in_t && p->run != NULL && strlen(p->run->text) < 65536) {
        gchar* joined = g_strdup_printf("%s%.*s", p->run->text, (gint)len, text);
        g_free(p->run->text);
        p->run->text = joined;
    }
}

PptxTree* pptx_tree_read(ZipReader* zip, const gchar* part, const PptxTheme* theme, const PptxTree* master) {
    PptxTree* t = g_new0(PptxTree, 1);
    t->shapes = g_ptr_array_new_with_free_func(pshape_free);
    t->bg = 0xFFFFFF;
    t->bg_end = SLIDE_NO_COLOR;
    t->show_master_sp = TRUE;
    static const gint DEFAULT_MAP[C_COUNT] = { C_DK1, C_LT1, C_DK2, C_LT2, C_ACCENT1, C_ACCENT2, C_ACCENT3,
                                               C_ACCENT4, C_ACCENT5, C_ACCENT6, C_HLINK, C_FOLHLINK };
    memcpy(t->clr_map, master != NULL ? master->clr_map : DEFAULT_MAP, sizeof t->clr_map);
    Parse p;
    memset(&p, 0, sizeof p);
    p.zip = zip;
    p.dir = zip_xml_dirname(part);
    p.rels = zip_xml_relationships(zip, part);
    p.theme = theme;
    p.tree = t;
    p.xforms = g_array_new(FALSE, FALSE, sizeof(Xform));
    Xform root = { 1 / EMU_PER_PT, 1 / EMU_PER_PT, 0, 0 };
    g_array_append_val(p.xforms, root);
    static const GMarkupParser parser = { tree_start, tree_end, tree_text, NULL, NULL };
    zip_xml_parse(zip, part, MAX_PART, TRUE, &parser, &p, NULL);
    if (p.cur != NULL) {
        end_shape(&p);
    }
    for (gint i = 0; i < p.depth && i < MAX_DEPTH; i++) {
        g_free(p.stack[i]);
    }
    g_array_free(p.xforms, TRUE);
    g_hash_table_destroy(p.rels);
    g_free(p.dir);
    return t;
}

void pptx_tree_free(PptxTree* tree) {
    if (tree == NULL) {
        return;
    }
    g_ptr_array_free(tree->shapes, TRUE);
    if (tree->bg_picture != NULL) g_bytes_unref(tree->bg_picture);
    for (gint k = 0; k < 3; k++) {
        for (gint i = 0; i < MAX_LEVELS; i++) {
            pptx_ls_clear(&tree->styles[k][i]);
        }
    }
    g_free(tree);
}

