/* Rendering helpers of Diff tabs: custom source line-number / change-bar
 * gutters and split/unified buffer formatting (see diff_tab_render.h). */
#include "preview/diff_tab_render.h"
#include "core/i18n.h"
#include "theme/palette.h"
#include "theme/text_tags.h"

#include <pango/pangocairo.h>
#include <string.h>

#define DIFF_UNIFIED_CTX   3
#define GUTTER_PAD_LEFT    8
#define GUTTER_PAD_RIGHT   8
#define GUTTER_BAR_WIDTH   3
#define GUTTER_MIN_DIGITS  2

/* ------------------------------------------------------------------------- */
/* Custom source line-number + diff bar gutter                               */
/* ------------------------------------------------------------------------- */

static gint gutter_wanted_width(DiffGutterPane* gp) {
    gint max_line = 1;
    SideDiffDoc* doc = gp->doc_ptr != NULL ? *gp->doc_ptr : NULL;
    if (doc != NULL && doc->rows->len > 0) {
        for (guint i = doc->rows->len; i-- > 0;) {
            SideDiffRow* r = g_ptr_array_index(doc->rows, i);
            gint n = gp->is_right ? r->right_line : r->left_line;
            if (n > 0) {
                max_line = n;
                break;
            }
        }
    }
    gint digits = 1;
    for (gint n = max_line; n >= 10; n /= 10) digits++;
    digits = MAX(digits, GUTTER_MIN_DIGITS);
    PangoLayout* layout = gtk_widget_create_pango_layout(gp->view, "0");
    gint digit_w = 0;
    pango_layout_get_pixel_size(layout, &digit_w, NULL);
    g_object_unref(layout);
    return GUTTER_PAD_LEFT + digits * MAX(digit_w, 6) + GUTTER_PAD_RIGHT + GUTTER_BAR_WIDTH;
}

void diff_gutter_pane_refresh(DiffGutterPane* gp) {
    if (gp == NULL || gp->area == NULL || gp->view == NULL) return;
    gint w = gutter_wanted_width(gp);
    if (w != gp->width) {
        gp->width = w;
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(gp->area), w);
        gtk_widget_set_size_request(gp->area, w, -1);
    }
    gtk_widget_queue_draw(gp->area);
}

static void gutter_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    DiffGutterPane* gp = data;
    if (gp == NULL || gp->view == NULL) return;

    double r, g, b;
    palette_rgb(PAL_BG_VIEW, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);

    SideDiffDoc* doc = gp->doc_ptr != NULL ? *gp->doc_ptr : NULL;
    if (doc == NULL || doc->rows->len == 0) return;

    GdkRectangle vis;
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(gp->view), &vis);
    GtkTextIter it;
    gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(gp->view), &it, vis.y, NULL);
    gint first = gtk_text_iter_get_line(&it);
    gint bottom = vis.y + vis.height;

    PangoLayout* layout = gtk_widget_create_pango_layout(gp->view, "");
    double fg_r, fg_g, fg_b;
    palette_rgb(PAL_FG_SUBTLE, &fg_r, &fg_g, &fg_b);

    for (gint line = first; line < (gint)doc->rows->len; line++) {
        gtk_text_buffer_get_iter_at_line(gp->buf, &it, line);
        gint y = 0, lh = 0;
        gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(gp->view), &it, &y, &lh);
        if (y > bottom) break;
        gint wy = 0;
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(gp->view), GTK_TEXT_WINDOW_LEFT, 0, y, NULL, &wy);
        if (wy + lh < 0 || wy > height) continue;

        SideDiffRow* row = g_ptr_array_index(doc->rows, (guint)line);
        gint src_line = gp->is_right ? row->right_line : row->left_line;

        if (src_line == 0) {
            palette_rgb(PAL_BG_BLOCK, &r, &g, &b);
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, 0, wy, width, lh);
            cairo_fill(cr);
        } else if (row->kind == SIDE_DIFF_ADDED || (row->kind == SIDE_DIFF_MODIFIED && gp->is_right)) {
            palette_rgb(PAL_DIFF_ADDED_BG, &r, &g, &b);
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, 0, wy, width, lh);
            cairo_fill(cr);
        } else if (row->kind == SIDE_DIFF_REMOVED || (row->kind == SIDE_DIFF_MODIFIED && !gp->is_right)) {
            palette_rgb(PAL_DIFF_REMOVED_BG, &r, &g, &b);
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, 0, wy, width, lh);
            cairo_fill(cr);
        }

        if (row->kind != SIDE_DIFF_EQUAL && src_line > 0) {
            PaletteRole bar_role = row->kind == SIDE_DIFF_MODIFIED ? PAL_GUTTER_MODIFIED
                                 : row->kind == SIDE_DIFF_ADDED    ? PAL_GUTTER_ADDED
                                                                   : PAL_GUTTER_DELETED;
            palette_rgb(bar_role, &r, &g, &b);
            cairo_set_source_rgb(cr, r, g, b);
            cairo_rectangle(cr, width - GUTTER_BAR_WIDTH, wy, GUTTER_BAR_WIDTH, lh);
            cairo_fill(cr);
        }

        if (src_line > 0) {
            gchar num[16];
            g_snprintf(num, sizeof(num), "%d", src_line);
            pango_layout_set_text(layout, num, -1);
            gint tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_set_source_rgb(cr, fg_r, fg_g, fg_b);
            cairo_move_to(cr, width - GUTTER_BAR_WIDTH - GUTTER_PAD_RIGHT - tw, wy + (lh - th) / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
    }
    g_object_unref(layout);
}

static void on_gutter_vadj_changed(GtkAdjustment* adj, gpointer data) {
    (void)adj;
    DiffGutterPane* gp = data;
    if (gp != NULL && gp->area != NULL) {
        gtk_widget_queue_draw(gp->area);
    }
}

void diff_gutter_pane_init(DiffGutterPane* gp, SideDiffDoc* const* doc_ptr,
                           GtkWidget* view, GtkTextBuffer* buf, GtkWidget* scroll,
                           gboolean is_right) {
    gp->doc_ptr = doc_ptr;
    gp->is_right = is_right;
    gp->view = view;
    gp->buf = buf;
    gp->area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(gp->area), gutter_draw, gp, NULL);
    gtk_text_view_set_gutter(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_LEFT, gp->area);
    gp->vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
    g_signal_connect(gp->vadj, "value-changed", G_CALLBACK(on_gutter_vadj_changed), gp);
    g_signal_connect(gp->vadj, "changed", G_CALLBACK(on_gutter_vadj_changed), gp);
    diff_gutter_pane_refresh(gp);
}

/* ------------------------------------------------------------------------- */
/* Split & Unified buffer rendering                                          */
/* ------------------------------------------------------------------------- */

static void ensure_diff_tags(GtkTextBuffer* buf) {
    text_tag_ensure(buf, "diff-added", "paragraph-background", "@diff_added_bg", NULL);
    text_tag_ensure(buf, "diff-removed", "paragraph-background", "@diff_removed_bg", NULL);
    text_tag_ensure(buf, "diff-filler", "paragraph-background", "@bg_block", NULL);
    text_tag_ensure(buf, "diff-added-word", "foreground", "@diff_added_fg",
                    "weight", PANGO_WEIGHT_BOLD, "underline", PANGO_UNDERLINE_SINGLE, NULL);
    text_tag_ensure(buf, "diff-removed-word", "foreground", "@diff_removed_fg",
                    "weight", PANGO_WEIGHT_BOLD, "underline", PANGO_UNDERLINE_SINGLE, NULL);
    text_tag_ensure(buf, "diff-hunk", "foreground", "@diff_hunk_fg",
                    "paragraph-background", "@diff_hunk_bg", "weight", PANGO_WEIGHT_BOLD, NULL);
    text_tag_ensure(buf, "diff-subtle", "foreground", "@fg_subtle", NULL);
}

static void apply_line_tag(GtkTextBuffer* buf, const gchar* tag, gint line) {
    GtkTextIter s, e;
    gtk_text_buffer_get_iter_at_line(buf, &s, line);
    e = s;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    if (!gtk_text_iter_is_end(&e)) gtk_text_iter_forward_char(&e);
    gtk_text_buffer_apply_tag_by_name(buf, tag, &s, &e);
}

static void apply_byte_spans(GtkTextBuffer* buf, const gchar* tag, gint line,
                             const gchar* line_text, GArray* spans) {
    if (spans == NULL || spans->len == 0) return;
    gsize line_len = strlen(line_text);
    for (guint i = 0; i < spans->len; i++) {
        SideDiffSpan sp = g_array_index(spans, SideDiffSpan, i);
        if (sp.start >= sp.end || sp.end > line_len) continue;
        glong cstart = g_utf8_pointer_to_offset(line_text, line_text + sp.start);
        glong cend = g_utf8_pointer_to_offset(line_text, line_text + sp.end);
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_line_offset(buf, &s, line, (gint)cstart);
        gtk_text_buffer_get_iter_at_line_offset(buf, &e, line, (gint)cend);
        gtk_text_buffer_apply_tag_by_name(buf, tag, &s, &e);
    }
}

void diff_render_split(SideDiffDoc* doc, GtkTextBuffer* left_buf, GtkTextBuffer* right_buf,
                       DiffGutterPane* left_gutter, DiffGutterPane* right_gutter) {
    GString* ls = g_string_sized_new(4096);
    GString* rs = g_string_sized_new(4096);
    for (guint i = 0; i < doc->rows->len; i++) {
        SideDiffRow* r = g_ptr_array_index(doc->rows, i);
        if (i > 0) {
            g_string_append_c(ls, '\n');
            g_string_append_c(rs, '\n');
        }
        g_string_append(ls, r->left_text);
        g_string_append(rs, r->right_text);
    }
    gtk_text_buffer_set_text(left_buf, ls->str, (gint)ls->len);
    gtk_text_buffer_set_text(right_buf, rs->str, (gint)rs->len);
    g_string_free(ls, TRUE);
    g_string_free(rs, TRUE);

    ensure_diff_tags(left_buf);
    ensure_diff_tags(right_buf);
    for (guint i = 0; i < doc->rows->len; i++) {
        SideDiffRow* r = g_ptr_array_index(doc->rows, i);
        if (r->kind == SIDE_DIFF_ADDED) {
            apply_line_tag(left_buf, "diff-filler", (gint)i);
            apply_line_tag(right_buf, "diff-added", (gint)i);
        } else if (r->kind == SIDE_DIFF_REMOVED) {
            apply_line_tag(left_buf, "diff-removed", (gint)i);
            apply_line_tag(right_buf, "diff-filler", (gint)i);
        } else if (r->kind == SIDE_DIFF_MODIFIED) {
            apply_line_tag(left_buf, "diff-removed", (gint)i);
            apply_line_tag(right_buf, "diff-added", (gint)i);
            apply_byte_spans(left_buf, "diff-removed-word", (gint)i, r->left_text, r->left_spans);
            apply_byte_spans(right_buf, "diff-added-word", (gint)i, r->right_text, r->right_spans);
        }
    }
    diff_gutter_pane_refresh(left_gutter);
    diff_gutter_pane_refresh(right_gutter);
}

void diff_render_unified(SideDiffDoc* doc, GtkTextBuffer* unified_buf,
                         GArray* unified_hunks, GArray* unified_src_lines,
                         GArray* unified_src_right) {
    g_array_set_size(unified_hunks, 0);
    g_array_set_size(unified_src_lines, 0);
    g_array_set_size(unified_src_right, 0);

    ensure_diff_tags(unified_buf);
    if (doc->hunks->len == 0) {
        gtk_text_buffer_set_text(unified_buf, _("Files are identical."), -1);
        apply_line_tag(unified_buf, "diff-subtle", 0);
        return;
    }

    GString* out = g_string_sized_new(4096);
    GArray* line_tags = g_array_new(FALSE, FALSE, sizeof(gint));
    guint n = doc->rows->len;

    guint h = 0;
    while (h < doc->hunks->len) {
        guint start = g_array_index(doc->hunks, guint, h);
        guint end = start;
        while (end < n && ((SideDiffRow*)g_ptr_array_index(doc->rows, end))->kind != SIDE_DIFF_EQUAL) {
            end++;
        }
        guint next_h = h + 1;
        while (next_h < doc->hunks->len) {
            guint ns = g_array_index(doc->hunks, guint, next_h);
            if (ns > end + 2 * DIFF_UNIFIED_CTX) break;
            end = ns;
            while (end < n && ((SideDiffRow*)g_ptr_array_index(doc->rows, end))->kind != SIDE_DIFF_EQUAL) {
                end++;
            }
            next_h++;
        }
        guint win_start = start > DIFF_UNIFIED_CTX ? start - DIFF_UNIFIED_CTX : 0;
        guint win_end = MIN(n, end + DIFF_UNIFIED_CTX);

        gint l_first = 0, r_first = 0;
        guint l_cnt = 0, r_cnt = 0;
        for (guint i = win_start; i < win_end; i++) {
            SideDiffRow* r = g_ptr_array_index(doc->rows, i);
            if (r->left_line > 0) {
                if (l_first == 0) l_first = r->left_line;
                l_cnt++;
            }
            if (r->right_line > 0) {
                if (r_first == 0) r_first = r->right_line;
                r_cnt++;
            }
        }

        guint u_hunk_line = line_tags->len;
        g_array_append_val(unified_hunks, u_hunk_line);

        if (out->len > 0) g_string_append_c(out, '\n');
        g_string_append_printf(out, "@@ -%d,%u +%d,%u @@", l_first, l_cnt, r_first, r_cnt);
        gint tag_hunk = 1;
        gint zero_line = r_first > 0 ? r_first : l_first;
        gboolean is_r = r_first > 0;
        g_array_append_val(line_tags, tag_hunk);
        g_array_append_val(unified_src_lines, zero_line);
        g_array_append_val(unified_src_right, is_r);

        guint i = win_start;
        while (i < win_end) {
            SideDiffRow* r = g_ptr_array_index(doc->rows, i);
            if (r->kind == SIDE_DIFF_EQUAL) {
                g_string_append_printf(out, "\n%5d %5d   %s", r->left_line, r->right_line, r->right_text);
                gint k = 0;
                gint sl = r->right_line;
                gboolean sr = TRUE;
                g_array_append_val(line_tags, k);
                g_array_append_val(unified_src_lines, sl);
                g_array_append_val(unified_src_right, sr);
                i++;
            } else {
                guint b_start = i;
                while (i < win_end && ((SideDiffRow*)g_ptr_array_index(doc->rows, i))->kind != SIDE_DIFF_EQUAL) {
                    i++;
                }
                for (guint j = b_start; j < i; j++) {
                    SideDiffRow* cr = g_ptr_array_index(doc->rows, j);
                    if (cr->left_line > 0) {
                        g_string_append_printf(out, "\n%5d       - %s", cr->left_line, cr->left_text);
                        gint k = 2;
                        gint sl = cr->left_line;
                        gboolean sr = FALSE;
                        g_array_append_val(line_tags, k);
                        g_array_append_val(unified_src_lines, sl);
                        g_array_append_val(unified_src_right, sr);
                    }
                }
                for (guint j = b_start; j < i; j++) {
                    SideDiffRow* cr = g_ptr_array_index(doc->rows, j);
                    if (cr->right_line > 0) {
                        g_string_append_printf(out, "\n      %5d + %s", cr->right_line, cr->right_text);
                        gint k = 3;
                        gint sl = cr->right_line;
                        gboolean sr = TRUE;
                        g_array_append_val(line_tags, k);
                        g_array_append_val(unified_src_lines, sl);
                        g_array_append_val(unified_src_right, sr);
                    }
                }
            }
        }
        h = next_h;
    }

    gtk_text_buffer_set_text(unified_buf, out->str, (gint)out->len);
    g_string_free(out, TRUE);
    for (guint i = 0; i < line_tags->len; i++) {
        gint k = g_array_index(line_tags, gint, i);
        if (k == 1) apply_line_tag(unified_buf, "diff-hunk", (gint)i);
        else if (k == 2) apply_line_tag(unified_buf, "diff-removed", (gint)i);
        else if (k == 3) apply_line_tag(unified_buf, "diff-added", (gint)i);
    }
    g_array_unref(line_tags);
}
