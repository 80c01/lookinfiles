/* Built-in terminal widget (VTE API subset) drawing a vt_screen fed by a vt_pty. */
#ifdef HAVE_BUILTIN_TERM

#include "term_widget.h"
#include "vt_pty.h"
#include "vt_screen.h"

#include <string.h>

#define TERM_SCROLLBACK 10000
#define WHEEL_LINES 3

struct _VteTerminal {
    GtkWidget parent_instance;
    VtScreen* screen;
    VtPty* pty;
    GPid pid;
    guint child_watch;

    GdkRGBA fg, bg, palette[16], cursor, highlight;
    gboolean has_cursor_color, has_highlight;
    PangoFontDescription* font;
    double cell_w;
    gint cell_h;

    gint scroll;          /* lines scrolled back into the scrollback */
    double wheel;         /* partial lines of smooth scrolling */
    gboolean focused;
    GtkIMContext* im;

    /* Selection from the anchor to the end, rows as vt_screen_line() rows
     * plus vt_screen_scrolled() (they stay on their line as output scrolls) */
    gboolean has_selection;
    gint64 sel_row0, sel_row1;
    gint sel_col0, sel_col1;
    double drag_x, drag_y;  /* where the drag started, in pixels and as a cell */
    gint64 drag_row;
    gint drag_col;
};

enum { SIG_CHILD_EXITED, SIG_TERMPROP_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(VteTerminal, vte_terminal, GTK_TYPE_WIDGET)

/* ------------------------------------------------------------------------- */
/* Colours and metrics                                                       */
/* ------------------------------------------------------------------------- */

static GdkRGBA rgba(double r, double g, double b) {
    GdkRGBA c = {(float)r, (float)g, (float)b, 1.0f};
    return c;
}

static GdkRGBA color_of(const VteTerminal* t, guint32 c, gboolean foreground) {
    switch (VT_COLOR_KIND(c)) {
    case 1: {
        guint idx = c & 0xff;
        if (idx < 16) {
            return t->palette[idx];
        }
        if (idx < 232) {
            idx -= 16;
            guint lv[3] = {idx / 36, (idx / 6) % 6, idx % 6};
            double v[3];
            for (gint i = 0; i < 3; i++) {
                v[i] = lv[i] == 0 ? 0.0 : (lv[i] * 40 + 55) / 255.0;
            }
            return rgba(v[0], v[1], v[2]);
        }
        double gray = (8 + (idx - 232) * 10) / 255.0;
        return rgba(gray, gray, gray);
    }
    case 2:
        return rgba(((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0);
    default:
        return foreground ? t->fg : t->bg;
    }
}

static gboolean is_selected(const VteTerminal* t, gint64 row, gint col) {
    if (!t->has_selection) {
        return FALSE;
    }
    gint64 r0 = t->sel_row0, r1 = t->sel_row1;
    gint c0 = t->sel_col0, c1 = t->sel_col1;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) {
        gint64 tr = r0; r0 = r1; r1 = tr;
        gint tc = c0; c0 = c1; c1 = tc;
    }
    return (row > r0 || (row == r0 && col >= c0)) && (row < r1 || (row == r1 && col < c1));
}

static void cell_colors(const VteTerminal* t, const VtCell* cell, gboolean selected, GdkRGBA* fg, GdkRGBA* bg) {
    *fg = color_of(t, cell->fg, TRUE);
    *bg = color_of(t, cell->bg, FALSE);
    if (cell->attrs & VT_ATTR_INVERSE) {
        GdkRGBA tmp = *fg; *fg = *bg; *bg = tmp;
    }
    if (selected) {
        if (t->has_highlight) {
            *bg = t->highlight;
        } else {
            GdkRGBA tmp = *fg; *fg = *bg; *bg = tmp;
        }
    }
    if (cell->attrs & VT_ATTR_DIM) {
        fg->alpha *= 0.5f;
    }
}

static void update_metrics(VteTerminal* t) {
    PangoLayout* layout = gtk_widget_create_pango_layout(GTK_WIDGET(t), "MMMMMMMMMM");
    if (t->font != NULL) {
        pango_layout_set_font_description(layout, t->font);
    }
    PangoRectangle logical;
    pango_layout_get_extents(layout, NULL, &logical);
    t->cell_w = MAX(1.0, logical.width / 10.0 / PANGO_SCALE);
    t->cell_h = MAX(1, PANGO_PIXELS_CEIL(logical.height));
    g_object_unref(layout);
}

/* ------------------------------------------------------------------------- */
/* Output and input                                                          */
/* ------------------------------------------------------------------------- */

static void send(VteTerminal* t, const gchar* data, gssize len) {
    if (t->pty == NULL) {
        return;
    }
    vt_pty_write(t->pty, data, len < 0 ? strlen(data) : (gsize)len);
    if (t->scroll != 0) {
        t->scroll = 0;
        gtk_widget_queue_draw(GTK_WIDGET(t));
    }
}

static void on_output(const gchar* data, gsize len, gpointer user_data) {
    VteTerminal* t = user_data;
    gint64 before = vt_screen_scrolled(t->screen);
    guint changes = vt_screen_feed(t->screen, data, len);
    gsize reply_len = 0;
    gchar* reply = vt_screen_take_reply(t->screen, &reply_len);
    if (reply != NULL) {
        vt_pty_write(t->pty, reply, reply_len);
        g_free(reply);
    }
    /* A view scrolled back stays on the same lines */
    if (t->scroll > 0) {
        t->scroll = (gint)MIN(t->scroll + (vt_screen_scrolled(t->screen) - before),
                              vt_screen_scrollback(t->screen));
    }
    if (changes & VT_CHANGE_TITLE) {
        g_signal_emit(t, signals[SIG_TERMPROP_CHANGED], g_quark_from_static_string(VTE_TERMPROP_XTERM_TITLE),
                      VTE_TERMPROP_XTERM_TITLE);
    }
    if (changes & VT_CHANGE_BELL) {
        gtk_widget_error_bell(GTK_WIDGET(t));
    }
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

/* Pasted text: line breaks as Enter, no other control characters (they
 * could end a bracketed paste) */
static void paste_text(VteTerminal* t, const gchar* text) {
    if (text == NULL || text[0] == '\0') {
        return;
    }
    gboolean bracketed = (vt_screen_modes(t->screen) & VT_MODE_BRACKETED_PASTE) != 0;
    GString* out = g_string_new(bracketed ? "\033[200~" : "");
    for (const gchar* p = text; *p != '\0'; p++) {
        if (*p == '\r' && p[1] == '\n') {
            continue;
        }
        if (*p == '\n' || *p == '\r') {
            g_string_append_c(out, '\r');
        } else if ((guchar)*p >= 0x20 || *p == '\t') {
            g_string_append_c(out, *p);
        }
    }
    if (bracketed) {
        g_string_append(out, "\033[201~");
    }
    send(t, out->str, (gssize)out->len);
    g_string_free(out, TRUE);
}

static void on_paste_ready(GObject* source, GAsyncResult* res, gpointer data) {
    VteTerminal* t = data;
    gchar* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, NULL);
    paste_text(t, text);
    g_free(text);
    g_object_unref(t);
}

static void paste_from(VteTerminal* t, GdkClipboard* clipboard) {
    gdk_clipboard_read_text_async(clipboard, NULL, on_paste_ready, g_object_ref(t));
}

static void on_im_commit(GtkIMContext* im, const gchar* text, gpointer data) {
    (void)im;
    send(VTE_TERMINAL(data), text, -1);
}

/* Keys without text: xterm sequences (modifiers as ESC [1;<m>X) */
static gboolean special_key(VteTerminal* t, guint keyval, GdkModifierType mods, GString* out) {
    gboolean shift = (mods & GDK_SHIFT_MASK) != 0;
    gboolean ctrl = (mods & GDK_CONTROL_MASK) != 0;
    gboolean alt = (mods & GDK_ALT_MASK) != 0;
    gint m = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
    gboolean app_cursor = (vt_screen_modes(t->screen) & VT_MODE_APP_CURSOR) != 0;
    gchar letter = 0;
    gboolean function = FALSE;
    gint tilde = 0;
    switch (keyval) {
    case GDK_KEY_Up: case GDK_KEY_KP_Up: letter = 'A'; break;
    case GDK_KEY_Down: case GDK_KEY_KP_Down: letter = 'B'; break;
    case GDK_KEY_Right: case GDK_KEY_KP_Right: letter = 'C'; break;
    case GDK_KEY_Left: case GDK_KEY_KP_Left: letter = 'D'; break;
    case GDK_KEY_Home: case GDK_KEY_KP_Home: letter = 'H'; break;
    case GDK_KEY_End: case GDK_KEY_KP_End: letter = 'F'; break;
    case GDK_KEY_F1: letter = 'P'; function = TRUE; break;
    case GDK_KEY_F2: letter = 'Q'; function = TRUE; break;
    case GDK_KEY_F3: letter = 'R'; function = TRUE; break;
    case GDK_KEY_F4: letter = 'S'; function = TRUE; break;
    case GDK_KEY_Insert: case GDK_KEY_KP_Insert: tilde = 2; break;
    case GDK_KEY_Delete: case GDK_KEY_KP_Delete: tilde = 3; break;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: tilde = 5; break;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: tilde = 6; break;
    case GDK_KEY_F5: tilde = 15; break;
    case GDK_KEY_F6: tilde = 17; break;
    case GDK_KEY_F7: tilde = 18; break;
    case GDK_KEY_F8: tilde = 19; break;
    case GDK_KEY_F9: tilde = 20; break;
    case GDK_KEY_F10: tilde = 21; break;
    case GDK_KEY_F11: tilde = 23; break;
    case GDK_KEY_F12: tilde = 24; break;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter:
        g_string_append(out, alt ? "\033\r" : "\r");
        return TRUE;
    case GDK_KEY_BackSpace:
        g_string_append(out, alt ? "\033" : "");
        g_string_append_c(out, ctrl ? '\b' : '\177');
        return TRUE;
    case GDK_KEY_Tab: case GDK_KEY_KP_Tab:
        g_string_append(out, shift ? "\033[Z" : "\t");
        return TRUE;
    case GDK_KEY_ISO_Left_Tab:
        g_string_append(out, "\033[Z");
        return TRUE;
    case GDK_KEY_Escape:
        g_string_append(out, "\033");
        return TRUE;
    default:
        return FALSE;
    }
    if (letter != 0 && m > 1) {
        g_string_append_printf(out, "\033[1;%d%c", m, letter);
    } else if (letter != 0) {
        g_string_append_printf(out, (function || app_cursor) ? "\033O%c" : "\033[%c", letter);
    } else if (m > 1) {
        g_string_append_printf(out, "\033[%d;%d~", tilde, m);
    } else {
        g_string_append_printf(out, "\033[%d~", tilde);
    }
    return TRUE;
}

/* Ctrl+key as a control character, -1 if none */
static gint control_char(guint keyval) {
    guint k = gdk_keyval_to_lower(keyval);
    if (k >= 'a' && k <= 'z') {
        return (gint)(k - 'a' + 1);
    }
    switch (k) {
    case '@': case ' ': case '2': return 0;
    case '[': case '3': return 27;
    case '\\': case '4': return 28;
    case ']': case '5': return 29;
    case '^': case '6': return 30;
    case '_': case '-': case '7': case '/': return 31;
    case '8': return 127;
    default: return -1;
    }
}

static void scroll_view(VteTerminal* t, gint lines) {
    gint scroll = CLAMP(t->scroll + lines, 0, vt_screen_scrollback(t->screen));
    if (scroll != t->scroll) {
        t->scroll = scroll;
        gtk_widget_queue_draw(GTK_WIDGET(t));
    }
}

static gboolean on_key_pressed(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                               GdkModifierType state, gpointer data) {
    (void)keycode;
    VteTerminal* t = data;
    GdkModifierType mods = state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK);
    if (mods == GDK_SHIFT_MASK && (keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_Page_Down)) {
        gint page = MAX(1, vt_screen_rows(t->screen) - 1);
        scroll_view(t, keyval == GDK_KEY_Page_Up ? page : -page);
        return TRUE;
    }
    if (t->pty == NULL) {
        return FALSE;
    }
    GString* out = g_string_new(NULL);
    gboolean handled = special_key(t, keyval, mods, out);
    gboolean control = (mods & GDK_CONTROL_MASK) != 0;
    gboolean alt = (mods & GDK_ALT_MASK) != 0;
    if (!handled && control && !alt) {
        gint c = control_char(keyval);
        if (c >= 0) {
            g_string_append_c(out, (gchar)c);
            handled = TRUE;
        }
    }
    if (!handled && !alt && !control) {
        GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(ctrl));
        if (event != NULL && gtk_im_context_filter_keypress(t->im, event)) {
            g_string_free(out, TRUE);
            return TRUE; /* the text comes with "commit" */
        }
    }
    if (!handled) {
        gunichar uc = gdk_keyval_to_unicode(keyval);
        if (uc != 0 && !g_unichar_iscntrl(uc)) {
            /* Alt+key: ESC then the key; Ctrl+Alt is AltGr on Windows */
            if (alt && !control) {
                g_string_append_c(out, '\033');
            }
            g_string_append_unichar(out, uc);
            handled = TRUE;
        }
    }
    if (handled) {
        send(t, out->str, (gssize)out->len);
    }
    g_string_free(out, TRUE);
    return handled;
}

static void on_key_released(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                            GdkModifierType state, gpointer data) {
    (void)keyval;
    (void)keycode;
    (void)state;
    VteTerminal* t = data;
    GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(ctrl));
    if (event != NULL) {
        gtk_im_context_filter_keypress(t->im, event);
    }
}

static void on_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    VteTerminal* t = data;
    t->focused = TRUE;
    gtk_im_context_focus_in(t->im);
    if (vt_screen_modes(t->screen) & VT_MODE_FOCUS_EVENTS) {
        vt_pty_write(t->pty, "\033[I", 3);
    }
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

static void on_focus_leave(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    VteTerminal* t = data;
    t->focused = FALSE;
    gtk_im_context_focus_out(t->im);
    if (vt_screen_modes(t->screen) & VT_MODE_FOCUS_EVENTS) {
        vt_pty_write(t->pty, "\033[O", 3);
    }
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

/* ------------------------------------------------------------------------- */
/* Mouse: selection, wheel, middle-click paste                               */
/* ------------------------------------------------------------------------- */

/* Cell boundary nearest to (x, y): row as stored in the selection */
static void point_to_cell(VteTerminal* t, double x, double y, gint64* row, gint* col) {
    gint view_row = (gint)(y / t->cell_h);
    if (y < 0) {
        view_row = -1;
    }
    view_row = CLAMP(view_row, -1, vt_screen_rows(t->screen));
    *row = view_row - t->scroll + vt_screen_scrolled(t->screen);
    *col = CLAMP((gint)(x / t->cell_w + 0.5), 0, vt_screen_cols(t->screen));
}

static gchar* selection_text(VteTerminal* t) {
    if (!t->has_selection) {
        return NULL;
    }
    gint64 r0 = t->sel_row0, r1 = t->sel_row1;
    gint c0 = t->sel_col0, c1 = t->sel_col1;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) {
        gint64 tr = r0; r0 = r1; r1 = tr;
        gint tc = c0; c0 = c1; c1 = tc;
    }
    gint64 base = vt_screen_scrolled(t->screen);
    return vt_screen_text(t->screen, (gint)(r0 - base), c0, (gint)(r1 - base), c1);
}

static void selection_to_primary(VteTerminal* t) {
    gchar* text = selection_text(t);
    if (text != NULL && text[0] != '\0') {
        gdk_clipboard_set_text(gtk_widget_get_primary_clipboard(GTK_WIDGET(t)), text);
    }
    g_free(text);
}

/* The selection changes only once the pointer moves to another cell (a
 * double click keeps its word) */
static void on_drag_begin(GtkGestureDrag* drag, double x, double y, gpointer data) {
    (void)drag;
    VteTerminal* t = data;
    gtk_widget_grab_focus(GTK_WIDGET(t));
    t->drag_x = x;
    t->drag_y = y;
    point_to_cell(t, x, y, &t->drag_row, &t->drag_col);
}

static void on_drag_update(GtkGestureDrag* drag, double dx, double dy, gpointer data) {
    (void)drag;
    VteTerminal* t = data;
    gint64 row;
    gint col;
    point_to_cell(t, t->drag_x + dx, t->drag_y + dy, &row, &col);
    gboolean moved = row != t->drag_row || col != t->drag_col;
    if (!moved || (t->has_selection && row == t->sel_row1 && col == t->sel_col1 &&
                   t->sel_row0 == t->drag_row && t->sel_col0 == t->drag_col)) {
        return;
    }
    t->sel_row0 = t->drag_row;
    t->sel_col0 = t->drag_col;
    t->sel_row1 = row;
    t->sel_col1 = col;
    t->has_selection = TRUE;
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

static void on_drag_end(GtkGestureDrag* drag, double dx, double dy, gpointer data) {
    (void)drag;
    (void)dx;
    (void)dy;
    selection_to_primary(VTE_TERMINAL(data));
}

static gboolean word_char(gunichar c) {
    return c != 0 && (g_unichar_isalnum(c) || strchr("-_./~:@%+#=?&", (gint)c) != NULL);
}

/* Double click: the word under the pointer; triple click: the line */
static void on_click(GtkGestureClick* click, gint n_press, double x, double y, gpointer data) {
    (void)click;
    VteTerminal* t = data;
    if (n_press < 2) {
        if (t->has_selection) {
            t->has_selection = FALSE; /* a click drops the selection */
            gtk_widget_queue_draw(GTK_WIDGET(t));
        }
        return;
    }
    gint view_row = CLAMP((gint)(y / t->cell_h), 0, vt_screen_rows(t->screen) - 1);
    gint line = view_row - t->scroll;
    gint len = 0;
    const VtCell* cells = vt_screen_line(t->screen, line, &len);
    if (cells == NULL || len == 0) {
        return;
    }
    gint col = CLAMP((gint)(x / t->cell_w), 0, len - 1);
    gint from = 0, to = len;
    if (n_press == 2) {
        if (!word_char(cells[col].ch)) {
            return;
        }
        from = col;
        to = col + 1;
        while (from > 0 && (word_char(cells[from - 1].ch) || (cells[from].attrs & VT_ATTR_WIDE_TAIL))) {
            from--;
        }
        while (to < len && (word_char(cells[to].ch) || (cells[to].attrs & VT_ATTR_WIDE_TAIL))) {
            to++;
        }
    }
    gint64 row = line + vt_screen_scrolled(t->screen);
    t->sel_row0 = t->sel_row1 = row;
    t->sel_col0 = from;
    t->sel_col1 = to;
    t->has_selection = TRUE;
    selection_to_primary(t);
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

static void on_middle_click(GtkGestureClick* click, gint n_press, double x, double y, gpointer data) {
    (void)click;
    (void)n_press;
    (void)x;
    (void)y;
    VteTerminal* t = data;
    gtk_widget_grab_focus(GTK_WIDGET(t));
    paste_from(t, gtk_widget_get_primary_clipboard(GTK_WIDGET(t)));
}

static gboolean on_scroll(GtkEventControllerScroll* ctrl, double dx, double dy, gpointer data) {
    (void)dx;
    VteTerminal* t = data;
    gboolean pixels = gtk_event_controller_scroll_get_unit(ctrl) == GDK_SCROLL_UNIT_SURFACE;
    t->wheel += pixels ? dy / t->cell_h : dy * WHEEL_LINES;
    gint lines = (gint)t->wheel;
    t->wheel -= lines;
    if (lines == 0) {
        return TRUE;
    }
    if (vt_screen_modes(t->screen) & VT_MODE_ALT_SCREEN) {
        /* Full-screen programs (less, vim): the wheel moves their cursor */
        gboolean app = (vt_screen_modes(t->screen) & VT_MODE_APP_CURSOR) != 0;
        const gchar* key = lines < 0 ? (app ? "\033OA" : "\033[A") : (app ? "\033OB" : "\033[B");
        for (gint i = 0; i < ABS(lines); i++) {
            vt_pty_write(t->pty, key, 3);
        }
        return TRUE;
    }
    scroll_view(t, -lines);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Drawing and size                                                          */
/* ------------------------------------------------------------------------- */

static void draw_text(GtkSnapshot* snapshot, PangoLayout* layout, const gchar* text,
                      gint len, guint16 attrs, const GdkRGBA* color, double x, double y) {
    pango_layout_set_text(layout, text, len);
    PangoAttrList* list = pango_attr_list_new();
    if (attrs & VT_ATTR_BOLD) {
        pango_attr_list_insert(list, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    }
    if (attrs & VT_ATTR_ITALIC) {
        pango_attr_list_insert(list, pango_attr_style_new(PANGO_STYLE_ITALIC));
    }
    if (attrs & VT_ATTR_UNDERLINE) {
        pango_attr_list_insert(list, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
    }
    if (attrs & VT_ATTR_STRIKE) {
        pango_attr_list_insert(list, pango_attr_strikethrough_new(TRUE));
    }
    pango_layout_set_attributes(layout, list);
    pango_attr_list_unref(list);
    gtk_snapshot_save(snapshot);
    gtk_snapshot_translate(snapshot, &GRAPHENE_POINT_INIT((float)x, (float)y));
    gtk_snapshot_append_layout(snapshot, layout, color);
    gtk_snapshot_restore(snapshot);
}

/* Backgrounds of one line that differ from the terminal's */
static void draw_backgrounds(VteTerminal* t, GtkSnapshot* snapshot, const VtCell* cells, gint len,
                             gint64 row, double y) {
    gint start = -1;
    GdkRGBA run_bg = t->bg;
    for (gint c = 0; c <= len; c++) {
        GdkRGBA fg, bg;
        gboolean colored = FALSE;
        if (c < len) {
            cell_colors(t, &cells[c], is_selected(t, row, c), &fg, &bg);
            colored = !gdk_rgba_equal(&bg, &t->bg);
        }
        if (start >= 0 && (!colored || !gdk_rgba_equal(&bg, &run_bg))) {
            graphene_rect_t r = GRAPHENE_RECT_INIT((float)(start * t->cell_w), (float)y,
                                                   (float)((c - start) * t->cell_w), (float)t->cell_h);
            gtk_snapshot_append_color(snapshot, &run_bg, &r);
            start = -1;
        }
        if (colored && start < 0) {
            start = c;
            run_bg = bg;
        }
    }
}

/* Text of one line: runs of ASCII cells of one style, other characters one
 * by one at their cell (fallback fonts may be wider) */
static void draw_line_text(VteTerminal* t, GtkSnapshot* snapshot, PangoLayout* layout, const VtCell* cells,
                           gint len, gint64 row, double y) {
    GString* run = g_string_new(NULL);
    gint run_start = 0, run_end = 0;
    guint16 run_style = 0;
    GdkRGBA run_fg = t->fg;
    for (gint c = 0; c <= len; c++) {
        const VtCell* cell = c < len ? &cells[c] : NULL;
        if (cell != NULL && (cell->attrs & VT_ATTR_WIDE_TAIL)) {
            continue;
        }
        gunichar ch = cell != NULL && cell->ch != 0 ? cell->ch : ' ';
        guint16 style = cell != NULL ? cell->attrs & (VT_ATTR_BOLD | VT_ATTR_ITALIC | VT_ATTR_UNDERLINE | VT_ATTR_STRIKE) : 0;
        GdkRGBA fg = t->fg, bg;
        if (cell != NULL) {
            cell_colors(t, cell, is_selected(t, row, c), &fg, &bg);
        }
        gboolean blank = cell == NULL || (cell->attrs & VT_ATTR_HIDDEN) ||
                         (ch == ' ' && !(style & (VT_ATTR_UNDERLINE | VT_ATTR_STRIKE)));
        gboolean ascii = ch < 0x80;
        if (run->len > 0 && (blank || !ascii || style != run_style || c != run_end ||
                             !gdk_rgba_equal(&fg, &run_fg))) {
            draw_text(snapshot, layout, run->str, (gint)run->len, run_style, &run_fg, run_start * t->cell_w, y);
            g_string_truncate(run, 0);
        }
        if (blank) {
            continue;
        }
        if (!ascii) {
            gchar buf[8];
            gint n = g_unichar_to_utf8(ch, buf);
            draw_text(snapshot, layout, buf, n, style, &fg, c * t->cell_w, y);
            continue;
        }
        if (run->len == 0) {
            run_start = c;
            run_style = style;
            run_fg = fg;
        }
        g_string_append_c(run, (gchar)ch);
        run_end = c + 1;
    }
    g_string_free(run, TRUE);
}

static void draw_cursor(VteTerminal* t, GtkSnapshot* snapshot, PangoLayout* layout) {
    if (vt_screen_modes(t->screen) & VT_MODE_CURSOR_HIDDEN) {
        return;
    }
    gint cy, cx;
    vt_screen_cursor(t->screen, &cy, &cx);
    gint view_row = cy + t->scroll;
    if (view_row >= vt_screen_rows(t->screen)) {
        return;
    }
    gint len = 0;
    const VtCell* cells = vt_screen_line(t->screen, cy, &len);
    if (cells == NULL || cx >= len) {
        return;
    }
    gint width = (cx + 1 < len && (cells[cx + 1].attrs & VT_ATTR_WIDE_TAIL)) ? 2 : 1;
    graphene_rect_t r = GRAPHENE_RECT_INIT((float)(cx * t->cell_w), (float)(view_row * t->cell_h),
                                           (float)(width * t->cell_w), (float)t->cell_h);
    GdkRGBA color = t->has_cursor_color ? t->cursor : t->fg;
    GdkRectangle area = {(gint)r.origin.x, (gint)r.origin.y, (gint)r.size.width, (gint)r.size.height};
    gtk_im_context_set_cursor_location(t->im, &area);
    if (!t->focused) {
        GskRoundedRect rr;
        gsk_rounded_rect_init_from_rect(&rr, &r, 0);
        const float widths[4] = {1, 1, 1, 1};
        const GdkRGBA colors[4] = {color, color, color, color};
        gtk_snapshot_append_border(snapshot, &rr, widths, colors);
        return;
    }
    gtk_snapshot_append_color(snapshot, &color, &r);
    if (cells[cx].ch != 0 && cells[cx].ch != ' ') {
        gchar buf[8];
        gint n = g_unichar_to_utf8(cells[cx].ch, buf);
        draw_text(snapshot, layout, buf, n, cells[cx].attrs, &t->bg, r.origin.x, r.origin.y);
    }
}

static void vte_terminal_snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
    VteTerminal* t = VTE_TERMINAL(widget);
    graphene_rect_t all = GRAPHENE_RECT_INIT(0, 0, (float)gtk_widget_get_width(widget),
                                             (float)gtk_widget_get_height(widget));
    gtk_snapshot_append_color(snapshot, &t->bg, &all);
    PangoLayout* layout = gtk_widget_create_pango_layout(widget, NULL);
    if (t->font != NULL) {
        pango_layout_set_font_description(layout, t->font);
    }
    gint64 scrolled = vt_screen_scrolled(t->screen);
    for (gint r = 0; r < vt_screen_rows(t->screen); r++) {
        gint line = r - t->scroll;
        gint len = 0;
        const VtCell* cells = vt_screen_line(t->screen, line, &len);
        if (cells == NULL) {
            continue;
        }
        double y = (double)r * t->cell_h;
        draw_backgrounds(t, snapshot, cells, len, line + scrolled, y);
        draw_line_text(t, snapshot, layout, cells, len, line + scrolled, y);
    }
    draw_cursor(t, snapshot, layout);
    g_object_unref(layout);
}

static void vte_terminal_measure(GtkWidget* widget, GtkOrientation orientation, int for_size,
                                 int* minimum, int* natural, int* minimum_baseline, int* natural_baseline) {
    (void)for_size;
    VteTerminal* t = VTE_TERMINAL(widget);
    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        *minimum = (int)(t->cell_w * 2 + 0.999);
        *natural = (int)(t->cell_w * 80 + 0.999);
    } else {
        *minimum = t->cell_h;
        *natural = t->cell_h * 24;
    }
    *minimum_baseline = *natural_baseline = -1;
}

static void vte_terminal_size_allocate(GtkWidget* widget, int width, int height, int baseline) {
    (void)baseline;
    VteTerminal* t = VTE_TERMINAL(widget);
    gint cols = MAX(1, (gint)(width / t->cell_w));
    gint rows = MAX(1, height / t->cell_h);
    if (cols != vt_screen_cols(t->screen) || rows != vt_screen_rows(t->screen)) {
        vt_screen_resize(t->screen, cols, rows);
        vt_pty_resize(t->pty, cols, rows);
        t->scroll = MIN(t->scroll, vt_screen_scrollback(t->screen));
    }
}

/* ------------------------------------------------------------------------- */
/* Child process                                                             */
/* ------------------------------------------------------------------------- */

static void on_child_exit(GPid pid, gint status, gpointer data) {
    VteTerminal* t = data;
    t->child_watch = 0;
    t->pid = 0;
    g_spawn_close_pid(pid);
    vt_pty_free(t->pty);
    t->pty = NULL;
    g_object_ref(t);
    g_signal_emit(t, signals[SIG_CHILD_EXITED], 0, status);
    g_object_unref(t);
}

#ifndef G_OS_WIN32
static void reap_child(GPid pid, gint status, gpointer data) {
    (void)status;
    (void)data;
    g_spawn_close_pid(pid);
}
#endif

void vte_terminal_spawn_async(VteTerminal* t, VtePtyFlags pty_flags, const char* working_directory,
                              char** argv, char** envv, GSpawnFlags spawn_flags,
                              GSpawnChildSetupFunc child_setup, gpointer child_setup_data,
                              GDestroyNotify child_setup_data_destroy, int timeout,
                              GCancellable* cancellable, VteTerminalSpawnAsyncCallback callback,
                              gpointer user_data) {
    (void)pty_flags;
    (void)child_setup;
    (void)timeout;
    (void)cancellable;
    if (child_setup_data_destroy != NULL) {
        child_setup_data_destroy(child_setup_data);
    }
    GError* error = NULL;
    GPid pid = 0;
    if (t->pty != NULL) {
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_BUSY, "The terminal already runs a program");
    } else {
        t->pty = vt_pty_spawn(argv, working_directory, envv, (spawn_flags & G_SPAWN_SEARCH_PATH) != 0,
                              vt_screen_cols(t->screen), vt_screen_rows(t->screen), on_output, t, &pid, &error);
    }
    if (t->pty == NULL && error != NULL) {
        gchar* msg = g_strdup_printf("[ %s ]\r\n", error->message);
        vt_screen_feed(t->screen, msg, strlen(msg));
        g_free(msg);
        gtk_widget_queue_draw(GTK_WIDGET(t));
    }
    if (callback != NULL) {
        callback(t, pid, error, user_data);
    } else if (error != NULL) {
        g_error_free(error);
    }
}

void vte_terminal_watch_child(VteTerminal* t, GPid pid) {
    if (t->child_watch != 0) {
        g_source_remove(t->child_watch);
    }
    t->pid = pid;
    t->child_watch = g_child_watch_add(pid, on_child_exit, t);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void vte_terminal_set_colors(VteTerminal* t, const GdkRGBA* foreground, const GdkRGBA* background,
                             const GdkRGBA* palette, gsize palette_size) {
    if (foreground != NULL) {
        t->fg = *foreground;
    }
    if (background != NULL) {
        t->bg = *background;
    }
    for (gsize i = 0; palette != NULL && i < MIN(palette_size, 16); i++) {
        t->palette[i] = palette[i];
    }
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

void vte_terminal_set_color_cursor(VteTerminal* t, const GdkRGBA* cursor_background) {
    t->has_cursor_color = cursor_background != NULL;
    if (cursor_background != NULL) {
        t->cursor = *cursor_background;
    }
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

void vte_terminal_set_color_highlight(VteTerminal* t, const GdkRGBA* highlight_background) {
    t->has_highlight = highlight_background != NULL;
    if (highlight_background != NULL) {
        t->highlight = *highlight_background;
    }
    gtk_widget_queue_draw(GTK_WIDGET(t));
}

void vte_terminal_set_font(VteTerminal* t, const PangoFontDescription* font_desc) {
    g_clear_pointer(&t->font, pango_font_description_free);
    t->font = font_desc != NULL ? pango_font_description_copy(font_desc) : NULL;
    update_metrics(t);
    gtk_widget_queue_resize(GTK_WIDGET(t));
}

void vte_terminal_copy_clipboard_format(VteTerminal* t, VteFormat format) {
    (void)format;
    gchar* text = selection_text(t);
    if (text != NULL && text[0] != '\0') {
        gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(t)), text);
    }
    g_free(text);
}

void vte_terminal_paste_clipboard(VteTerminal* t) {
    paste_from(t, gtk_widget_get_clipboard(GTK_WIDGET(t)));
}

const char* vte_terminal_get_termprop_string_by_id(VteTerminal* t, int prop, gsize* size) {
    const gchar* title = prop == VTE_PROPERTY_ID_XTERM_TITLE ? vt_screen_title(t->screen) : NULL;
    if (title == NULL || title[0] == '\0') {
        if (size != NULL) {
            *size = 0;
        }
        return NULL;
    }
    if (size != NULL) {
        *size = strlen(title);
    }
    return title;
}

GtkWidget* vte_terminal_new(void) {
    return g_object_new(VTE_TYPE_TERMINAL, NULL);
}

/* ------------------------------------------------------------------------- */
/* GObject                                                                   */
/* ------------------------------------------------------------------------- */

static void vte_terminal_dispose(GObject* object) {
    VteTerminal* t = VTE_TERMINAL(object);
    if (t->child_watch != 0) {
        g_source_remove(t->child_watch);
        t->child_watch = 0;
    }
    vt_pty_free(t->pty); /* ends the shell */
    t->pty = NULL;
    if (t->pid != 0) {
#ifdef G_OS_WIN32
        g_spawn_close_pid(t->pid);
#else
        g_child_watch_add(t->pid, reap_child, NULL); /* no zombie */
#endif
        t->pid = 0;
    }
    if (t->im != NULL) {
        gtk_im_context_set_client_widget(t->im, NULL);
        g_clear_object(&t->im);
    }
    G_OBJECT_CLASS(vte_terminal_parent_class)->dispose(object);
}

static void vte_terminal_finalize(GObject* object) {
    VteTerminal* t = VTE_TERMINAL(object);
    vt_screen_free(t->screen);
    g_clear_pointer(&t->font, pango_font_description_free);
    G_OBJECT_CLASS(vte_terminal_parent_class)->finalize(object);
}

static void vte_terminal_class_init(VteTerminalClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);
    object_class->dispose = vte_terminal_dispose;
    object_class->finalize = vte_terminal_finalize;
    widget_class->snapshot = vte_terminal_snapshot;
    widget_class->measure = vte_terminal_measure;
    widget_class->size_allocate = vte_terminal_size_allocate;
    gtk_widget_class_set_css_name(widget_class, "vte-terminal");

    signals[SIG_CHILD_EXITED] = g_signal_new("child-exited", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                                             0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
    signals[SIG_TERMPROP_CHANGED] = g_signal_new("termprop-changed", G_TYPE_FROM_CLASS(klass),
                                                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0, NULL, NULL,
                                                 NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void vte_terminal_init(VteTerminal* t) {
    GtkWidget* w = GTK_WIDGET(t);
    gtk_widget_set_focusable(w, TRUE);
    gtk_widget_set_focus_on_click(w, TRUE);
    gtk_widget_set_overflow(w, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_cursor_from_name(w, "text");

    /* Until set_colors(): light grey on black, xterm-like palette */
    t->fg = rgba(0.85, 0.85, 0.85);
    t->bg = rgba(0, 0, 0);
    for (gint i = 0; i < 16; i++) {
        double v = i >= 8 ? 1.0 : 0.8;
        t->palette[i] = (i == 7 || i == 8) ? rgba(i == 7 ? 0.8 : 0.5, i == 7 ? 0.8 : 0.5, i == 7 ? 0.8 : 0.5)
                                           : rgba((i & 1) ? v : 0, (i & 2) ? v : 0, (i & 4) ? v : 0);
    }
    t->screen = vt_screen_new(80, 24, TERM_SCROLLBACK);
    update_metrics(t);

    t->im = gtk_im_multicontext_new();
    gtk_im_context_set_client_widget(t->im, w);
    gtk_im_context_set_use_preedit(t->im, FALSE);
    g_signal_connect(t->im, "commit", G_CALLBACK(on_im_commit), t);

    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), t);
    g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), t);
    gtk_widget_add_controller(w, keys);

    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_focus_enter), t);
    g_signal_connect(focus, "leave", G_CALLBACK(on_focus_leave), t);
    gtk_widget_add_controller(w, focus);

    GtkGesture* drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), t);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), t);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), t);
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(drag));

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), t);
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(click));

    GtkGesture* middle = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle), GDK_BUTTON_MIDDLE);
    g_signal_connect(middle, "pressed", G_CALLBACK(on_middle_click), t);
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(middle));

    GtkEventController* wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(wheel, "scroll", G_CALLBACK(on_scroll), t);
    gtk_widget_add_controller(w, wheel);
}

#endif /* HAVE_BUILTIN_TERM */
