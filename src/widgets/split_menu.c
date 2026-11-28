/* The menu of a split handle (see split_menu.h) */
#include "widgets/split_menu.h"
#include "core/i18n.h"
#include "theme/palette.h"
#include "widgets/split_ratio.h"

#define HIDE_DELAY_MS 1000 /* pointer away from the handle and the bar before it hides */
#define BUTTON_SIZE 20
#define BEFORE_KEY "lookin-split-before" /* gdouble*: the share before a collapse */
#define KIND_KEY "lookin-split-kind"

typedef enum {
    BTN_START,   /* collapse (or expand) the start side */
    BTN_DEFAULT, /* the start side at its default width */
    BTN_FIT,     /* fit the start side to its content */
    BTN_RATIO,   /* + index into the shares of the paned shown */
    BTN_END = BTN_RATIO + SPLIT_RATIO_COUNT,
    BTN_COUNT
} ButtonKind;

struct SplitMenu {
    SplitMenuHooks hooks;
    GtkOverlay* overlay;      /* weak */
    GtkWidget* bar;           /* box of the buttons, in the overlay */
    GtkWidget* buttons[BTN_COUNT];
    GtkWidget* hovered;       /* paned whose handle has the pointer (weak), or NULL */
    GtkWidget* shown;         /* paned the bar is shown for (weak), or NULL */
    gboolean on_bar;          /* the pointer is over the bar */
    SplitShares shares;       /* shares offered for the paned shown */
    gint n_shares;            /* their number */
    gint def;                 /* its default position, -1: none */
    gint fit;                 /* its fitted position, -1: no fit */
    guint hide_id;
};

/* --- Geometry of a paned ------------------------------------------------- */

static GtkWidget* paned_handle(GtkWidget* paned) {
    for (GtkWidget* c = gtk_widget_get_first_child(paned); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (g_strcmp0(gtk_widget_get_css_name(c), "separator") == 0) return c;
    }
    return NULL;
}

static gboolean paned_vertical(GtkWidget* paned) {
    return gtk_orientable_get_orientation(GTK_ORIENTABLE(paned)) == GTK_ORIENTATION_VERTICAL;
}

static gint paned_max(GtkWidget* paned) {
    gint max = 0;
    g_object_get(paned, "max-position", &max, NULL);
    return max;
}

static gint paned_min(GtkWidget* paned) {
    gint min = 0;
    g_object_get(paned, "min-position", &min, NULL);
    return min;
}

/* Minimum size of a side along the split, so a share keeps it usable */
static gint child_min(GtkWidget* paned, GtkWidget* child) {
    if (child == NULL || !gtk_widget_get_visible(child)) return 0;
    gint min = 0;
    gtk_widget_measure(child, gtk_orientable_get_orientation(GTK_ORIENTABLE(paned)), -1, &min, NULL, NULL, NULL);
    return min;
}

/* Room of the two sides (the paned's length less its handle's), as GtkPaned
 * counts it: its max position, plus the end side's minimum size when that
 * side cannot shrink. Shares are of that room. */
static gint paned_room(GtkWidget* paned) {
    gint max = paned_max(paned);
    if (!gtk_paned_get_shrink_end_child(GTK_PANED(paned))) {
        max += child_min(paned, gtk_paned_get_end_child(GTK_PANED(paned)));
    }
    return MAX(max, 0);
}

static gint clamp_position(GtkWidget* paned, gint pos) {
    gint min = paned_min(paned), max = paned_max(paned);
    return max >= min ? CLAMP(pos, min, max) : pos;
}

/* Minimum size of a side that GtkPaned enforces: 0 when it may shrink (a
 * share asked for is then given exactly, whatever the side's natural minimum) */
static gint start_min(GtkWidget* paned) {
    if (gtk_paned_get_shrink_start_child(GTK_PANED(paned))) return 0;
    return child_min(paned, gtk_paned_get_start_child(GTK_PANED(paned)));
}

static gint end_min(GtkWidget* paned) {
    if (gtk_paned_get_shrink_end_child(GTK_PANED(paned))) return 0;
    return child_min(paned, gtk_paned_get_end_child(GTK_PANED(paned)));
}

static gboolean both_sides_shown(GtkWidget* paned) {
    GtkWidget* a = gtk_paned_get_start_child(GTK_PANED(paned));
    GtkWidget* b = gtk_paned_get_end_child(GTK_PANED(paned));
    return a != NULL && b != NULL && gtk_widget_get_visible(a) && gtk_widget_get_visible(b);
}

/* --- Drawing ------------------------------------------------------------- */

static void set_source(cairo_t* cr, PaletteRole role, gdouble alpha) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgba(cr, r, g, b, alpha);
}

static void rounded_rect(cairo_t* cr, gdouble x, gdouble y, gdouble w, gdouble h, gdouble r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, G_PI * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, G_PI * 0.5, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, G_PI * 1.5);
    cairo_close_path(cr);
}

/* A box split at share, the start part filled */
static void draw_ratio(cairo_t* cr, gdouble share, gboolean lit, gboolean current) {
    const gdouble x = 3.5, y = 5.5, w = 13.0, h = 9.0;
    gdouble cut = x + w * share;
    cairo_rectangle(cr, x, y, cut - x, h);
    set_source(cr, current ? PAL_ACCENT : (lit ? PAL_FG_BRIGHT : PAL_FG_MUTED), current ? 0.8 : 0.45);
    cairo_fill(cr);
    rounded_rect(cr, x, y, w, h, 1.5);
    set_source(cr, current ? PAL_ACCENT : (lit ? PAL_FG_BRIGHT : PAL_FG_MUTED), 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_move_to(cr, cut, y);
    cairo_line_to(cr, cut, y + h);
    cairo_stroke(cr);
}

/* Lines of text of different lengths, a bar at the end of the longest */
static void draw_fit(cairo_t* cr, gboolean lit, gboolean current) {
    PaletteRole role = current ? PAL_ACCENT : (lit ? PAL_FG_BRIGHT : PAL_FG_MUTED);
    set_source(cr, role, 1.0);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    static const gdouble ends[3] = {9.0, 13.0, 7.0};
    for (gint i = 0; i < 3; i++) {
        gdouble y = 6.5 + 3.5 * i;
        cairo_move_to(cr, 4.0, y);
        cairo_line_to(cr, ends[i], y);
    }
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1.2);
    cairo_move_to(cr, 15.5, 4.5);
    cairo_line_to(cr, 15.5, 15.5);
    cairo_stroke(cr);
}

/* A narrow start part, and a circular arrow back to it in the end part */
static void draw_default(cairo_t* cr, gboolean lit, gboolean current) {
    PaletteRole role = current ? PAL_ACCENT : (lit ? PAL_FG_BRIGHT : PAL_FG_MUTED);
    const gdouble x = 3.5, y = 5.5, w = 13.0, h = 9.0, cut = 7.5;
    cairo_rectangle(cr, x, y, cut - x, h);
    set_source(cr, role, current ? 0.8 : 0.45);
    cairo_fill(cr);
    set_source(cr, role, 1.0);
    cairo_set_line_width(cr, 1.0);
    rounded_rect(cr, x, y, w, h, 1.5);
    cairo_stroke(cr);
    cairo_move_to(cr, cut, y);
    cairo_line_to(cr, cut, y + h);
    cairo_stroke(cr);
    /* Three quarters of a circle, its head pointing up at the start */
    const gdouble cx = 12.0, cy = 10.0, r = 2.4;
    cairo_set_line_width(cr, 1.2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_arc(cr, cx, cy, r, -G_PI * 0.5, G_PI);
    cairo_stroke(cr);
    cairo_move_to(cr, cx - r - 1.3, cy + 0.9);
    cairo_line_to(cr, cx - r, cy - 0.6);
    cairo_line_to(cr, cx - r + 1.3, cy + 0.9);
    cairo_stroke(cr);
}

/* An edge with a chevron: towards it (collapse that side) or away (expand it) */
static void draw_edge(cairo_t* cr, gboolean at_start, gboolean expand, gboolean lit) {
    set_source(cr, lit ? PAL_FG_BRIGHT : PAL_FG_MUTED, 1.0);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    gdouble edge = at_start ? 5.0 : 15.0;
    cairo_move_to(cr, edge, 5.0);
    cairo_line_to(cr, edge, 15.0);
    cairo_stroke(cr);
    /* Pointing to the start when collapsing the start side or expanding the end one */
    gboolean to_start = (at_start != expand);
    gdouble tip = to_start ? 8.5 : 11.5;
    gdouble back = to_start ? 12.5 : 7.5;
    cairo_move_to(cr, back, 6.0);
    cairo_line_to(cr, tip, 10.0);
    cairo_line_to(cr, back, 14.0);
    cairo_stroke(cr);
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    SplitMenu* menu = data;
    GtkWidget* button = gtk_widget_get_parent(GTK_WIDGET(area));
    GtkWidget* paned = menu->shown;
    if (button == NULL || paned == NULL) return;
    gint kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), KIND_KEY));
    GtkStateFlags state = gtk_widget_get_state_flags(button);
    gboolean lit = (state & GTK_STATE_FLAG_PRELIGHT) != 0;
    if ((state & GTK_STATE_FLAG_INSENSITIVE) != 0) cairo_push_group(cr);

    /* Drawn left to right; a vertical paned (top / bottom) swaps the axes */
    cairo_translate(cr, (width - BUTTON_SIZE) * 0.5, (height - BUTTON_SIZE) * 0.5);
    if (paned_vertical(paned)) {
        cairo_matrix_t swap;
        cairo_matrix_init(&swap, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0);
        cairo_transform(cr, &swap);
    }
    gint pos = gtk_paned_get_position(GTK_PANED(paned));
    gint max = paned_room(paned);
    SplitSide collapsed = split_ratio_collapsed(pos, max);
    if (kind == BTN_START) {
        draw_edge(cr, TRUE, collapsed == SPLIT_SIDE_START, lit);
    } else if (kind == BTN_END) {
        draw_edge(cr, FALSE, collapsed == SPLIT_SIDE_END, lit);
    } else if (kind == BTN_DEFAULT) {
        draw_default(cr, lit, collapsed == SPLIT_SIDE_NONE && menu->def >= 0 && ABS(pos - menu->def) <= SPLIT_RATIO_SLACK);
    } else if (kind == BTN_FIT) {
        draw_fit(cr, lit, collapsed == SPLIT_SIDE_NONE && menu->fit >= 0 && ABS(pos - menu->fit) <= SPLIT_RATIO_SLACK);
    } else {
        if (kind - BTN_RATIO >= menu->n_shares) return;
        gdouble share = split_ratio_shares(menu->shares, NULL)[kind - BTN_RATIO];
        gboolean current = collapsed == SPLIT_SIDE_NONE &&
                           split_ratio_matches(pos, max, share, start_min(paned), end_min(paned));
        draw_ratio(cr, share, lit, current);
    }
    if ((state & GTK_STATE_FLAG_INSENSITIVE) != 0) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, 0.35);
    }
}

/* --- Showing and hiding -------------------------------------------------- */

static void clear_timer(guint* id) {
    if (*id != 0) {
        g_source_remove(*id);
        *id = 0;
    }
}

void split_menu_hide(SplitMenu* menu) {
    if (menu == NULL) return;
    clear_timer(&menu->hide_id);
    if (menu->bar != NULL) gtk_widget_set_visible(menu->bar, FALSE);
    g_clear_weak_pointer(&menu->shown);
    menu->on_bar = FALSE;
}

static const gchar* side_tooltip(GtkWidget* paned, gboolean at_start, gboolean expand) {
    if (paned_vertical(paned)) {
        if (expand) return at_start ? _("Expand the Top Side") : _("Expand the Bottom Side");
        return at_start ? _("Collapse the Top Side") : _("Collapse the Bottom Side");
    }
    if (expand) return at_start ? _("Expand the Left Side") : _("Expand the Right Side");
    return at_start ? _("Collapse the Left Side") : _("Collapse the Right Side");
}

/* Buttons for the paned shown: which ones, their tooltips and sensitivity */
static void update_buttons(SplitMenu* menu, gint sides) {
    GtkWidget* paned = menu->shown;
    gint pos = gtk_paned_get_position(GTK_PANED(paned));
    gint max = paned_room(paned);
    SplitSide collapsed = split_ratio_collapsed(pos, max);
    gint smin = start_min(paned), emin = end_min(paned);

    /* A side collapses only if the paned lets it shrink to nothing; the
     * collapsed side can always expand */
    gboolean start_ok = collapsed == SPLIT_SIDE_START ||
                        ((sides & SPLIT_SIDE_START) && gtk_paned_get_shrink_start_child(GTK_PANED(paned)));
    gboolean end_ok = collapsed == SPLIT_SIDE_END ||
                      ((sides & SPLIT_SIDE_END) && gtk_paned_get_shrink_end_child(GTK_PANED(paned)));
    gtk_widget_set_visible(menu->buttons[BTN_START], start_ok);
    gtk_widget_set_visible(menu->buttons[BTN_END], end_ok);
    gtk_widget_set_tooltip_text(menu->buttons[BTN_START], side_tooltip(paned, TRUE, collapsed == SPLIT_SIDE_START));
    gtk_widget_set_tooltip_text(menu->buttons[BTN_END], side_tooltip(paned, FALSE, collapsed == SPLIT_SIDE_END));

    /* The tooltips name the sides: "a third" alone does not say which one */
    static const gchar* const EVEN_TIPS_H[SPLIT_RATIO_COUNT] = {
        N_("Left Side a Third, Right Side Two Thirds"),
        N_("Split in Half (Double-Click the Border)"),
        N_("Left Side Two Thirds, Right Side a Third"),
    };
    static const gchar* const EVEN_TIPS_V[SPLIT_RATIO_COUNT] = {
        N_("Top Side a Third, Bottom Side Two Thirds"),
        N_("Split in Half (Double-Click the Border)"),
        N_("Top Side Two Thirds, Bottom Side a Third"),
    };
    gboolean vertical = paned_vertical(paned);
    gboolean side = menu->shares == SPLIT_SHARES_SIDE;
    const gdouble* shares = split_ratio_shares(menu->shares, &menu->n_shares);
    for (gint i = 0; i < SPLIT_RATIO_COUNT; i++) {
        GtkWidget* b = menu->buttons[BTN_RATIO + i];
        gtk_widget_set_visible(b, i < menu->n_shares);
        if (i >= menu->n_shares) continue;
        /* The double click puts the default width, or fits, rather than
         * halves where the paned has them */
        const gchar* tip;
        if (side) {
            /* A fifth, then a half */
            tip = (i + 1 < menu->n_shares) ? (vertical ? N_("Top Side a Fifth, Bottom Side Four Fifths")
                                                       : N_("Left Side a Fifth, Right Side Four Fifths"))
                                           : N_("Split in Half");
        } else {
            tip = (i == 1 && (menu->def >= 0 || menu->fit >= 0)) ? N_("Split in Half")
                                                                 : (vertical ? EVEN_TIPS_V : EVEN_TIPS_H)[i];
        }
        gtk_widget_set_tooltip_text(b, _(tip));
        gtk_widget_set_sensitive(b, split_ratio_reachable(max, shares[i], smin, emin));
    }
    gtk_widget_set_visible(menu->buttons[BTN_DEFAULT], menu->def >= 0);
    gtk_widget_set_tooltip_text(menu->buttons[BTN_DEFAULT], _("Default Width (Double-Click the Border)"));
    gtk_widget_set_visible(menu->buttons[BTN_FIT], menu->fit >= 0);
    gtk_widget_set_tooltip_text(menu->buttons[BTN_FIT], menu->def >= 0
                                                            ? _("Fit to the Widest Name")
                                                            : _("Fit to the Widest Name (Double-Click the Border)"));
    for (gint i = 0; i < BTN_COUNT; i++) {
        gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(menu->buttons[i])));
    }
}

/* Place the bar in the middle of the handle of the paned shown, beside its
 * line (after it, or before it at the edge of the window): the line stays
 * free to drag */
static gboolean place_bar(SplitMenu* menu) {
    GtkWidget* paned = menu->shown;
    GtkWidget* handle = paned_handle(paned);
    GtkWidget* over = GTK_WIDGET(menu->overlay);
    if (handle == NULL || over == NULL) return FALSE;
    gboolean vertical = paned_vertical(paned);
    /* The bar runs along the handle: across a vertical paned's horizontal line */
    gtk_orientable_set_orientation(GTK_ORIENTABLE(menu->bar),
                                   vertical ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
    /* Measured shown (a hidden widget measures 0) and without the margins
     * that placed it last time (they count in the size) */
    gtk_widget_set_margin_start(menu->bar, 0);
    gtk_widget_set_margin_top(menu->bar, 0);
    gint bw = 0, bh = 0;
    gtk_widget_measure(menu->bar, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &bw, NULL, NULL);
    gtk_widget_measure(menu->bar, GTK_ORIENTATION_VERTICAL, -1, NULL, &bh, NULL, NULL);
    gint ow = gtk_widget_get_width(over), oh = gtk_widget_get_height(over);
    gint hw = gtk_widget_get_width(handle), hh = gtk_widget_get_height(handle);
    graphene_point_t at;
    if (ow <= 0 || oh <= 0 || hw <= 0 || hh <= 0 || bw <= 0 || bh <= 0 ||
        !gtk_widget_compute_point(handle, over, &GRAPHENE_POINT_INIT(0.0f, 0.0f), &at)) {
        return FALSE;
    }
    /* Visible part of the handle (the window may cut it) */
    gdouble from = MAX(vertical ? at.x : at.y, 0.0);
    gdouble to = MIN(vertical ? at.x + hw : at.y + hh, (gdouble)(vertical ? ow : oh));
    gdouble length = vertical ? bw : bh;
    gdouble limit = vertical ? ow : oh;
    gdouble start = CLAMP((from + to - length) * 0.5, 0.0, MAX(limit - length, 0.0));
    gdouble thick = vertical ? bh : bw;
    gdouble room = vertical ? oh : ow;
    gdouble across = vertical ? at.y + hh : at.x + hw;
    if (across + thick > room) across = (vertical ? at.y : at.x) - thick;
    across = CLAMP(across, 0.0, MAX(room - thick, 0.0));
    gtk_widget_set_margin_start(menu->bar, (gint)(vertical ? start : across));
    gtk_widget_set_margin_top(menu->bar, (gint)(vertical ? across : start));
    return TRUE;
}

/* Fitted position of the start side of paned, clamped to its range; -1: none */
static gint paned_fit(SplitMenu* menu, GtkWidget* paned) {
    gint fit = menu->hooks.fit != NULL ? menu->hooks.fit(paned, menu->hooks.data) : -1;
    return fit >= 0 ? clamp_position(paned, fit) : -1;
}

/* Default position of the start side of paned, clamped to its range; -1: none */
static gint paned_default(SplitMenu* menu, GtkWidget* paned) {
    gint width = menu->hooks.default_pos != NULL ? menu->hooks.default_pos(paned, menu->hooks.data) : -1;
    gint def = split_ratio_width(width, paned_room(paned), start_min(paned), end_min(paned));
    return def >= 0 ? clamp_position(paned, def) : -1;
}

static void show_for(SplitMenu* menu, GtkWidget* paned) {
    if (menu->bar == NULL || !gtk_widget_get_mapped(paned) || !both_sides_shown(paned)) return;
    gint sides = menu->hooks.sides != NULL ? menu->hooks.sides(paned, menu->hooks.data) : 0;
    if (sides < 0) return;
    g_set_weak_pointer(&menu->shown, paned);
    menu->shares = menu->hooks.shares != NULL ? menu->hooks.shares(paned, menu->hooks.data) : SPLIT_SHARES_EVEN;
    menu->def = paned_default(menu, paned);
    menu->fit = paned_fit(menu, paned);
    update_buttons(menu, sides);
    gtk_widget_set_visible(menu->bar, TRUE);
    if (!place_bar(menu)) split_menu_hide(menu);
}

static gboolean hide_now(gpointer data) {
    SplitMenu* menu = data;
    menu->hide_id = 0;
    if (!menu->on_bar && menu->hovered == NULL) split_menu_hide(menu);
    return G_SOURCE_REMOVE;
}

static void schedule_hide(SplitMenu* menu) {
    clear_timer(&menu->hide_id);
    if (menu->shown != NULL) menu->hide_id = g_timeout_add(HIDE_DELAY_MS, hide_now, menu);
}

/* --- Pointer on a handle ------------------------------------------------- */

static void handle_pointer(SplitMenu* menu, GtkEventController* ctrl) {
    GtkWidget* paned = gtk_widget_get_parent(gtk_event_controller_get_widget(ctrl));
    if ((gtk_event_controller_get_current_event_state(ctrl) & GDK_BUTTON1_MASK) != 0) {
        split_menu_hide(menu); /* a drag */
        return;
    }
    g_set_weak_pointer(&menu->hovered, paned);
    clear_timer(&menu->hide_id);
    if (menu->shown == paned) return; /* the bar stays where it is */
    if (menu->shown != NULL) split_menu_hide(menu);
    show_for(menu, paned);
}

static void on_handle_enter(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data) {
    (void)x;
    (void)y;
    handle_pointer(data, GTK_EVENT_CONTROLLER(ctrl));
}

/* Also after a drag: the bar comes back on the first move */
static void on_handle_motion(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data) {
    (void)x;
    (void)y;
    handle_pointer(data, GTK_EVENT_CONTROLLER(ctrl));
}

static void on_handle_leave(GtkEventControllerMotion* ctrl, gpointer data) {
    (void)ctrl;
    SplitMenu* menu = data;
    g_clear_weak_pointer(&menu->hovered);
    schedule_hide(menu);
}

/* --- The bar ------------------------------------------------------------- */

static void move_handle(SplitMenu* menu, GtkWidget* paned, gint pos) {
    gint cur = gtk_paned_get_position(GTK_PANED(paned));
    gint max = paned_room(paned);
    pos = clamp_position(paned, pos);
    /* Remember the share before a collapse, to expand back to it */
    if (split_ratio_collapsed(cur, max) == SPLIT_SIDE_NONE && split_ratio_collapsed(pos, max) != SPLIT_SIDE_NONE &&
        max > 0) {
        gdouble* before = g_new(gdouble, 1);
        *before = (gdouble)cur / max;
        g_object_set_data_full(G_OBJECT(paned), BEFORE_KEY, before, g_free);
    }
    if (menu->hooks.apply != NULL) {
        menu->hooks.apply(paned, pos, menu->hooks.data);
    } else {
        gtk_paned_set_position(GTK_PANED(paned), pos);
    }
}

/* Expand the collapsed side of paned back to the share it had */
static void expand_collapsed(SplitMenu* menu, GtkWidget* paned) {
    gdouble* before = g_object_get_data(G_OBJECT(paned), BEFORE_KEY);
    move_handle(menu, paned,
                split_ratio_expand(paned_room(paned), before != NULL ? *before : -1.0, start_min(paned),
                                   end_min(paned)));
}

static void on_button_clicked(GtkButton* button, gpointer data) {
    SplitMenu* menu = data;
    GtkWidget* paned = menu->shown;
    if (paned == NULL) return;
    g_object_ref(paned);
    gint kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), KIND_KEY));
    gint pos = gtk_paned_get_position(GTK_PANED(paned));
    gint max = paned_room(paned);
    gint smin = start_min(paned), emin = end_min(paned);
    SplitSide collapsed = split_ratio_collapsed(pos, max);
    split_menu_hide(menu); /* the handle moves away from the bar */
    gint target;
    if ((kind == BTN_START && collapsed == SPLIT_SIDE_START) || (kind == BTN_END && collapsed == SPLIT_SIDE_END)) {
        expand_collapsed(menu, paned);
        g_object_unref(paned);
        return;
    } else if (kind == BTN_START) {
        target = paned_min(paned);
    } else if (kind == BTN_END) {
        target = max;
    } else if (kind == BTN_DEFAULT || kind == BTN_FIT) {
        /* Measured again: the font or the rows may have changed */
        target = kind == BTN_DEFAULT ? paned_default(menu, paned) : paned_fit(menu, paned);
        if (target < 0) {
            g_object_unref(paned);
            return;
        }
    } else {
        SplitShares set = menu->hooks.shares != NULL ? menu->hooks.shares(paned, menu->hooks.data) : SPLIT_SHARES_EVEN;
        gint count = 0;
        const gdouble* shares = split_ratio_shares(set, &count);
        if (kind - BTN_RATIO >= count) {
            g_object_unref(paned);
            return;
        }
        target = split_ratio_position(max, shares[kind - BTN_RATIO], smin, emin);
    }
    move_handle(menu, paned, target);
    g_object_unref(paned);
}

/* --- A collapsed side ----------------------------------------------------- */

/* The side of paned collapsed at its border, with both sides shown */
static SplitSide paned_collapsed(GtkWidget* paned) {
    if (!both_sides_shown(paned)) return SPLIT_SIDE_NONE;
    return split_ratio_collapsed(gtk_paned_get_position(GTK_PANED(paned)), paned_room(paned));
}

static const gchar* collapsed_tooltip(GtkWidget* paned, SplitSide side) {
    gboolean start = side == SPLIT_SIDE_START;
    if (paned_vertical(paned)) {
        return start ? _("Top Side Collapsed: Click to Expand") : _("Bottom Side Collapsed: Click to Expand");
    }
    return start ? _("Left Side Collapsed: Click to Expand") : _("Right Side Collapsed: Click to Expand");
}

/* Mark the handle of a collapsed split (the theme draws it as a coloured
 * strip, so the collapsed side can be seen at the window's edge) */
static void update_collapsed_mark(GtkWidget* paned) {
    GtkWidget* handle = paned_handle(paned);
    if (handle == NULL) return;
    SplitSide side = paned_collapsed(paned);
    if (side == SPLIT_SIDE_NONE) {
        gtk_widget_remove_css_class(handle, "collapsed");
        gtk_widget_set_tooltip_text(handle, NULL);
    } else {
        gtk_widget_add_css_class(handle, "collapsed");
        gtk_widget_set_tooltip_text(handle, collapsed_tooltip(paned, side));
    }
}

static void on_paned_geometry(GObject* object, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    (void)data;
    update_collapsed_mark(GTK_WIDGET(object));
}

static void on_side_visible(GObject* child, GParamSpec* pspec, gpointer data) {
    (void)child;
    (void)pspec;
    update_collapsed_mark(GTK_WIDGET(data));
}

/* Follow the visibility of the sides of paned (a hidden side has no border) */
static void watch_sides(GtkWidget* paned) {
    GtkWidget* sides[2] = {gtk_paned_get_start_child(GTK_PANED(paned)), gtk_paned_get_end_child(GTK_PANED(paned))};
    for (gint i = 0; i < 2; i++) {
        if (sides[i] == NULL) continue;
        g_signal_handlers_disconnect_by_func(sides[i], on_side_visible, paned);
        g_signal_connect_object(sides[i], "notify::visible", G_CALLBACK(on_side_visible), paned, 0);
    }
    update_collapsed_mark(paned);
}

static void on_paned_child(GObject* object, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    (void)data;
    watch_sides(GTK_WIDGET(object));
}

/* A click on the handle of a collapsed split (without a drag: the handle has
 * not moved) expands the side back. The press position is kept on the paned. */
#define PRESS_KEY "lookin-split-press" /* position + 1 at the press, 0: none */

static gboolean on_paned_event(GtkEventControllerLegacy* ctrl, GdkEvent* event, gpointer data) {
    SplitMenu* menu = data;
    GtkWidget* paned = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    GdkEventType type = gdk_event_get_event_type(event);
    if (type != GDK_BUTTON_PRESS && type != GDK_BUTTON_RELEASE) return FALSE;
    if (gdk_button_event_get_button(event) != GDK_BUTTON_PRIMARY) return FALSE;
    gint pos = gtk_paned_get_position(GTK_PANED(paned));
    if (type == GDK_BUTTON_PRESS) {
        GtkWidget* handle = paned_handle(paned);
        gdouble x = 0.0, y = 0.0;
        graphene_point_t at;
        gboolean on_handle = FALSE;
        if (handle != NULL && gtk_widget_has_css_class(handle, "collapsed") && gdk_event_get_position(event, &x, &y)) {
            GtkNative* native = gtk_widget_get_native(paned);
            double nx = 0.0, ny = 0.0;
            if (native != NULL) gtk_native_get_surface_transform(native, &nx, &ny);
            on_handle = gtk_widget_compute_point(GTK_WIDGET(native), handle,
                                                 &GRAPHENE_POINT_INIT((float)(x - nx), (float)(y - ny)), &at) &&
                        gtk_widget_contains(handle, at.x, at.y);
        }
        g_object_set_data(G_OBJECT(paned), PRESS_KEY, GINT_TO_POINTER(on_handle ? pos + 1 : 0));
        return FALSE;
    }
    gint pressed = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(paned), PRESS_KEY));
    g_object_set_data(G_OBJECT(paned), PRESS_KEY, GINT_TO_POINTER(0));
    if (pressed == 0 || pressed - 1 != pos || paned_collapsed(paned) == SPLIT_SIDE_NONE) return FALSE;
    gint sides = menu->hooks.sides != NULL ? menu->hooks.sides(paned, menu->hooks.data) : 0;
    if (sides < 0) return FALSE;
    split_menu_hide(menu);
    expand_collapsed(menu, paned);
    return FALSE;
}

static void on_bar_enter(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    (void)x;
    (void)y;
    SplitMenu* menu = data;
    menu->on_bar = TRUE;
    clear_timer(&menu->hide_id);
}

static void on_bar_leave(GtkEventControllerMotion* ctrl, gpointer data) {
    (void)ctrl;
    SplitMenu* menu = data;
    menu->on_bar = FALSE;
    if (menu->hovered == NULL) schedule_hide(menu);
}

static void on_button_state(GtkWidget* button, GtkStateFlags previous, gpointer data) {
    (void)previous;
    (void)data;
    gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(button)));
}

void split_menu_set_overlay(SplitMenu* menu, GtkOverlay* overlay) {
    if (menu == NULL || overlay == NULL || menu->bar != NULL) return;
    g_set_weak_pointer(&menu->overlay, overlay);
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_name(bar, "split-menu");
    gtk_widget_set_halign(bar, GTK_ALIGN_START);
    gtk_widget_set_valign(bar, GTK_ALIGN_START);
    gtk_widget_set_visible(bar, FALSE);
    for (gint i = 0; i < BTN_COUNT; i++) {
        GtkWidget* b = gtk_button_new();
        gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
        gtk_widget_set_focusable(b, FALSE);
        gtk_widget_add_css_class(b, "split-menu-btn");
        g_object_set_data(G_OBJECT(b), KIND_KEY, GINT_TO_POINTER(i));
        GtkWidget* area = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), BUTTON_SIZE);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), BUTTON_SIZE);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, menu, NULL);
        gtk_button_set_child(GTK_BUTTON(b), area);
        g_signal_connect(b, "clicked", G_CALLBACK(on_button_clicked), menu);
        g_signal_connect(b, "state-flags-changed", G_CALLBACK(on_button_state), NULL);
        gtk_box_append(GTK_BOX(bar), b);
        menu->buttons[i] = b;
    }
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_bar_enter), menu);
    g_signal_connect(motion, "leave", G_CALLBACK(on_bar_leave), menu);
    gtk_widget_add_controller(bar, motion);
    gtk_overlay_add_overlay(overlay, bar);
    menu->bar = bar;
}

void split_menu_reset(SplitMenu* menu, GtkWidget* paned) {
    if (menu == NULL || !GTK_IS_PANED(paned) || !both_sides_shown(paned)) return;
    gint sides = menu->hooks.sides != NULL ? menu->hooks.sides(paned, menu->hooks.data) : 0;
    if (sides < 0) return;
    split_menu_hide(menu);
    gint target = paned_default(menu, paned);
    if (target < 0) target = paned_fit(menu, paned);
    if (target < 0) target = split_ratio_position(paned_room(paned), 0.5, start_min(paned), end_min(paned));
    move_handle(menu, paned, target);
}

void split_menu_attach(SplitMenu* menu, GtkWidget* paned) {
    GtkWidget* handle = (menu != NULL && GTK_IS_PANED(paned)) ? paned_handle(paned) : NULL;
    if (handle == NULL) return;
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_handle_enter), menu);
    g_signal_connect(motion, "motion", G_CALLBACK(on_handle_motion), menu);
    g_signal_connect(motion, "leave", G_CALLBACK(on_handle_leave), menu);
    gtk_widget_add_controller(handle, motion);

    GtkEventController* legacy = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(legacy, GTK_PHASE_CAPTURE);
    g_signal_connect(legacy, "event", G_CALLBACK(on_paned_event), menu);
    gtk_widget_add_controller(paned, legacy);

    g_signal_connect(paned, "notify::position", G_CALLBACK(on_paned_geometry), NULL);
    g_signal_connect(paned, "notify::max-position", G_CALLBACK(on_paned_geometry), NULL);
    g_signal_connect(paned, "notify::start-child", G_CALLBACK(on_paned_child), NULL);
    g_signal_connect(paned, "notify::end-child", G_CALLBACK(on_paned_child), NULL);
    watch_sides(paned);
}

SplitMenu* split_menu_new(const SplitMenuHooks* hooks) {
    SplitMenu* menu = g_new0(SplitMenu, 1);
    menu->fit = -1;
    if (hooks != NULL) menu->hooks = *hooks;
    return menu;
}

void split_menu_free(SplitMenu* menu) {
    if (menu == NULL) return;
    clear_timer(&menu->hide_id);
    g_clear_weak_pointer(&menu->hovered);
    g_clear_weak_pointer(&menu->shown);
    g_clear_weak_pointer(&menu->overlay);
    g_free(menu);
}
