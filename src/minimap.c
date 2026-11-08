#include "minimap.h"
#include "diff_gutter.h"
#include "line_diff.h"
#include "minimap_geometry.h"
#include "palette.h"

static void set_source_role(cairo_t* cr, PaletteRole role, double alpha) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, alpha);
}

/* The minimap is a scaled drawing of the text view as laid out on screen:
 * wrapped lines, font sizes, line backgrounds (code panels, quotes), embedded
 * widgets (tables) and images, not the raw lines of the buffer. As in VS
 * Code, a long document keeps a readable scale and the minimap scrolls with
 * the view (minimap_geometry); only the part around what is shown is drawn. */

/* Height of the offscreen render, in minimap heights: scrolling within it
 * only repaints, past it the render is rebuilt around the new position */
#define MINIMAP_CACHE_SCREENS 3
/* A visibility change that may come from a height estimate is checked again
 * one frame later, at most MINIMAP_MAX_WAITS times (about 100 ms) */
#define MINIMAP_SETTLE_MS 16
#define MINIMAP_MAX_WAITS 6
#define MINIMAP_MIN_LINE_PX 10
/* Longest time the previous render stands in for a layout in progress */
#define MINIMAP_LAYOUT_WAIT_US (200 * 1000)

/* Scale, minimap scroll and slider for the view's current position */
static gboolean is_paged_view(GtkTextView* view) {
    if (view == NULL) {
        return FALSE;
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    if (buffer != NULL && g_object_get_data(G_OBJECT(buffer), "minimap_paged") != NULL) {
        return TRUE;
    }
    if (buffer != NULL) {
        GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(buffer);
        if (tags != NULL && gtk_text_tag_table_lookup(tags, "pdf_page_badge") != NULL) {
            return TRUE;
        }
    }
    AppData* app = g_object_get_data(G_OBJECT(view), "app");
    if (app != NULL && !app->render_raw &&
        (app->viewed_kind == FILE_KIND_PDF || app->viewed_kind == FILE_KIND_PRESENTATION)) {
        return TRUE;
    }
    return FALSE;
}

static gboolean minimap_geometry(GtkWidget* minimap, GtkTextView* view, MinimapGeometry* g) {
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    if (vadj == NULL) {
        *g = (MinimapGeometry){0};
        return FALSE;
    }
    gdouble lower = gtk_adjustment_get_lower(vadj);
    gdouble upper = gtk_adjustment_get_upper(vadj) - lower;
    gdouble page = gtk_adjustment_get_page_size(vadj);
    gdouble value = gtk_adjustment_get_value(vadj) - lower;
    gdouble height = gtk_widget_get_height(minimap);

    if (is_paged_view(view)) {
        gint view_w = gtk_widget_get_width(GTK_WIDGET(view));
        gint mm_w = gtk_widget_get_width(minimap);
        if (mm_w <= 0) {
            mm_w = MINIMAP_WIDTH;
        }
        if (view_w > 0) {
            gdouble sx = (gdouble)(mm_w - 4) / view_w;
            sx = MIN(sx, MINIMAP_MAX_SCALE);
            if (sx > 0.0) {
                return minimap_geometry_compute_fixed(upper, page, value, height, sx, g);
            }
        }
    }
    return minimap_geometry_compute(upper, page, value, height, g);
}

/* Colours applied at an iterator: text foreground and paragraph background
 * (highest-priority tag wins) */
static void tag_colors(const GtkTextIter* it, GdkRGBA* fg, gboolean* has_fg,
                       GdkRGBA* bg, gboolean* has_bg) {
    *has_fg = FALSE;
    *has_bg = FALSE;
    GSList* tags = gtk_text_iter_get_tags(it); /* ascending priority */
    for (GSList* l = tags; l != NULL; l = l->next) {
        gboolean fg_set = FALSE, bg_set = FALSE;
        g_object_get(l->data, "foreground-set", &fg_set, "paragraph-background-set", &bg_set, NULL);
        if (fg_set) {
            GdkRGBA* c = NULL;
            g_object_get(l->data, "foreground-rgba", &c, NULL);
            if (c != NULL) {
                *fg = *c;
                *has_fg = TRUE;
                gdk_rgba_free(c);
            }
        }
        if (bg_set) {
            GdkRGBA* c = NULL;
            g_object_get(l->data, "paragraph-background-rgba", &c, NULL);
            if (c != NULL) {
                *bg = *c;
                *has_bg = TRUE;
                gdk_rgba_free(c);
            }
        }
    }
    g_slist_free(tags);
}

/* A texture (PDF page) painted at the minimap scale. The downscaled copy is
 * kept on the texture so that redraws do not download it again. */
static gboolean draw_texture(cairo_t* cc, GdkTexture* tex, gdouble x, gdouble y, gdouble w, gdouble h) {
    (void)h;
    gint tw = gdk_texture_get_width(tex), th = gdk_texture_get_height(tex);
    if (tw <= 0 || th <= 0 || w < 1.0) {
        return FALSE;
    }
    gint dw = MAX(1, (gint)(w + 0.5));
    gdouble scale = (gdouble)dw / tw;
    gint dh = MAX(1, (gint)(th * scale + 0.5));
    cairo_surface_t* small = g_object_get_data(G_OBJECT(tex), "minimap-thumb");
    if (small == NULL || cairo_image_surface_get_width(small) != dw ||
        cairo_image_surface_get_height(small) != dh) {
        cairo_surface_t* full = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
        gdk_texture_download(tex, cairo_image_surface_get_data(full),
                             cairo_image_surface_get_stride(full));
        cairo_surface_mark_dirty(full);
        small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
        cairo_t* sc = cairo_create(small);
        cairo_scale(sc, scale, scale);
        cairo_set_source_surface(sc, full, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(sc), CAIRO_FILTER_BEST);
        cairo_paint(sc);
        cairo_destroy(sc);
        cairo_surface_destroy(full);
        g_object_set_data_full(G_OBJECT(tex), "minimap-thumb", small,
                               (GDestroyNotify)cairo_surface_destroy);
    }
    cairo_save(cc);
    cairo_set_source_surface(cc, small, x, y);
    cairo_rectangle(cc, x, y, dw, dh);
    cairo_fill(cc);
    cairo_restore(cc);
    return TRUE;
}

/* One run of text (same tags) of a display line */
static void draw_run(cairo_t* cc, PangoLayout* layout, GtkTextView* view, GtkTextIter* rs,
                     GtkTextIter* re, gdouble sx, gdouble sy, gdouble font_scale) {
    GdkRectangle loc;
    gtk_text_view_get_iter_location(view, rs, &loc);

    GtkTextChildAnchor* anchor = gtk_text_iter_get_child_anchor(rs);
    GdkPaintable* paintable = gtk_text_iter_get_paintable(rs);
    if (anchor != NULL || paintable != NULL) {
        gint w = loc.width, h = loc.height;
        if (anchor != NULL) {
            guint n = 0;
            GtkWidget** widgets = gtk_text_child_anchor_get_widgets(anchor, &n);
            if (n > 0) {
                w = gtk_widget_get_width(widgets[0]);
                h = gtk_widget_get_height(widgets[0]);
            }
            g_free(widgets);
        }
        if (paintable != NULL && GDK_IS_TEXTURE(paintable)) {
            GdkTexture* tex = GDK_TEXTURE(paintable);
            gint tw = gdk_texture_get_width(tex), th = gdk_texture_get_height(tex);
            if (w <= 0) {
                w = tw;
            }
            if (h <= 0) {
                h = th;
            }
            gint mm_width = cairo_image_surface_get_width(cairo_get_target(cc));
            gdouble max_w = (mm_width > 4) ? (gdouble)(mm_width - 4) : w * sx;
            gdouble tex_w = MIN(w * sx, max_w);
            gdouble tex_x = CLAMP(2.0 + loc.x * sx, 2.0, (gdouble)(mm_width - 2) - tex_w);
            if (draw_texture(cc, tex, tex_x, loc.y * sy, tex_w, h * sy)) {
                return;
            }
        }
        set_source_role(cc, PAL_RULE, 1.0); /* table / image block */
        cairo_rectangle(cc, 2.0 + loc.x * sx, loc.y * sy, MAX(1.0, w * sx), MAX(1.0, h * sy));
        cairo_fill(cc);
        return;
    }

    gchar* text = gtk_text_iter_get_text(rs, re);
    g_strdelimit(text, "\n\t", ' ');
    if (g_strstrip(text)[0] == '\0') {
        g_free(text);
        return;
    }
    GdkRGBA fg, bg;
    gboolean has_fg, has_bg;
    tag_colors(rs, &fg, &has_fg, &bg, &has_bg);
    if (has_fg) {
        cairo_set_source_rgba(cc, fg.red, fg.green, fg.blue, 0.9);
    } else {
        set_source_role(cc, PAL_FG, 0.75);
    }
    /* Skip the leading spaces that were stripped: start where the text starts */
    GtkTextIter first = *rs;
    while (gtk_text_iter_compare(&first, re) < 0 && g_unichar_isspace(gtk_text_iter_get_char(&first))) {
        gtk_text_iter_forward_char(&first);
    }
    gtk_text_view_get_iter_location(view, &first, &loc);
    gdouble px = loc.height * font_scale;
    if (px >= 2.5) {
        PangoFontDescription* fd = pango_font_description_from_string("Sans");
        pango_font_description_set_absolute_size(fd, px * 0.8 * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, text, -1);
        cairo_move_to(cc, 2.0 + loc.x * sx, loc.y * sy + (loc.height * sy - px) / 2.0);
        pango_cairo_show_layout(cc, layout);
    } else {
        /* Too small for glyphs: one block per word, as in VS Code, on whole
         * pixel rows so that lines stay apart (a 2 px line: 1 px of ink) */
        GdkRectangle end_loc;
        gtk_text_view_get_iter_location(view, re, &end_loc);
        gint n = gtk_text_iter_get_offset(re) - gtk_text_iter_get_offset(&first);
        gdouble cw = (end_loc.y == loc.y && end_loc.x > loc.x && n > 0)
                     ? (gdouble)(end_loc.x - loc.x) / n : loc.height * 0.5;
        gdouble line_h = loc.height * sy;
        gint h = MAX(1, (gint)(line_h * 0.6));
        gint y = (gint)(loc.y * sy + (line_h - h) / 2.0 + 0.5);
        gint col = 0, word = -1;
        for (const gchar* p = text;; p = g_utf8_next_char(p), col++) {
            gboolean ink = (*p != '\0' && !g_unichar_isspace(g_utf8_get_char(p)));
            if (ink && word < 0) {
                word = col;
            } else if (!ink && word >= 0) {
                cairo_rectangle(cc, 2.0 + (loc.x + word * cw) * sx, y,
                                MAX(1.0, (col - word) * cw * sx), h);
                word = -1;
            }
            if (*p == '\0') {
                break;
            }
        }
        cairo_fill(cc);
    }
    g_free(text);
}

void minimap_cache_clear(MinimapCache* cache) {
    if (cache->surface != NULL) {
        cairo_surface_destroy(cache->surface);
    }
    *cache = (MinimapCache){0};
}

/* The text changed: the render is redrawn on the next draw, but stays
 * available to stand in while the new layout is incomplete */
static void minimap_cache_mark_stale(MinimapCache* cache) {
    cache->stale = TRUE;
}

/* Does the render cover what the minimap shows now? */
static gboolean cache_covers(const MinimapCache* c, const MinimapGeometry* g, gint width, gint height) {
    return c->surface != NULL && !c->stale && c->width == width && c->scale == g->scale &&
           g->offset >= c->top - 0.5 && g->offset + height <= c->top + c->height + 0.5;
}

/* Whether a line is hidden on purpose (folded Org heading): its zero height
 * is final */
static gboolean line_invisible(const GtkTextIter* it) {
    gboolean hidden = FALSE;
    GSList* tags = gtk_text_iter_get_tags(it);
    for (GSList* l = tags; l != NULL; l = l->next) {
        gboolean set = FALSE, invisible = FALSE;
        g_object_get(l->data, "invisible-set", &set, "invisible", &invisible, NULL);
        if (set) {
            hidden = invisible;
        }
    }
    g_slist_free(tags);
    return hidden;
}

/* Whether GtkTextView has laid out every line the minimap would draw for g.
 * After new text is set it measures the lines in the background: a line not
 * measured yet has no height, so it and every line after it would be drawn
 * piled at one spot, and the minimap would flash a cut-off document. */
static gboolean layout_complete(GtkTextBuffer* buffer, GtkTextView* view, const MinimapGeometry* g,
                                gint height) {
    gdouble sy = g->scale;
    gdouble top = MAX(0.0, g->offset - height * (MINIMAP_CACHE_SCREENS - 1) / 2.0);
    gdouble bottom = g->offset + height * (MINIMAP_CACHE_SCREENS + 1) / 2.0;
    GtkTextIter it;
    gtk_text_view_get_line_at_y(view, &it, (gint)(top / sy), NULL);
    gint lines = gtk_text_buffer_get_line_count(buffer);
    for (gint i = gtk_text_iter_get_line(&it); i < lines; i++) {
        gtk_text_buffer_get_iter_at_line(buffer, &it, i);
        gint ly = 0, lh = 0;
        gtk_text_view_get_line_yrange(view, &it, &ly, &lh);
        if (lh == 0 && !line_invisible(&it)) {
            return FALSE;
        }
        if ((ly + lh) * sy > bottom) {
            break;
        }
    }
    return TRUE;
}

/* Draw the document lines around the minimap's position into an offscreen
 * image surface (cached) */
static void minimap_build_cache(GtkTextBuffer* buffer, GtkTextView* view, MinimapCache* c,
                                gint width, gint height, const MinimapGeometry* g) {
    minimap_cache_clear(c);
    gint view_w = gtk_widget_get_width(GTK_WIDGET(view));
    if (width <= 0 || height <= 0 || g->scale <= 0.0 || view_w <= 0) {
        return;
    }
    gdouble sy = g->scale;
    gdouble want = MIN(g->content, (gdouble)height * MINIMAP_CACHE_SCREENS);
    gint ch = (gint)want + ((gdouble)(gint)want < want ? 1 : 0);
    ch = MAX(ch, height);
    c->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, ch);
    c->width = width;
    c->height = ch;
    c->scale = sy;
    /* Centered on the shown part, within the content */
    c->top = (g->content <= ch) ? 0.0
             : (gdouble)(gint)CLAMP(g->offset - (ch - height) / 2.0, 0.0, g->content - ch);

    cairo_t* cc = cairo_create(c->surface);
    /* Background identical to the preview pane */
    set_source_role(cc, PAL_BG_VIEW, 1.0);
    cairo_paint(cc);
    cairo_translate(cc, 0, -c->top);

    gdouble sx = (gdouble)(width - 4) / view_w;
    gdouble font_scale = MIN(sx, sy);
    PangoLayout* layout = pango_cairo_create_layout(cc);

    /* Buffer lines in the drawn range */
    gdouble range_end = c->top + ch;
    GtkTextIter first_line, last_line;
    gtk_text_view_get_line_at_y(view, &first_line, (gint)(c->top / sy), NULL);
    gtk_text_view_get_line_at_y(view, &last_line, (gint)(range_end / sy), NULL);
    gint first = gtk_text_iter_get_line(&first_line);
    gint last = gtk_text_iter_get_line(&last_line);
    gboolean detailed = (last - first <= 5000); /* very dense: one bar per line */
#ifdef HAVE_GSV
    /* GtkSourceView colours only what has been shown: colour the drawn lines */
    if (detailed && GTK_SOURCE_IS_BUFFER(buffer)) {
        if (!gtk_text_iter_ends_line(&last_line)) {
            gtk_text_iter_forward_to_line_end(&last_line);
        }
        gtk_source_buffer_ensure_highlight(GTK_SOURCE_BUFFER(buffer), &first_line, &last_line);
    }
#endif
    gint lines = gtk_text_buffer_get_line_count(buffer);
    for (gint i = first; i < lines; i++) {
        GtkTextIter ls;
        gtk_text_buffer_get_iter_at_line(buffer, &ls, i);
        gint ly = 0, lh = 0;
        gtk_text_view_get_line_yrange(view, &ls, &ly, &lh);
        if (ly * sy > range_end) {
            break;
        }
        if ((ly + lh) * sy < c->top) {
            continue;
        }

        GdkRGBA fg, bg;
        gboolean has_fg, has_bg;
        tag_colors(&ls, &fg, &has_fg, &bg, &has_bg);
        if (has_bg) {
            cairo_set_source_rgba(cc, bg.red, bg.green, bg.blue, bg.alpha);
            cairo_rectangle(cc, 0, ly * sy, width, MAX(1.0, lh * sy));
            cairo_fill(cc);
        }

        GtkTextIter lend = ls;
        if (!gtk_text_iter_ends_line(&lend)) {
            gtk_text_iter_forward_to_line_end(&lend);
        }
        if (!detailed) {
            gint len = gtk_text_iter_get_line_offset(&lend);
            if (len > 0) {
                set_source_role(cc, PAL_FG, 0.6);
                cairo_rectangle(cc, 2.0, (gint)(ly * sy + 0.5), MIN(len * 7.0 * sx, width - 2.0),
                                MAX(1, (gint)(lh * sy * 0.5)));
                cairo_fill(cc);
            }
            continue;
        }

        /* Each display (wrapped) line, run by run */
        GtkTextIter ds = ls;
        while (TRUE) {
            GtkTextIter de = ds;
            gtk_text_view_forward_display_line_end(view, &de);
            if (gtk_text_iter_compare(&de, &lend) > 0 || gtk_text_iter_get_line(&de) != i) {
                de = lend;
            }
            GtkTextIter rs = ds;
            while (gtk_text_iter_compare(&rs, &de) < 0) {
                GtkTextIter re = rs;
                if (gtk_text_iter_get_child_anchor(&rs) != NULL || gtk_text_iter_get_paintable(&rs) != NULL) {
                    gtk_text_iter_forward_char(&re);
                } else {
                    gtk_text_iter_forward_to_tag_toggle(&re, NULL);
                    /* stop before an embedded object so it is drawn as a block */
                    GtkTextIter scan = rs;
                    while (gtk_text_iter_compare(&scan, &re) < 0 && gtk_text_iter_compare(&scan, &de) < 0) {
                        if (gtk_text_iter_get_char(&scan) == 0xFFFC && !gtk_text_iter_equal(&scan, &rs)) {
                            re = scan;
                            break;
                        }
                        gtk_text_iter_forward_char(&scan);
                    }
                }
                if (gtk_text_iter_compare(&re, &de) > 0) {
                    re = de;
                }
                if (gtk_text_iter_equal(&re, &rs)) {
                    break;
                }
                draw_run(cc, layout, view, &rs, &re, sx, sy, font_scale);
                rs = re;
            }
            GtkTextIter next = ds;
            if (!gtk_text_view_forward_display_line(view, &next) || gtk_text_iter_get_line(&next) != i ||
                gtk_text_iter_compare(&next, &ds) <= 0) {
                break;
            }
            ds = next;
        }
    }
    g_object_unref(layout);
    cairo_destroy(cc);
}

/* While the minimap is shown it is the vertical scrollbar (click / drag), as
 * in VS Code: hide GTK's overlay scrollbar, which would be drawn over the
 * end of long lines. Wheel and keyboard scrolling are unchanged. */
static void sync_vertical_scrollbar(GtkWidget* view, gboolean minimap_shown) {
    GtkWidget* sw = (view != NULL) ? gtk_widget_get_ancestor(view, GTK_TYPE_SCROLLED_WINDOW) : NULL;
    if (sw == NULL) {
        return;
    }
    GtkPolicyType h, v;
    gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(sw), &h, &v);
    GtkPolicyType want = minimap_shown ? GTK_POLICY_EXTERNAL : GTK_POLICY_AUTOMATIC;
    if (v != want) {
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), h, want);
    }
}

/* Whether the text has enough lines to fill more than the viewer even with
 * the smallest line height: a short text hides the minimap at once */
static gboolean may_need_bar(GtkWidget* view, gdouble page) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    return buffer != NULL && gtk_text_buffer_get_line_count(buffer) * MINIMAP_MIN_LINE_PX > page;
}

/* What the last visibility check saw, kept on the minimap widget */
typedef struct {
    gint view_w, view_h;
    gdouble upper, page;
    guint waits;
} MinimapVisState;

/* While a text is laid out or its viewer resized, GtkTextView may report an
 * estimated height for a frame: following it makes the minimap flicker. Such a
 * change is confirmed on the next frame: showing after a resize, and hiding
 * while the size or height is still moving for a text long enough to need the
 * minimap. Everything else applies at once, as does any change while the view
 * is off screen (nothing can be seen flickering). */
static gboolean vis_must_wait(GtkWidget* minimap, GtkWidget* view, gboolean useful) {
    MinimapVisState* st = g_object_get_data(G_OBJECT(minimap), "minimap-vis");
    if (st == NULL) {
        st = g_new0(MinimapVisState, 1);
        g_object_set_data_full(G_OBJECT(minimap), "minimap-vis", st, g_free);
    }
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gint w = gtk_widget_get_width(view);
    gint h = gtk_widget_get_height(view);
    gdouble upper = (adj != NULL) ? gtk_adjustment_get_upper(adj) : 0.0;
    gdouble page = (adj != NULL) ? gtk_adjustment_get_page_size(adj) : 0.0;
    gboolean resized = w != st->view_w || h != st->view_h;
    gboolean settled = upper == st->upper && page == st->page;
    st->view_w = w;
    st->view_h = h;
    st->upper = upper;
    st->page = page;

    gboolean shown = gtk_widget_get_visible(minimap);
    gboolean wait = FALSE;
    if (shown != useful && gtk_widget_get_mapped(view)) {
        wait = useful ? resized : ((resized || !settled) && may_need_bar(view, page));
    }
    /* Content that keeps moving (panel drag, loading) gets its answer anyway */
    if (wait && ++st->waits > MINIMAP_MAX_WAITS) {
        wait = FALSE;
    }
    if (!wait) {
        st->waits = 0;
    }
    return wait;
}

/* LOOKIN_DEBUG_MINIMAP=1: print each show / hide (and each wait) with its
 * time, to spot flicker that lasts too little to be seen on a screenshot */
static gboolean debug_minimap(void) {
    static gint enabled = -1;
    if (enabled < 0) {
        enabled = g_getenv("LOOKIN_DEBUG_MINIMAP") != NULL;
    }
    return enabled;
}

static void debug_minimap_log(const gchar* who, const gchar* what, GtkWidget* view) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    g_printerr("minimap %s %s: %.1f ms (view %dx%d, upper %.0f, page %.0f)\n", who, what,
               g_get_monotonic_time() / 1000.0, gtk_widget_get_width(view),
               gtk_widget_get_height(view), adj != NULL ? gtk_adjustment_get_upper(adj) : 0.0,
               adj != NULL ? gtk_adjustment_get_page_size(adj) : 0.0);
}

/* Show or hide the minimap now, or check again next frame by
 * calling `again` (which stores its source in *source); `who` names it in
 * the debug log */
static void vis_update(GtkWidget* minimap, GtkWidget* view, MinimapCache* cache, gboolean useful,
                       guint* source, GSourceFunc again, gpointer data, const gchar* who) {
    if (vis_must_wait(minimap, view, useful)) {
        if (debug_minimap()) {
            debug_minimap_log(who, useful ? "wait-show" : "wait-hide", view);
        }
        *source = g_timeout_add(MINIMAP_SETTLE_MS, again, data);
        return;
    }
    if (gtk_widget_get_visible(minimap) != useful) {
        if (!useful) {
            minimap_cache_clear(cache);
        }
        gtk_widget_set_visible(minimap, useful);
        if (debug_minimap()) {
            debug_minimap_log(who, useful ? "show" : "hide", view);
        }
    }
    sync_vertical_scrollbar(view, useful);
}

/* Git change marks (diff_gutter) as a bar along the left edge, like VS Code:
 * runs of added / modified lines in one rectangle, deletions as a red tick */
static void draw_change_marks(cairo_t* cr, GtkTextView* view, const MinimapGeometry* g, int height) {
    gdouble sy = g->scale;
    gdouble shown_end = g->offset + height;
    guint n = 0;
    const guint8* marks = diff_gutter_get_marks(GTK_WIDGET(view), &n);
    if (marks == NULL || sy <= 0.0) {
        return;
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    n = MIN(n, (guint)gtk_text_buffer_get_line_count(buffer));
    const gdouble x = 1.0;
    const gdouble w = 3.0;
    guint i = 0;
    while (i < n) {
        guint8 kind = marks[i] & (LINE_DIFF_ADDED | LINE_DIFF_MODIFIED);
        GtkTextIter it;
        gint ly = 0, lh = 0;
        if (marks[i] & (LINE_DIFF_DELETED_ABOVE | LINE_DIFF_DELETED_BELOW)) {
            gtk_text_buffer_get_iter_at_line(buffer, &it, (gint)i);
            gtk_text_view_get_line_yrange(view, &it, &ly, &lh);
            gdouble dy = ((marks[i] & LINE_DIFF_DELETED_ABOVE) ? ly : ly + lh) * sy;
            if (dy >= g->offset - 2.0 && dy <= shown_end + 2.0) {
                set_source_role(cr, PAL_GUTTER_DELETED, 1.0);
                cairo_rectangle(cr, x, MIN(dy - 1.0, g->content - 2.0), w, 2.0);
                cairo_fill(cr);
            }
        }
        if (kind == 0) {
            i++;
            continue;
        }
        /* Run of lines with the same mark */
        guint j = i;
        while (j + 1 < n && (marks[j + 1] & (LINE_DIFF_ADDED | LINE_DIFF_MODIFIED)) == kind &&
               !(marks[j + 1] & (LINE_DIFF_DELETED_ABOVE | LINE_DIFF_DELETED_BELOW))) {
            j++;
        }
        gtk_text_buffer_get_iter_at_line(buffer, &it, (gint)i);
        gtk_text_view_get_line_yrange(view, &it, &ly, &lh);
        gdouble y0 = ly * sy;
        if (j != i) {
            gtk_text_buffer_get_iter_at_line(buffer, &it, (gint)j);
            gtk_text_view_get_line_yrange(view, &it, &ly, &lh);
        }
        gdouble y1 = (ly + lh) * sy;
        i = j + 1;
        if (y1 < g->offset || y0 > shown_end) {
            continue;
        }
        if (kind & LINE_DIFF_ADDED) {
            set_source_role(cr, PAL_GUTTER_ADDED, 1.0);
        } else {
            set_source_role(cr, PAL_GUTTER_MODIFIED, 1.0);
        }
        cairo_rectangle(cr, x, y0, w, MAX(2.0, y1 - y0));
        cairo_fill(cr);
    }
}

static gboolean redraw_later(gpointer data) {
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}

void minimap_draw_content(GtkWidget* widget, cairo_t* cr, int width, int height,
                          GtkTextBuffer* buffer, GtkTextView* text_view, MinimapCache* cache) {
    if (width <= 0 || height <= 0 || buffer == NULL || text_view == NULL) {
        return;
    }
    MinimapGeometry g;
    if (minimap_geometry(widget, text_view, &g)) {
        /* Rebuilt on resize, rescale, or when scrolled out of the render.
         * While GtkTextView is still laying out changed text, the previous
         * render stands in (at most MINIMAP_LAYOUT_WAIT_US), checked again
         * next frame. */
        if (!cache_covers(cache, &g, width, height)) {
            gboolean wait = FALSE;
            if (cache->surface != NULL && cache->stale &&
                !layout_complete(buffer, text_view, &g, height)) {
                gint64 now = g_get_monotonic_time();
                if (cache->incomplete_since == 0) {
                    cache->incomplete_since = now;
                }
                wait = now - cache->incomplete_since < MINIMAP_LAYOUT_WAIT_US;
            }
            if (wait) {
                g_timeout_add_full(G_PRIORITY_DEFAULT, MINIMAP_SETTLE_MS, redraw_later,
                                   g_object_ref(widget), g_object_unref);
            } else {
                minimap_build_cache(buffer, text_view, cache, width, height, &g);
            }
        }
        if (cache->surface != NULL) {
            /* Whole pixels: no blurry resampling */
            gdouble dy = cache->top - g.offset;
            gint y = (dy >= 0.0) ? (gint)(dy + 0.5) : -(gint)(-dy + 0.5);
            cairo_set_source_surface(cr, cache->surface, 0, y);
            cairo_paint(cr);
        }

        /* Slider: the view's visible range */
        set_source_role(cr, PAL_SCROLLBAR, 0.35);
        cairo_rectangle(cr, 0, g.slider_top, width, g.slider_height);
        cairo_fill(cr);

        cairo_save(cr);
        cairo_translate(cr, 0, -g.offset);
        draw_change_marks(cr, text_view, &g, height);
        cairo_restore(cr);
    }

    /* Thin border on the viewer/editor side */
    set_source_role(cr, PAL_BORDER, 1.0);
    cairo_rectangle(cr, 0, 0, 1, height);
    cairo_fill(cr);
}

static void set_scroll(GtkWidget* minimap, GtkTextView* text_view, gdouble value) {
    /* Keep horizontal scroll locked to origin so left padding is never scrolled away */
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(text_view));
    if (hadj != NULL) {
        gdouble min_h = gtk_adjustment_get_lower(hadj);
        if (gtk_adjustment_get_value(hadj) != min_h) {
            gtk_adjustment_set_value(hadj, min_h);
        }
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text_view));
    gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj) + value);
    gtk_widget_queue_draw(minimap);
}

/* Button press at y, as in VS Code: on the slider, start dragging it; elsewhere,
 * center the view on the line under the pointer, then drag from there.
 * *grab is the pointer's distance to the slider top, kept while dragging. */
void minimap_press_view(GtkWidget* minimap, GtkTextView* text_view, gdouble y, gdouble* grab) {
    MinimapGeometry g;
    if (minimap == NULL || text_view == NULL || !minimap_geometry(minimap, text_view, &g)) {
        return;
    }
    if (y < g.slider_top || y > g.slider_top + g.slider_height) {
        GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text_view));
        gdouble lower = gtk_adjustment_get_lower(vadj);
        set_scroll(minimap, text_view,
                   minimap_geometry_value_centered(&g, gtk_adjustment_get_upper(vadj) - lower,
                                                   gtk_adjustment_get_page_size(vadj), y));
        minimap_geometry(minimap, text_view, &g);
    }
    *grab = y - g.slider_top;
}

/* Pointer moved to y while dragging: move the slider with it */
void minimap_drag_view(GtkWidget* minimap, GtkTextView* text_view, gdouble y, gdouble grab) {
    MinimapGeometry g;
    if (minimap == NULL || text_view == NULL || !minimap_geometry(minimap, text_view, &g)) {
        return;
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text_view));
    gdouble lower = gtk_adjustment_get_lower(vadj);
    set_scroll(minimap, text_view,
               minimap_geometry_value_for_slider(&g, gtk_adjustment_get_upper(vadj) - lower,
                                                 gtk_adjustment_get_page_size(vadj),
                                                 gtk_widget_get_height(minimap), y - grab));
}

/* Mouse wheel over the minimap scrolls the text view: one notch moves a
 * tenth of a page (at least 3 lines' worth), touchpads by pixels */
static gboolean on_minimap_wheel(GtkEventControllerScroll* ctrl,
                                 gdouble dx, gdouble dy, gpointer data) {
    (void)dx;
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(data));
    if (vadj == NULL || dy == 0.0) {
        return FALSE;
    }
    gdouble page = gtk_adjustment_get_page_size(vadj);
    gdouble step = dy;
    if (gtk_event_controller_scroll_get_unit(ctrl) == GDK_SCROLL_UNIT_WHEEL) {
        step = dy * MAX(page / 10.0, 48.0);
    }
    gdouble min_v = gtk_adjustment_get_lower(vadj);
    gdouble max_v = MAX(min_v, gtk_adjustment_get_upper(vadj) - page);
    gtk_adjustment_set_value(vadj, CLAMP(gtk_adjustment_get_value(vadj) + step, min_v, max_v));
    return TRUE;
}

void minimap_attach_wheel(GtkWidget* minimap, GtkWidget* text_view) {
    GtkEventController* scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_minimap_wheel), text_view);
    gtk_widget_add_controller(minimap, scroll);
}

/* =========================================================================
 * Editor Tab Minimap Implementation
 * ========================================================================= */

void editor_minimap_invalidate(EditorTab* tab) {
    if (tab == NULL) {
        return;
    }
    minimap_cache_mark_stale(&tab->mm_cache);
    if (tab->minimap != NULL && gtk_widget_get_visible(tab->minimap)) {
        gtk_widget_queue_draw(tab->minimap);
    }
}

static gboolean editor_minimap_is_useful(EditorTab* tab) {
    if (tab == NULL || tab->minimap == NULL || tab->view == NULL) {
        return FALSE;
    }
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tab->view));
    if (adj == NULL) {
        return FALSE;
    }
    return gtk_adjustment_get_upper(adj) >
           gtk_adjustment_get_page_size(adj) + 1.5;
}

static gboolean editor_minimap_vis_idle(gpointer data) {
    EditorTab* tab = (EditorTab*)data;
    tab->mm_vis_source = 0;
    if (tab->minimap == NULL || tab->view == NULL) {
        return G_SOURCE_REMOVE;
    }
    vis_update(tab->minimap, tab->view, &tab->mm_cache, editor_minimap_is_useful(tab),
               &tab->mm_vis_source, editor_minimap_vis_idle, tab, "editor");
    return G_SOURCE_REMOVE;
}

void editor_minimap_update_visibility(EditorTab* tab) {
    if (tab == NULL || tab->minimap == NULL || tab->view == NULL) {
        return;
    }
    if (tab->mm_vis_source == 0) {
        tab->mm_vis_source = g_idle_add(editor_minimap_vis_idle, tab);
    }
}

static void on_editor_minimap_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    EditorTab* tab = (EditorTab*)data;
    if (tab == NULL || tab->buffer == NULL || tab->view == NULL) {
        return;
    }
    minimap_draw_content(GTK_WIDGET(area), cr, width, height,
                         tab->buffer, GTK_TEXT_VIEW(tab->view), &tab->mm_cache);
}

static void on_editor_minimap_pressed(GtkGestureClick* gesture, gint n_press,
                                      gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->buffer != NULL && tab->view != NULL) {
        tab->mm_dragging = TRUE;
        minimap_press_view(tab->minimap, GTK_TEXT_VIEW(tab->view), y, &tab->mm_grab);
    }
}

static void on_editor_minimap_motion(GtkEventControllerMotion* ctrl,
                                     gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    (void)x;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->mm_dragging && tab->buffer != NULL && tab->view != NULL) {
        minimap_drag_view(tab->minimap, GTK_TEXT_VIEW(tab->view), y, tab->mm_grab);
    }
}

static void on_editor_minimap_released(GtkGestureClick* gesture, gint n_press,
                                       gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        tab->mm_dragging = FALSE;
    }
}

static void on_editor_minimap_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_invalidate(tab);
        editor_minimap_update_visibility(tab);
    }
}

static void on_editor_minimap_scroll(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->minimap != NULL && gtk_widget_get_visible(tab->minimap)) {
        gtk_widget_queue_draw(tab->minimap);
    }
}

static void on_editor_minimap_adj_changed(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_invalidate(tab); /* content height changed: redraw */
        editor_minimap_update_visibility(tab);
    }
}

static void on_editor_minimap_resize(GtkDrawingArea* area, gint width, gint height, gpointer data) {
    (void)area;
    (void)width;
    (void)height;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_invalidate(tab);
    }
}

static void on_editor_minimap_realize(GtkWidget* widget, gpointer data) {
    (void)widget;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_update_visibility(tab);
    }
}

void editor_minimap_init(EditorTab* tab) {
    if (tab == NULL || tab->view == NULL || tab->buffer == NULL) {
        return;
    }

    GtkWidget* minimap = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(minimap), MINIMAP_WIDTH);
    gtk_widget_set_size_request(minimap, MINIMAP_WIDTH, -1);
    gtk_widget_set_vexpand(minimap, TRUE);
    gtk_widget_set_visible(minimap, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(minimap),
                                   on_editor_minimap_draw, tab, NULL);

    GtkGesture* mm_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mm_click), GDK_BUTTON_PRIMARY);
    g_signal_connect(mm_click, "pressed", G_CALLBACK(on_editor_minimap_pressed), tab);
    g_signal_connect(mm_click, "released", G_CALLBACK(on_editor_minimap_released), tab);
    gtk_widget_add_controller(minimap, GTK_EVENT_CONTROLLER(mm_click));

    GtkEventController* mm_motion = gtk_event_controller_motion_new();
    g_signal_connect(mm_motion, "motion", G_CALLBACK(on_editor_minimap_motion), tab);
    gtk_widget_add_controller(minimap, mm_motion);
    minimap_attach_wheel(minimap, tab->view);

    g_signal_connect(minimap, "resize", G_CALLBACK(on_editor_minimap_resize), tab);
    g_signal_connect(minimap, "realize", G_CALLBACK(on_editor_minimap_realize), tab);
    g_signal_connect(tab->buffer, "changed", G_CALLBACK(on_editor_minimap_buffer_changed), tab);

    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tab->view));
    if (vadj != NULL) {
        g_signal_connect(vadj, "value-changed", G_CALLBACK(on_editor_minimap_scroll), tab);
        g_signal_connect(vadj, "changed", G_CALLBACK(on_editor_minimap_adj_changed), tab);
    }

    diff_gutter_add_companion(tab->view, minimap);

    tab->minimap = minimap;
    tab->mm_cache = (MinimapCache){0};
    tab->mm_dragging = FALSE;
    tab->mm_grab = 0.0;
    tab->mm_vis_source = 0;
}

/* =========================================================================
 * Minimap of a read-only text view (the preview, the Help tab)
 * ========================================================================= */

typedef struct {
    GtkWidget* minimap;
    GtkWidget* view;
    MinimapCache cache;
    gboolean dragging;
    gdouble grab;
    guint vis_source;
    MinimapShownIf shown_if; /* extra condition to show it, or NULL */
    gpointer shown_data;
    const gchar* who;        /* its name in the LOOKIN_DEBUG_MINIMAP log */
} ViewMinimap;

static void view_minimap_free(gpointer data) {
    ViewMinimap* vm = data;
    if (vm->vis_source != 0) {
        g_source_remove(vm->vis_source);
    }
    minimap_cache_clear(&vm->cache);
    g_free(vm);
}

static void view_minimap_invalidate(ViewMinimap* vm) {
    minimap_cache_mark_stale(&vm->cache);
    if (gtk_widget_get_visible(vm->minimap)) {
        gtk_widget_queue_draw(vm->minimap);
    }
}

static gboolean view_minimap_vis_idle(gpointer data) {
    ViewMinimap* vm = data;
    vm->vis_source = 0;
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vm->view));
    gboolean useful = adj != NULL &&
                      gtk_adjustment_get_upper(adj) > gtk_adjustment_get_page_size(adj) + 1.5 &&
                      (vm->shown_if == NULL || vm->shown_if(vm->shown_data));
    vis_update(vm->minimap, vm->view, &vm->cache, useful, &vm->vis_source, view_minimap_vis_idle, vm, vm->who);
    return G_SOURCE_REMOVE;
}

static void view_minimap_update_visibility(ViewMinimap* vm) {
    if (vm->vis_source == 0) {
        vm->vis_source = g_idle_add(view_minimap_vis_idle, vm);
    }
}

static void on_view_minimap_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    ViewMinimap* vm = data;
    minimap_draw_content(GTK_WIDGET(area), cr, width, height,
                         gtk_text_view_get_buffer(GTK_TEXT_VIEW(vm->view)),
                         GTK_TEXT_VIEW(vm->view), &vm->cache);
}

static void on_view_minimap_pressed(GtkGestureClick* gesture, gint n_press,
                                    gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    ViewMinimap* vm = data;
    vm->dragging = TRUE;
    minimap_press_view(vm->minimap, GTK_TEXT_VIEW(vm->view), y, &vm->grab);
}

static void on_view_minimap_motion(GtkEventControllerMotion* ctrl,
                                   gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    (void)x;
    ViewMinimap* vm = data;
    if (vm->dragging) {
        minimap_drag_view(vm->minimap, GTK_TEXT_VIEW(vm->view), y, vm->grab);
    }
}

static void on_view_minimap_released(GtkGestureClick* gesture, gint n_press,
                                     gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    ((ViewMinimap*)data)->dragging = FALSE;
}

/* Text, layout or tag colours (theme) changed: redraw from scratch */
static void on_view_minimap_stale(gpointer data) {
    ViewMinimap* vm = g_object_get_data(G_OBJECT(data), "view-minimap");
    if (vm != NULL) {
        view_minimap_invalidate(vm);
        view_minimap_update_visibility(vm);
    }
}

static void on_view_minimap_scroll(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    if (gtk_widget_get_visible(GTK_WIDGET(data))) {
        gtk_widget_queue_draw(GTK_WIDGET(data));
    }
}

static void on_view_minimap_realize(GtkWidget* widget, gpointer data) {
    (void)widget;
    view_minimap_update_visibility(data);
}

GtkWidget* minimap_new_for_view(GtkWidget* text_view) {
    ViewMinimap* vm = g_new0(ViewMinimap, 1);
    vm->view = text_view;
    vm->who = "help";
    GtkWidget* minimap = gtk_drawing_area_new();
    vm->minimap = minimap;
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(minimap), MINIMAP_WIDTH);
    gtk_widget_set_size_request(minimap, MINIMAP_WIDTH, -1);
    gtk_widget_set_vexpand(minimap, TRUE);
    gtk_widget_set_visible(minimap, FALSE);
    g_object_set_data_full(G_OBJECT(minimap), "view-minimap", vm, view_minimap_free);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(minimap), on_view_minimap_draw, vm, NULL);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_view_minimap_pressed), vm);
    g_signal_connect(click, "released", G_CALLBACK(on_view_minimap_released), vm);
    gtk_widget_add_controller(minimap, GTK_EVENT_CONTROLLER(click));
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_view_minimap_motion), vm);
    gtk_widget_add_controller(minimap, motion);
    minimap_attach_wheel(minimap, text_view);
    g_signal_connect(minimap, "realize", G_CALLBACK(on_view_minimap_realize), vm);
    g_signal_connect_swapped(minimap, "resize", G_CALLBACK(on_view_minimap_stale), minimap);

    /* Signals of longer-lived objects: disconnected with the minimap */
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    g_signal_connect_object(buffer, "changed", G_CALLBACK(on_view_minimap_stale), minimap, G_CONNECT_SWAPPED);
    g_signal_connect_object(gtk_text_buffer_get_tag_table(buffer), "tag-changed",
                            G_CALLBACK(on_view_minimap_stale), minimap, G_CONNECT_SWAPPED);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text_view));
    if (vadj != NULL) {
        g_signal_connect_object(vadj, "value-changed", G_CALLBACK(on_view_minimap_scroll), minimap, 0);
        g_signal_connect_object(vadj, "changed", G_CALLBACK(on_view_minimap_stale), minimap, G_CONNECT_SWAPPED);
    }
    return minimap;
}

static ViewMinimap* view_minimap_of(GtkWidget* minimap) {
    return minimap != NULL ? g_object_get_data(G_OBJECT(minimap), "view-minimap") : NULL;
}

void minimap_view_set_shown_if(GtkWidget* minimap, const gchar* who, MinimapShownIf shown_if, gpointer data) {
    ViewMinimap* vm = view_minimap_of(minimap);
    if (vm != NULL) {
        vm->who = who;
        vm->shown_if = shown_if;
        vm->shown_data = data;
    }
}

void minimap_view_invalidate(GtkWidget* minimap) {
    ViewMinimap* vm = view_minimap_of(minimap);
    if (vm != NULL) {
        view_minimap_invalidate(vm);
    }
}

void minimap_view_update_visibility(GtkWidget* minimap) {
    ViewMinimap* vm = view_minimap_of(minimap);
    if (vm != NULL) {
        view_minimap_update_visibility(vm);
    }
}

void minimap_view_cache_clear(GtkWidget* minimap) {
    ViewMinimap* vm = view_minimap_of(minimap);
    if (vm != NULL) {
        minimap_cache_clear(&vm->cache);
    }
}

/* =========================================================================
 * The preview's minimap (app->viewer_minimap)
 * ========================================================================= */

/* Only on the Preview page: Git, References or an editor tab may share the
 * notebook, even while editors live in the dedicated panel */
static gboolean preview_page_shown(gpointer data) {
    AppData* app = data;
    return app->notebook == NULL || gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook)) == 0;
}

GtkWidget* minimap_viewer_new(AppData* app) {
    GtkWidget* minimap = minimap_new_for_view(app->text_view);
    minimap_view_set_shown_if(minimap, "preview", preview_page_shown, app);
    diff_gutter_add_companion(app->text_view, minimap);
    app->viewer_minimap = minimap;
    return minimap;
}

gboolean minimap_viewer_is_hidden(AppData* app) {
    return app->viewer_minimap != NULL && !gtk_widget_get_visible(app->viewer_minimap);
}

void minimap_viewer_cache_clear(AppData* app) {
    minimap_view_cache_clear(app->viewer_minimap);
}

void minimap_invalidate(AppData* app) {
    minimap_view_invalidate(app->viewer_minimap);
}

/* Hide the minimap when the whole file is visible in the viewer */
void minimap_update_visibility(AppData* app) {
    minimap_view_update_visibility(app->viewer_minimap);
}
