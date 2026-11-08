/* Bar floating next to a Canvas block (see canvas_float.h) */
#include "canvas/canvas_float.h"
#include "i18n.h"
#include "theme.h"

#define BAR_GAP 6        /* between the bar and the block */
#define EDGE 8           /* the bar keeps this far from the overlay sides */
#define TOOLBAR_ROOM 60  /* the tool bar at the top of the page */

struct CanvasFloat {
    GtkWidget* box;
    GtkWidget* target; /* weak */
    guint tick;
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-float-bar { background-color: @bg_raised; border: 1px solid @border; border-radius: 8px;"
                  " padding: 3px 4px; box-shadow: 0 2px 8px @shadow; }"
                  ".canvas-fmt { min-width: 26px; min-height: 26px; padding: 2px; border-radius: 6px;"
                  " background: none; border: none; box-shadow: none; color: @fg; }"
                  ".canvas-fmt:hover { background-color: @bg_hover; }"
                  ".canvas-fmt:checked { background-color: @accent; color: @accent_text; }"
                  ".canvas-fmt:disabled { opacity: 0.4; }"
                  ".canvas-fmt-sep { margin: 4px 3px; }");
}

/* Above the block, at its left; below it when the tool bar is in the way;
 * inside the overlay */
static void place(CanvasFloat* f) {
    GtkWidget* overlay = gtk_widget_get_parent(f->box);
    if (overlay == NULL || f->target == NULL) return;
    graphene_point_t top, bottom;
    gint th = gtk_widget_get_height(f->target);
    if (!gtk_widget_compute_point(f->target, overlay, &GRAPHENE_POINT_INIT(0, 0), &top) ||
        !gtk_widget_compute_point(f->target, overlay, &GRAPHENE_POINT_INIT(0, (float)th), &bottom)) {
        return;
    }
    gint bw = gtk_widget_get_width(f->box), bh = gtk_widget_get_height(f->box);
    gint ow = gtk_widget_get_width(overlay), oh = gtk_widget_get_height(overlay);
    gint x = CLAMP((gint)top.x, EDGE, MAX(ow - bw - EDGE, EDGE));
    gint y = (gint)top.y - bh - BAR_GAP;
    if (y < TOOLBAR_ROOM) y = (gint)bottom.y + BAR_GAP;
    y = CLAMP(y, TOOLBAR_ROOM, MAX(oh - bh - EDGE, TOOLBAR_ROOM));
    if (gtk_widget_get_margin_start(f->box) != x) gtk_widget_set_margin_start(f->box, x);
    if (gtk_widget_get_margin_top(f->box) != y) gtk_widget_set_margin_top(f->box, y);
}

/* Each frame while shown: the block may move, the view scroll or zoom */
static gboolean on_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)widget;
    (void)clock;
    CanvasFloat* f = data;
    if (f->target == NULL || gtk_widget_get_root(f->target) == NULL) {
        f->tick = 0; /* removed by returning G_SOURCE_REMOVE */
        canvas_float_show_for(f, NULL);
        return G_SOURCE_REMOVE;
    }
    place(f);
    return G_SOURCE_CONTINUE;
}

void canvas_float_show_for(CanvasFloat* f, GtkWidget* target) {
    if (f == NULL) return;
    if (target == NULL) {
        g_clear_weak_pointer(&f->target);
        if (f->tick != 0) gtk_widget_remove_tick_callback(f->box, f->tick);
        f->tick = 0;
        gtk_widget_set_visible(f->box, FALSE);
        return;
    }
    g_set_weak_pointer(&f->target, target);
    gtk_widget_set_visible(f->box, TRUE);
    place(f);
    if (f->tick == 0) f->tick = gtk_widget_add_tick_callback(f->box, on_tick, f, NULL);
}

GtkWidget* canvas_float_button(GtkWidget* child, const gchar* tip, GCallback cb, gpointer data) {
    GtkWidget* b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), child);
    gtk_widget_add_css_class(b, "canvas-fmt");
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_focusable(b, FALSE);
    gtk_widget_set_tooltip_text(b, _(tip)); /* tip: an N_() string */
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

void canvas_float_add_separator(GtkWidget* bar) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(sep, "canvas-fmt-sep");
    gtk_box_append(GTK_BOX(bar), sep);
}

CanvasFloat* canvas_float_new(GtkWidget* box) {
    ensure_css();
    CanvasFloat* f = g_new0(CanvasFloat, 1);
    f->box = box;
    gtk_widget_add_css_class(box, "canvas-float-bar");
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_visible(box, FALSE);
    return f;
}

void canvas_float_free(CanvasFloat* f) {
    if (f == NULL) return;
    g_clear_weak_pointer(&f->target);
    g_free(f);
}
