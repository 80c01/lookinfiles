/* Slides built from PowerPoint shape trees with their inheritance (see pptx_shapes.h) */
#include "office/pptx_shapes.h"
#include "office/pptx_shapes_private.h"

#include <string.h>

#include "preview/preview_doc.h"

#define MAX_TABLE_CELLS 4096

/* ---- Slides from the trees ---- */

static gint kind_of(const PShape* s) {
    if (!s->is_ph) return KIND_OTHER;
    const gchar* t = s->ph_type;
    if (t != NULL && (strcmp(t, "title") == 0 || strcmp(t, "ctrTitle") == 0)) return KIND_TITLE;
    if (t == NULL || strcmp(t, "body") == 0 || strcmp(t, "subTitle") == 0 || strcmp(t, "obj") == 0) return KIND_BODY;
    return KIND_OTHER;
}

/* Placeholder types that match in layouts and masters */
static const gchar* ph_family(const PShape* s) {
    const gchar* t = s->ph_type;
    if (t == NULL || strcmp(t, "obj") == 0 || strcmp(t, "subTitle") == 0) return "body";
    if (strcmp(t, "ctrTitle") == 0) return "title";
    return t;
}

static const PShape* find_placeholder(const PptxTree* tree, const PShape* s, gboolean by_idx) {
    if (tree == NULL || !s->is_ph) return NULL;
    if (by_idx && s->ph_idx >= 0) {
        for (guint i = 0; i < tree->shapes->len; i++) {
            const PShape* o = g_ptr_array_index(tree->shapes, i);
            if (o->is_ph && o->ph_idx == s->ph_idx) return o;
        }
    }
    for (guint i = 0; i < tree->shapes->len; i++) {
        const PShape* o = g_ptr_array_index(tree->shapes, i);
        if (o->is_ph && g_strcmp0(o->ph_type, s->ph_type) == 0) return o;
    }
    for (guint i = 0; i < tree->shapes->len; i++) {
        const PShape* o = g_ptr_array_index(tree->shapes, i);
        if (o->is_ph && strcmp(ph_family(o), ph_family(s)) == 0) return o;
    }
    return NULL;
}

/* Built-in look of a level: the Office master */
static void default_style(gint kind, const gchar* ph_type, gint lvl, const PptxTheme* theme, const PptxTree* master,
                          LevelStyle* out) {
    memset(out, 0, sizeof *out);
    out->color = theme->colors[master != NULL ? master->clr_map[0] : C_DK1];
    out->font = g_strdup(kind == KIND_TITLE ? theme->major : theme->minor);
    if (kind == KIND_TITLE) {
        out->size = 44;
        out->line = 0.9;
    } else if (kind == KIND_BODY) {
        static const gdouble SIZES[] = { 28, 24, 20, 18, 18 };
        static const gchar* const BULLETS[] = { "•", "•", "•", "•", "•" };
        gint l = MIN(lvl, 4);
        out->size = SIZES[l];
        out->bullet = g_strdup(BULLETS[l]);
        out->marl = 18 + 36 * lvl;
        out->indent = -18;
        out->before = -0.2;
        out->line = 0.9;
    } else {
        out->size = 18;
    }
    /* Title slide: centred title and subtitle, no bullet */
    if (g_strcmp0(ph_type, "ctrTitle") == 0 || g_strcmp0(ph_type, "subTitle") == 0) {
        out->align = SLIDE_ALIGN_CENTER;
        g_clear_pointer(&out->bullet, g_free);
        out->marl = out->indent = 0;
        if (kind == KIND_TITLE) out->size = 54;
    }
}

static gchar* auto_number(const gchar* scheme, gint n) {
    GString* s = g_string_new(NULL);
    gboolean alpha = strstr(scheme, "alpha") != NULL, roman = strstr(scheme, "roman") != NULL;
    gboolean upper = strstr(scheme, "Uc") != NULL;
    if (alpha) {
        g_string_append_c(s, (gchar)((upper ? 'A' : 'a') + (n - 1) % 26));
    } else if (roman) {
        static const gchar* const R[] = { "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x",
                                          "xi", "xii", "xiii", "xiv", "xv", "xvi", "xvii", "xviii", "xix", "xx" };
        gchar* r = g_strdup(n >= 1 && n <= 20 ? R[n - 1] : "x");
        if (upper) {
            gchar* u = g_ascii_strup(r, -1);
            g_free(r);
            r = u;
        }
        g_string_append(s, r);
        g_free(r);
    } else {
        g_string_append_printf(s, "%d", n);
    }
    if (g_str_has_suffix(scheme, "ParenBoth")) {
        g_string_prepend_c(s, '(');
        g_string_append_c(s, ')');
    } else if (g_str_has_suffix(scheme, "ParenR")) {
        g_string_append_c(s, ')');
    } else if (g_str_has_suffix(scheme, "Period")) {
        g_string_append_c(s, '.');
    }
    return g_string_free(s, FALSE);
}

/* Paragraphs with their inherited styles: chain = master text style, then
 * the master and layout placeholders' list styles, then the shape's */
static void build_paragraphs(GPtrArray* raw, GPtrArray* out, gint kind, const gchar* ph_type,
                             const LevelStyle* const* chains, gint n_chain,
                             const PptxTheme* theme, const PptxTree* master, guint32 font_color, gboolean bold) {
    gint counters[MAX_LEVELS] = { 0 };
    for (guint i = 0; i < raw->len; i++) {
        const RawPara* rp = g_ptr_array_index(raw, i);
        LevelStyle eff;
        default_style(kind, ph_type, rp->lvl, theme, master, &eff);
        for (gint k = 0; k < n_chain; k++) {
            if (chains[k] != NULL) {
                pptx_ls_merge(&eff, &chains[k][rp->lvl]);
            }
            if (k == 0 && font_color != SLIDE_NO_COLOR) {
                eff.color = font_color;
            }
        }
        if (bold) eff.bold = TRUE;
        pptx_ls_merge(&eff, &rp->ppr);
        SlideParagraph* sp = slide_paragraph_new();
        sp->align = eff.align;
        sp->left = eff.marl;
        sp->indent = eff.indent;
        sp->line_spacing = eff.line;
        sp->size = (rp->end.set & LS_SIZE) ? rp->end.size : eff.size;
        sp->space_before = eff.before >= 0 ? eff.before : -eff.before * eff.size * 1.2;
        sp->space_after = eff.after >= 0 ? eff.after : -eff.after * eff.size * 1.2;
        gboolean has_text = FALSE;
        for (guint k = 0; k < rp->runs->len; k++) {
            const RawRun* rr = g_ptr_array_index(rp->runs, k);
            LevelStyle rs = eff;
            rs.font = g_strdup(eff.font);
            rs.bullet = g_strdup(eff.bullet);
            rs.bullet_font = g_strdup(eff.bullet_font);
            pptx_ls_merge(&rs, &rr->rpr);
            SlideRun* run = slide_paragraph_add_run(sp, rr->text != NULL ? rr->text : "\n", -1);
            run->font = g_strdup(rs.font);
            run->size = rs.size > 0 ? rs.size : 18;
            run->bold = rs.bold;
            run->italic = rs.italic;
            run->underline = rs.underline;
            run->strike = rs.strike;
            run->baseline = rs.baseline;
            run->color = rs.color;
            has_text |= rr->text != NULL && rr->text[0] != '\0';
            pptx_ls_clear(&rs);
        }
        if (eff.bullet != NULL && has_text) {
            if (eff.bullet[0] == '#') {
                counters[rp->lvl]++;
                sp->bullet = auto_number(eff.bullet + 1, counters[rp->lvl]);
            } else {
                sp->bullet = preview_symbol_text(eff.bullet, eff.bullet_font);
                sp->bullet_font = NULL;
            }
            if (eff.set & LS_BULLET_COLOR) sp->bullet_color = eff.bullet_color;
            sp->bullet_scale = (eff.set & LS_BULLET_SCALE) ? eff.bullet_scale : 0;
        }
        for (gint l = rp->lvl + 1; l < MAX_LEVELS; l++) {
            counters[l] = 0;
        }
        g_ptr_array_add(out, sp);
        pptx_ls_clear(&eff);
    }
}

/* Box of a placeholder nobody positioned: the Office layouts */
static void default_box(const PShape* s, gdouble w, gdouble h, SlideShape* out) {
    const gchar* t = s->ph_type != NULL ? s->ph_type : "body";
    gdouble box[4] = { 0.05, 0.233, 0.9, 0.66 };
    if (strcmp(t, "ctrTitle") == 0) {
        gdouble b[4] = { 0.075, 0.31, 0.85, 0.215 };
        memcpy(box, b, sizeof box);
    } else if (strcmp(t, "subTitle") == 0) {
        gdouble b[4] = { 0.15, 0.57, 0.7, 0.25 };
        memcpy(box, b, sizeof box);
    } else if (strcmp(t, "title") == 0) {
        gdouble b[4] = { 0.05, 0.04, 0.9, 0.167 };
        memcpy(box, b, sizeof box);
    }
    out->x = box[0] * w;
    out->y = box[1] * h;
    out->w = box[2] * w;
    out->h = box[3] * h;
}

static SlideTable* build_table(const PShape* s, const PptxTheme* theme, const PptxTree* master) {
    guint rows = s->rows->len, cols = s->grid->len;
    for (guint r = 0; r < rows; r++) {
        cols = MAX(cols, ((GPtrArray*)g_ptr_array_index(s->rows, r))->len);
    }
    if (rows == 0 || cols == 0 || rows * cols > MAX_TABLE_CELLS) return NULL;
    SlideTable* t = slide_table_new(rows, cols);
    gdouble width = s->shape->w > 0 ? s->shape->w : 400;
    for (guint c = 0; c < cols; c++) {
        t->col_widths[c] = c < s->grid->len && g_array_index(s->grid, gdouble, c) > 0
                               ? g_array_index(s->grid, gdouble, c) : width / cols;
    }
    gboolean styled = s->table_styled || s->first_row;
    guint32 accent = theme->colors[C_ACCENT1];
    t->border = styled ? theme->colors[C_LT1] : theme->colors[C_DK1];
    const LevelStyle* chain[1] = { master != NULL ? master->styles[KIND_OTHER] : NULL };
    for (guint r = 0; r < rows; r++) {
        GPtrArray* row = g_ptr_array_index(s->rows, r);
        t->row_heights[r] = r < s->heights->len ? g_array_index(s->heights, gdouble, r) : 0;
        gboolean header = styled && s->first_row && r == 0;
        for (guint c = 0; c < cols && c < row->len; c++) {
            RawCell* rc = g_ptr_array_index(row, c);
            SlideCell* cell = slide_table_cell(t, r, c);
            cell->col_span = rc->col_span;
            cell->row_span = rc->row_span;
            cell->merged = rc->merged;
            cell->anchor = rc->anchor;
            cell->fill = rc->fill;
            if (cell->fill == SLIDE_NO_COLOR && styled) {
                /* Medium Style 2: a header row, then banded rows */
                gint band = (gint)r - (s->first_row ? 1 : 0);
                cell->fill = header ? accent : pptx_tint(accent, s->band_rows && band % 2 == 0 ? 0.4 : 0.2);
            }
            build_paragraphs(rc->paras, cell->paragraphs, KIND_OTHER, NULL, chain, 1, theme, master,
                             header ? theme->colors[C_LT1] : SLIDE_NO_COLOR, header);
        }
    }
    return t;
}

/* A slide shape from the shape and its layout and master placeholders */
static SlideShape* build_shape(const PShape* s, const PShape* lp, const PShape* mp, const PptxTree* master,
                               const PptxTheme* theme, gdouble width, gdouble height) {
    SlideShape* out = slide_shape_new();
    const SlideShape* src = s->shape;
    out->rotation = src->rotation;
    out->flip_h = src->flip_h;
    out->flip_v = src->flip_v;
    out->geometry = src->geometry;
    if (src->path != NULL) {
        out->path = g_array_copy(src->path);
        out->path_w = src->path_w;
        out->path_h = src->path_h;
    }
    const PShape* pos = s->has_xfrm ? s : lp != NULL && lp->has_xfrm ? lp : mp != NULL && mp->has_xfrm ? mp : NULL;
    if (pos != NULL) {
        out->x = pos->shape->x;
        out->y = pos->shape->y;
        out->w = pos->shape->w;
        out->h = pos->shape->h;
    } else if (s->is_ph) {
        default_box(s, width, height, out);
    }
    /* Fill and outline: the shape's, its placeholders', its style */
    const PShape* fill = s->fill_set ? s : lp != NULL && lp->fill_set ? lp : mp != NULL && mp->fill_set ? mp : NULL;
    out->fill = fill != NULL ? fill->shape->fill : s->ref_fill;
    out->fill_end = fill != NULL ? fill->shape->fill_end : SLIDE_NO_COLOR;
    out->gradient_angle = fill != NULL ? fill->shape->gradient_angle : 0;
    const PShape* line = s->line_set ? s : lp != NULL && lp->line_set ? lp : mp != NULL && mp->line_set ? mp : NULL;
    out->line = line != NULL ? line->shape->line : s->ref_line;
    out->line_width = src->line_width;
    if (src->picture != NULL) out->picture = g_bytes_ref(src->picture);

    BodyProps body = { 0, kind_of(s) == KIND_TITLE ? SLIDE_ANCHOR_MIDDLE : SLIDE_ANCHOR_TOP, 7.2, 3.6, 7.2, 3.6,
                       FALSE, 1.0 };
    if (g_strcmp0(s->ph_type, "ctrTitle") == 0) body.anchor = SLIDE_ANCHOR_BOTTOM;
    if (mp != NULL) pptx_body_merge(&body, &mp->body);
    if (lp != NULL) pptx_body_merge(&body, &lp->body);
    pptx_body_merge(&body, &s->body);
    out->anchor = body.anchor;
    out->inset_left = body.left;
    out->inset_top = body.top;
    out->inset_right = body.right;
    out->inset_bottom = body.bottom;
    out->no_wrap = body.no_wrap;
    out->font_scale = body.font_scale > 0 ? body.font_scale : 1.0;

    gint kind = kind_of(s);
    const LevelStyle* chain[4] = { master != NULL ? master->styles[kind] : NULL, mp != NULL ? mp->lst : NULL,
                                   lp != NULL ? lp->lst : NULL, s->lst };
    build_paragraphs(s->paras, out->paragraphs, kind, s->ph_type, chain, 4, theme, master, s->ref_font, FALSE);
    if (s->rows != NULL) {
        out->table = build_table(s, theme, master);
    }
    if (s->label != NULL && out->table == NULL) {
        SlideParagraph* para = slide_paragraph_new();
        para->align = SLIDE_ALIGN_CENTER;
        SlideRun* run = slide_paragraph_add_run(para, s->label, -1);
        run->color = theme->colors[C_ACCENT3];
        run->font = g_strdup(theme->minor);
        g_ptr_array_add(out->paragraphs, para);
        out->anchor = SLIDE_ANCHOR_MIDDLE;
        out->line = theme->colors[C_ACCENT3];
    }
    return out;
}

static gboolean is_drawn_placeholder(const PShape* s) {
    /* Placeholders of masters and layouts are prompts, not content */
    return !s->is_ph;
}

static void add_tree_shapes(Slide* slide, const PptxTree* tree, const PptxTree* master, const PptxTheme* theme,
                            gdouble width, gdouble height) {
    for (guint i = 0; i < tree->shapes->len; i++) {
        const PShape* s = g_ptr_array_index(tree->shapes, i);
        if (is_drawn_placeholder(s) && s->has_xfrm) {
            g_ptr_array_add(slide->shapes, build_shape(s, NULL, NULL, master, theme, width, height));
        }
    }
}

Slide* pptx_build_slide(const PptxTree* tree, const PptxTree* layout, const PptxTree* master,
                        const PptxTheme* theme, gdouble width, gdouble height) {
    Slide* slide = slide_new();
    const PptxTree* bg = tree->has_bg ? tree : layout != NULL && layout->has_bg ? layout
                       : master != NULL && master->has_bg ? master : NULL;
    if (bg != NULL) {
        slide->background = bg->bg;
        slide->background_end = bg->bg_end;
        if (bg->bg_picture != NULL) slide->background_picture = g_bytes_ref(bg->bg_picture);
    } else {
        slide->background = theme->colors[master != NULL ? master->clr_map[1] : C_LT1];
    }
    gboolean show_layout = tree->show_master_sp;
    gboolean show_master = show_layout && (layout == NULL || layout->show_master_sp);
    if (master != NULL && show_master) add_tree_shapes(slide, master, master, theme, width, height);
    if (layout != NULL && show_layout) add_tree_shapes(slide, layout, master, theme, width, height);

    /* Shapes nobody positioned (minimal files) share the body's area */
    GPtrArray* loose = g_ptr_array_new();
    SlideShape* body_box = NULL;
    for (guint i = 0; i < tree->shapes->len; i++) {
        const PShape* s = g_ptr_array_index(tree->shapes, i);
        const PShape* lp = find_placeholder(layout, s, TRUE);
        const PShape* mp = find_placeholder(master, lp != NULL ? lp : s, FALSE);
        SlideShape* out = build_shape(s, lp, mp, master, theme, width, height);
        gboolean positioned = s->has_xfrm || (lp != NULL && lp->has_xfrm) || (mp != NULL && mp->has_xfrm);
        if (!s->is_ph && !positioned) {
            g_ptr_array_add(loose, out);
        } else {
            if (s->is_ph && !positioned && kind_of(s) == KIND_BODY) body_box = out;
            g_ptr_array_add(slide->shapes, out);
        }
        if (slide->title == NULL && kind_of(s) == KIND_TITLE) {
            gchar* text = slide_shape_text(out);
            if (text[0] != '\0') slide->title = text;
            else g_free(text);
        }
    }
    if (loose->len > 0) {
        gdouble box[4] = { 0.05 * width, 0.233 * height, 0.9 * width, 0.66 * height };
        if (body_box != NULL && body_box->paragraphs->len > 0) {
            body_box->w = box[2] / 2 - 9;
            box[0] += box[2] / 2 + 9;
            box[2] = box[2] / 2 - 9;
        }
        gdouble each = box[3] / loose->len;
        for (guint i = 0; i < loose->len; i++) {
            SlideShape* s = g_ptr_array_index(loose, i);
            s->x = box[0];
            s->y = box[1] + each * i;
            s->w = box[2];
            s->h = each - 9;
            if (s->table != NULL) {
                for (guint c = 0; c < s->table->cols; c++) s->table->col_widths[c] = s->w / s->table->cols;
            }
            g_ptr_array_add(slide->shapes, s);
        }
    }
    g_ptr_array_free(loose, TRUE);
    return slide;
}

gchar* pptx_tree_notes(const PptxTree* notes) {
    for (guint i = 0; i < notes->shapes->len; i++) {
        const PShape* s = g_ptr_array_index(notes->shapes, i);
        if (s->is_ph && g_strcmp0(s->ph_type, "body") == 0) {
            GString* out = g_string_new(NULL);
            for (guint k = 0; k < s->paras->len; k++) {
                const RawPara* p = g_ptr_array_index(s->paras, k);
                GString* line = g_string_new(NULL);
                for (guint r = 0; r < p->runs->len; r++) {
                    const RawRun* run = g_ptr_array_index(p->runs, r);
                    g_string_append(line, run->text != NULL ? run->text : " ");
                }
                g_strstrip(line->str);
                if (line->str[0] != '\0') {
                    if (out->len > 0) g_string_append_c(out, '\n');
                    g_string_append(out, line->str);
                }
                g_string_free(line, TRUE);
            }
            if (out->len > 0) return g_string_free(out, FALSE);
            g_string_free(out, TRUE);
        }
    }
    return NULL;
}
