/* Markdown tables (see markdown_table.h) */
#include "markup/markdown_table.h"
#include "markup/markdown_link.h"
#include "theme/fonts.h"
#include "theme/palette.h"

#include <math.h>
#include <string.h>

static gchar* clean_table_cell_text(const gchar* text) {
    GString* clean = g_string_new(NULL);
    const gchar* p = text;
    while (*p != '\0') {
        if (*p == '`' || *p == '*' || *p == '_' || *p == '~') {
            p++;
            continue;
        }
        if (*p == '[' && strchr(p, ']')) {
            const gchar* end = strchr(p, ']');
            p++;
            while (p < end) {
                g_string_append_c(clean, *p);
                p++;
            }
            if (*p == ']' && p[1] == '(') {
                const gchar* uend = strchr(p + 2, ')');
                if (uend) {
                    p = uend + 1;
                    continue;
                }
            }
            continue;
        }
        g_string_append_c(clean, *p);
        p++;
    }
    return g_string_free(clean, FALSE);
}

GPtrArray* markdown_table_split_row(const gchar* row_str) {
    GPtrArray* cells = g_ptr_array_new_with_free_func(g_free);
    const gchar* p = row_str;
    while (*p == ' ') p++;
    if (*p == '|') p++; /* skip leading border pipe */

    GString* cur = g_string_new(NULL);
    while (*p != '\0') {
        if (*p == '\\' && p[1] == '|') {
            g_string_append_c(cur, '|');
            p += 2;
            continue;
        }
        if (*p == '|') {
            const gchar* rest = p + 1;
            while (*rest == ' ') rest++;
            if (*rest == '\0') {
                break;
            }
            g_ptr_array_add(cells, g_strstrip(g_string_free(cur, FALSE)));
            cur = g_string_new(NULL);
            p++;
            continue;
        }
        g_string_append_c(cur, *p);
        p++;
    }
    if (cur->len > 0) {
        g_ptr_array_add(cells, g_strstrip(g_string_free(cur, FALSE)));
    } else {
        g_string_free(cur, TRUE);
    }
    return cells;
}

/* Pango markup of a table cell's inline Markdown (code, bold, italic,
 * strikethrough, links), escaped */
static void cell_markup_append(GString* out, const gchar* text, gsize len, gboolean links) {
    gsize i = 0;
    while (i < len) {
        const gchar* p = text + i;
        const gchar* rest_end = text + len;
        if (*p == '`') {
            const gchar* e = memchr(p + 1, '`', rest_end - p - 1);
            if (e != NULL) {
                gchar* esc = g_markup_escape_text(p + 1, e - p - 1);
                gchar* span = g_markup_printf_escaped("<span font_family=\"%s\" size=\"%d%%\" foreground=\"%s\" "
                                                      "background=\"%s\">",
                                                      fonts_family(FONT_CODE), (gint)(FONT_SCALE_CODE * 100),
                                                      palette_color(PAL_SYN_ESCAPE), palette_color(PAL_BG_CODE_INLINE));
                g_string_append_printf(out, "%s%s</span>", span, esc);
                g_free(span);
                g_free(esc);
                i = (gsize)(e - text) + 1;
                continue;
            }
        }
        if ((p[0] == '*' || p[0] == '_' || p[0] == '~') && i + 1 < len && p[1] == p[0]) {
            gchar marker[3] = { p[0], p[0], 0 };
            const gchar* e = g_strstr_len(p + 2, rest_end - p - 2, marker);
            if (e != NULL) {
                const gchar* tag = (p[0] == '~') ? "s" : "b";
                g_string_append_printf(out, "<%s>", tag);
                cell_markup_append(out, p + 2, e - p - 2, links);
                g_string_append_printf(out, "</%s>", tag);
                i = (gsize)(e - text) + 2;
                continue;
            }
        }
        if (p[0] == '*' || p[0] == '_') {
            const gchar* e = memchr(p + 1, p[0], rest_end - p - 1);
            if (e != NULL && e > p + 1) {
                g_string_append(out, "<i>");
                cell_markup_append(out, p + 1, e - p - 1, links);
                g_string_append(out, "</i>");
                i = (gsize)(e - text) + 1;
                continue;
            }
        }
        if (p[0] == '[') {
            const gchar* close = memchr(p, ']', rest_end - p);
            if (close != NULL && close + 1 < rest_end && close[1] == '(') {
                const gchar* paren = memchr(close, ')', rest_end - close);
                if (paren != NULL) {
                    if (links) { /* followed: GtkLabel link (markdown_link_connect_label()) */
                        gchar* url = g_strndup(close + 2, paren - close - 2);
                        gchar* a = g_markup_printf_escaped("<a href=\"%s\">", url);
                        g_string_append(out, a);
                        g_free(a);
                        g_free(url);
                    }
                    g_string_append_printf(out, "<span foreground=\"%s\">", palette_color(PAL_LINK));
                    cell_markup_append(out, p + 1, close - p - 1, links);
                    g_string_append(out, links ? "</span></a>" : "</span>");
                    i = (gsize)(paren - text) + 1;
                    continue;
                }
            }
        }
        gint n = g_utf8_skip[*(const guchar*)p];
        gchar* esc = g_markup_escape_text(p, n);
        g_string_append(out, esc);
        g_free(esc);
        i += (gsize)n;
    }
}

static gchar* cell_markup(const gchar* text, gboolean links) {
    GString* out = g_string_new(NULL);
    cell_markup_append(out, text, strlen(text), links);
    return g_string_free(out, FALSE);
}

/* Column alignment from the separator row: ":--" left, "--:" right, ":-:" center */
static gfloat cell_align(const gchar* sep_cell) {
    gsize n = strlen(sep_cell);
    gboolean left = (n > 0 && sep_cell[0] == ':');
    gboolean right = (n > 0 && sep_cell[n - 1] == ':');
    return (left && right) ? 0.5f : right ? 1.0f : 0.0f;
}

/* The text view sizes an anchored widget at its minimum: the table's frame
 * asks for the table's width, at most the width of the text, and scrolls
 * the rest horizontally (followed on each resize of the view). The view's
 * scrolled window must scroll horizontally too (not GTK_POLICY_NEVER), or
 * the frame's width would hold the view at its old width. */
static void fit_table_frame(GtkAdjustment* hadj, gpointer data) {
    GtkWidget* frame = data;
    GtkWidget* view = gtk_widget_get_ancestor(frame, GTK_TYPE_TEXT_VIEW);
    gint page = (gint)gtk_adjustment_get_page_size(hadj);
    if (view == NULL || page <= 0) {
        return;
    }
    /* - 1: the text layout keeps a pixel for the cursor after the table */
    gint avail = page - gtk_text_view_get_left_margin(GTK_TEXT_VIEW(view)) -
                 gtk_text_view_get_right_margin(GTK_TEXT_VIEW(view)) - 1;
    GtkWidget* grid = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(frame));
    gint minimum = 0, natural = 0;
    gtk_widget_measure(grid, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, &natural, NULL, NULL);
    gint width = MAX(MIN(natural, avail), 1);
    /* As tall as the table at the width it is shown at (a frame left to
     * itself would be as tall as the table at its smallest width, its rows
     * wrapped most: space under a table shown wider) */
    gint height = 0;
    gtk_widget_measure(grid, GTK_ORIENTATION_VERTICAL, MAX(width, minimum), &height, NULL, NULL, NULL);
    /* A fixed (not overlay) scrollbar takes room under the table, which the
     * frame does not count: add it when the table overflows */
    gboolean overlay = TRUE;
    g_object_get(gtk_widget_get_settings(frame), "gtk-overlay-scrolling", &overlay, NULL);
    if (minimum > width && !(overlay && gtk_scrolled_window_get_overlay_scrolling(GTK_SCROLLED_WINDOW(frame)))) {
        gint bar_h = 0;
        gtk_widget_measure(gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(frame)),
                           GTK_ORIENTATION_VERTICAL, -1, NULL, &bar_h, NULL, NULL);
        height += bar_h;
    }
    gint old_w = 0, old_h = 0;
    gtk_widget_get_size_request(frame, &old_w, &old_h);
    if (width != old_w || height != old_h) {
        gtk_widget_set_size_request(frame, width, height);
    }
}

/* Tables of a view waiting for their first fit (GPtrArray of frames) */
#define PENDING_FITS_KEY "lookin-md-table-fits"

static gboolean fit_pending_tables(gpointer data) {
    GtkTextView* view = data;
    GPtrArray* frames = g_object_steal_data(G_OBJECT(view), PENDING_FITS_KEY);
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view));
    for (guint i = 0; frames != NULL && hadj != NULL && i < frames->len; i++) {
        fit_table_frame(hadj, g_ptr_array_index(frames, i));
    }
    if (frames != NULL) {
        g_ptr_array_unref(frames);
    }
    return G_SOURCE_REMOVE;
}

/* Fit the frame before the next frame of the view, with the other tables
 * inserted until then: measuring right after each insertion validates the
 * styles of the view again each time (seconds for a document of hundreds
 * of tables), measuring them together validates them once */
static void fit_table_later(GtkTextView* view, GtkWidget* frame) {
    GPtrArray* frames = g_object_get_data(G_OBJECT(view), PENDING_FITS_KEY);
    if (frames == NULL) {
        frames = g_ptr_array_new_with_free_func(g_object_unref);
        g_object_set_data_full(G_OBJECT(view), PENDING_FITS_KEY, frames,
                               (GDestroyNotify)g_ptr_array_unref);
        /* Before the layout of the next frame (GDK_PRIORITY_REDRAW) */
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, fit_pending_tables, g_object_ref(view), g_object_unref);
    }
    g_ptr_array_add(frames, g_object_ref(frame));
}

/* The wheel over a table scrolls the text around it, as over the rest of
 * the text: the frame (vertical policy EXTERNAL) would take it for itself.
 * Shift+wheel stays with the frame (a wide table scrolls sideways). */
static gboolean on_frame_scroll(GtkEventControllerScroll* ctrl, gdouble dx, gdouble dy, gpointer data) {
    (void)dx;
    (void)data;
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (dy == 0 || (state & GDK_SHIFT_MASK) != 0) {
        return FALSE;
    }
    GtkWidget* frame = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    for (GtkWidget* w = gtk_widget_get_parent(frame); w != NULL; w = gtk_widget_get_parent(w)) {
        /* The first scrolled window around with something to scroll (the
         * preview's may scroll without a scrollbar: EXTERNAL, its minimap) */
        GtkPolicyType vpolicy = GTK_POLICY_NEVER;
        if (GTK_IS_SCROLLED_WINDOW(w)) {
            gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(w), NULL, &vpolicy);
        }
        GtkAdjustment* adj = vpolicy != GTK_POLICY_NEVER ? gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(w)) : NULL;
        if (adj == NULL || gtk_adjustment_get_upper(adj) - gtk_adjustment_get_lower(adj) <= gtk_adjustment_get_page_size(adj)) {
            continue;
        }
        /* As GtkScrolledWindow does: a wheel step scrolls page^(2/3) */
        gdouble step = gtk_event_controller_scroll_get_unit(ctrl) == GDK_SCROLL_UNIT_WHEEL
                           ? pow(gtk_adjustment_get_page_size(adj), 2.0 / 3.0)
                           : 1.0;
        gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + dy * step);
        return TRUE;
    }
    return FALSE;
}

/* Table as a grid widget in the text (VS Code: no vertical borders, a line
 * under the header, faint lines between rows; CSS in theme.c) */
void markdown_table_insert_grid(GtkTextBuffer* buffer, GtkTextView* view, GtkTextIter* iter,
                                GPtrArray* rows, GPtrArray* aligns, guint cols) {
    if (cols == 0) {
        return;
    }
    GtkWidget* grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "md-table");
    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* cells = g_ptr_array_index(rows, r);
        for (guint c = 0; c < cols; c++) {
            const gchar* text = (c < cells->len) ? g_ptr_array_index(cells, c) : "";
            gboolean links = markdown_link_enabled(buffer);
            gchar* markup = cell_markup(text, links);
            GtkWidget* label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(label), markup);
            g_free(markup);
            if (links) {
                markdown_link_connect_label(label, view);
            }
            gfloat align = (c < aligns->len) ? cell_align(g_ptr_array_index(aligns, c)) : 0.0f;
            gtk_label_set_xalign(GTK_LABEL(label), align);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
            /* The text view gives anchored widgets their minimum size: ask for
             * the cell's length (up to 30 chars) so short cells never wrap */
            gchar* visible = clean_table_cell_text(text);
            gtk_label_set_width_chars(GTK_LABEL(label), MIN((gint)g_utf8_strlen(visible, -1) + 1, 30)); /* +1: bold header */
            g_free(visible);
            gtk_label_set_max_width_chars(GTK_LABEL(label), 48);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_widget_set_focusable(label, FALSE);
            gtk_widget_set_valign(label, GTK_ALIGN_FILL);
            gtk_widget_add_css_class(label, r == 0 ? "md-th" : "md-td");
            if (r > 1) {
                gtk_widget_add_css_class(label, "md-td-sep");
            }
            gtk_grid_attach(GTK_GRID(grid), label, (gint)c, (gint)r, 1, 1);
        }
    }
    GtkWidget* frame = gtk_scrolled_window_new();
    gtk_widget_add_css_class(frame, "md-table-frame");
    /* EXTERNAL, not NEVER: the frame's height is the one fit_table_frame()
     * asks for, not the table's tallest */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(frame), GTK_POLICY_AUTOMATIC, GTK_POLICY_EXTERNAL);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(frame), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(frame), grid);
    GtkEventController* wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(wheel, GTK_PHASE_CAPTURE);
    g_signal_connect(wheel, "scroll", G_CALLBACK(on_frame_scroll), NULL);
    gtk_widget_add_controller(frame, wheel);
    GtkTextChildAnchor* anchor = gtk_text_buffer_create_child_anchor(buffer, iter);
    gtk_text_view_add_child_at_anchor(view, frame, anchor);
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view));
    if (hadj != NULL) {
        /* Disconnected when the frame goes (the text is rendered again) */
        g_signal_connect_object(hadj, "changed", G_CALLBACK(fit_table_frame), frame, 0);
        fit_table_later(view, frame);
    }
}

/* Without a view (no widgets): a text table with box-drawing borders */
void markdown_table_append_text(MdOut* out, GPtrArray* rows, guint cols) {
    if (cols == 0) {
        return;
    }
    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* rc = g_ptr_array_index(rows, r);
        for (guint c = 0; c < rc->len; c++) {
            gchar* cleaned = clean_table_cell_text(g_ptr_array_index(rc, c));
            g_free(g_ptr_array_index(rc, c));
            g_ptr_array_index(rc, c) = cleaned;
        }
    }
    gint* col_widths = g_new0(gint, cols);
    for (guint c = 0; c < cols; c++) col_widths[c] = 3;

    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* row_cells = g_ptr_array_index(rows, r);
        for (guint c = 0; c < row_cells->len && c < cols; c++) {
            const gchar* cell_text = g_ptr_array_index(row_cells, c);
            gint clen = (gint)g_utf8_strlen(cell_text, -1);
            if (clen > col_widths[c]) col_widths[c] = clen;
        }
    }

    GString* border_top = g_string_new("┌");
    GString* border_mid = g_string_new("├");
    GString* border_bot = g_string_new("└");
    for (guint c = 0; c < cols; c++) {
        for (gint w = 0; w < col_widths[c] + 2; w++) {
            g_string_append(border_top, "─");
            g_string_append(border_mid, "─");
            g_string_append(border_bot, "─");
        }
        if (c + 1 < cols) {
            g_string_append(border_top, "┬");
            g_string_append(border_mid, "┼");
            g_string_append(border_bot, "┴");
        } else {
            g_string_append(border_top, "┐\n");
            g_string_append(border_mid, "┤\n");
            g_string_append(border_bot, "┘\n");
        }
    }

    md_out_append_tagged(out, border_top->str, "md_table_border");

    for (guint r = 0; r < rows->len; r++) {
        GPtrArray* row_cells = g_ptr_array_index(rows, r);
        GString* row_str = g_string_new("│");
        for (guint c = 0; c < cols; c++) {
            const gchar* cell_text = (c < row_cells->len) ? g_ptr_array_index(row_cells, c) : "";
            gint clen = (gint)g_utf8_strlen(cell_text, -1);
            gint pad = col_widths[c] - clen;
            g_string_append_printf(row_str, " %s", cell_text);
            for (gint p = 0; p < pad; p++) g_string_append_c(row_str, ' ');
            g_string_append(row_str, " │");
        }
        g_string_append_c(row_str, '\n');

        md_out_append_tagged(out, row_str->str, (r == 0) ? "md_table_header" : "md_table");
        g_string_free(row_str, TRUE);

        if (r == 0) {
            md_out_append_tagged(out, border_mid->str, "md_table_border");
        }
    }

    md_out_append_tagged(out, border_bot->str, "md_table_border");

    g_string_free(border_top, TRUE);
    g_string_free(border_mid, TRUE);
    g_string_free(border_bot, TRUE);
    g_free(col_widths);
}
