/* Text format bar of the Canvas page (see canvas_format_bar.h) */
#include "canvas_format_bar.h"
#include "canvas_doc.h"
#include "icons.h"
#include "theme.h"

#define BAR_GAP 6        /* between the bar and the block */
#define EDGE 8           /* the bar keeps this far from the overlay sides */
#define TOOLBAR_ROOM 60  /* the tool bar at the top of the page */

/* Text colours: style flags, and the palette role showing them */
static const struct {
    guint flag;
    const gchar* color;
    const gchar* tip;
} TEXT_COLORS[] = {
    {CANVAS_STYLE_MUTED, "fg_subtle", "Grey Text"},
    {CANVAS_STYLE_ACCENT, "accent", "Accent Text"},
    {CANVAS_STYLE_DANGER, "error", "Red Text"},
    {CANVAS_STYLE_SUCCESS, "success", "Green Text"},
    {CANVAS_STYLE_WARNING, "warning", "Yellow Text"},
};

struct CanvasFormatBar {
    GtkWidget* box;
    GtkWidget* target; /* weak */
    guint tick;
    CanvasFormatBarCallbacks cbs;
    gpointer user_data;
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    GString* css = g_string_new(
        ".canvas-format-bar { background-color: @bg_raised; border: 1px solid @border; border-radius: 8px;"
        " padding: 3px 4px; box-shadow: 0 2px 8px @shadow; }"
        ".canvas-fmt { min-width: 26px; min-height: 26px; padding: 2px; border-radius: 6px;"
        " background: none; border: none; box-shadow: none; color: @fg; }"
        ".canvas-fmt:hover { background-color: @bg_hover; }"
        ".canvas-fmt-sep { margin: 4px 3px; }"
        ".canvas-fmt-swatch { min-width: 14px; min-height: 14px; border-radius: 50%; }");
    for (guint i = 0; i < G_N_ELEMENTS(TEXT_COLORS); i++) {
        g_string_append_printf(css, ".canvas-fmt-c-%s { background-color: @%s; }", TEXT_COLORS[i].color,
                               TEXT_COLORS[i].color);
    }
    theme_add_css(css->str);
    g_string_free(css, TRUE);
}

/* ------------------------------------------------------------------------- */
/* Buttons                                                                   */
/* ------------------------------------------------------------------------- */

static void popdown(GtkWidget* item) {
    GtkWidget* pop = gtk_widget_get_ancestor(item, GTK_TYPE_POPOVER);
    if (pop != NULL) gtk_popover_popdown(GTK_POPOVER(pop));
}

static void on_style_clicked(GtkButton* btn, gpointer data) {
    CanvasFormatBar* bar = data;
    popdown(GTK_WIDGET(btn));
    guint flag = (guint)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "style-flag"));
    if (bar->cbs.on_style) bar->cbs.on_style(flag, bar->user_data);
}

static void on_preset_clicked(GtkButton* btn, gpointer data) {
    CanvasFormatBar* bar = data;
    popdown(GTK_WIDGET(btn));
    CanvasPreset preset = (CanvasPreset)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "preset"));
    if (bar->cbs.on_preset) bar->cbs.on_preset(preset, bar->user_data);
}

static void on_list_clicked(GtkButton* btn, gpointer data) {
    CanvasFormatBar* bar = data;
    const gchar* prefix = g_object_get_data(G_OBJECT(btn), "list-prefix");
    if (bar->cbs.on_list) bar->cbs.on_list(prefix, bar->user_data);
}

static void on_checkbox_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasFormatBar* bar = data;
    if (bar->cbs.on_checkbox) bar->cbs.on_checkbox(bar->user_data);
}

static void on_indent_clicked(GtkButton* btn, gpointer data) {
    CanvasFormatBar* bar = data;
    gint step = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "indent-step"));
    if (bar->cbs.on_indent) bar->cbs.on_indent(step, bar->user_data);
}

static GtkWidget* swatch_new(const gchar* color) {
    GtkWidget* dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(dot, "canvas-fmt-swatch");
    gchar* cls = g_strconcat("canvas-fmt-c-", color, NULL);
    gtk_widget_add_css_class(dot, cls);
    g_free(cls);
    gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    return dot;
}

/* Buttons never take the focus: the text keeps it */
static GtkWidget* action_button(GtkWidget* child, const gchar* tip, GCallback cb, CanvasFormatBar* bar) {
    GtkWidget* b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), child);
    gtk_widget_add_css_class(b, "canvas-fmt");
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_focusable(b, FALSE);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, bar);
    return b;
}

static GtkWidget* style_button(const gchar* icon, const gchar* tip, guint flag, CanvasFormatBar* bar) {
    GtkWidget* b = action_button(icon_image_new(icon), tip, G_CALLBACK(on_style_clicked), bar);
    g_object_set_data(G_OBJECT(b), "style-flag", GINT_TO_POINTER((gint)flag));
    return b;
}

/* Menu button; its popover box is filled by the caller */
static GtkWidget* menu_button(GtkWidget* child, const gchar* tip, GtkWidget** pop_box, GtkOrientation o) {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(mb), child);
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(mb), FALSE);
    gtk_widget_add_css_class(mb, "canvas-fmt");
    gtk_widget_set_focus_on_click(mb, FALSE);
    gtk_widget_set_tooltip_text(mb, tip);
    GtkWidget* pop = gtk_popover_new();
    *pop_box = gtk_box_new(o, 2);
    gtk_popover_set_child(GTK_POPOVER(pop), *pop_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    return mb;
}

static GtkWidget* pop_item(GtkWidget* pop_box, const gchar* label, GCallback cb, CanvasFormatBar* bar) {
    GtkWidget* item = gtk_button_new_with_label(label);
    gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
    gtk_widget_add_css_class(item, "canvas-pop-item");
    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(item))), 0.0f);
    g_signal_connect(item, "clicked", cb, bar);
    gtk_box_append(GTK_BOX(pop_box), item);
    return item;
}

static void add_sep(GtkWidget* row) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(sep, "canvas-fmt-sep");
    gtk_box_append(GTK_BOX(row), sep);
}

static void build(CanvasFormatBar* bar, GtkWidget* row) {
    GtkWidget* pop_box;
    GtkWidget* presets = menu_button(icon_image_new("format-text-rich"), "Paragraph Style (Ctrl+Alt+1, 2, 0, Q)",
                                     &pop_box, GTK_ORIENTATION_VERTICAL);
    static const struct {
        const gchar* name;
        CanvasPreset preset;
    } PRESETS[] = {
        {"Heading 1", CANVAS_PRESET_H1},
        {"Heading 2", CANVAS_PRESET_H2},
        {"Body Text", CANVAS_PRESET_BODY},
        {"Quote", CANVAS_PRESET_QUOTE},
    };
    for (guint i = 0; i < G_N_ELEMENTS(PRESETS); i++) {
        GtkWidget* item = pop_item(pop_box, PRESETS[i].name, G_CALLBACK(on_preset_clicked), bar);
        g_object_set_data(G_OBJECT(item), "preset", GINT_TO_POINTER((gint)PRESETS[i].preset));
    }
    gtk_box_append(GTK_BOX(row), presets);
    add_sep(row);
    gtk_box_append(GTK_BOX(row), style_button("format-text-bold", "Bold (Ctrl+B)", CANVAS_STYLE_BOLD, bar));
    gtk_box_append(GTK_BOX(row), style_button("format-text-italic", "Italic (Ctrl+I)", CANVAS_STYLE_ITALIC, bar));
    gtk_box_append(GTK_BOX(row),
                   style_button("format-text-underline", "Underline (Ctrl+U)", CANVAS_STYLE_UNDERLINE, bar));
    gtk_box_append(GTK_BOX(row), style_button("format-text-strikethrough", "Strikethrough (Ctrl+Shift+S)",
                                              CANVAS_STYLE_STRIKE, bar));

    GtkWidget* colors = menu_button(swatch_new("accent"), "Text Color", &pop_box, GTK_ORIENTATION_HORIZONTAL);
    for (guint i = 0; i < G_N_ELEMENTS(TEXT_COLORS); i++) {
        GtkWidget* sw = action_button(swatch_new(TEXT_COLORS[i].color), TEXT_COLORS[i].tip,
                                      G_CALLBACK(on_style_clicked), bar);
        g_object_set_data(G_OBJECT(sw), "style-flag", GINT_TO_POINTER((gint)TEXT_COLORS[i].flag));
        gtk_box_append(GTK_BOX(pop_box), sw);
    }
    gtk_box_append(GTK_BOX(row), colors);

    GtkWidget* more = menu_button(icon_image_new("view-more"), "More Styles", &pop_box, GTK_ORIENTATION_VERTICAL);
    static const struct {
        const gchar* name;
        guint flag;
    } MORE[] = {
        {"Highlight (Ctrl+H)", CANVAS_STYLE_HIGHLIGHT},
        {"Code (Ctrl+E)", CANVAS_STYLE_CODE},
        {"Serif Font", CANVAS_STYLE_SERIF},
        {"Monospace Font", CANVAS_STYLE_MONO},
        {"Small Size", CANVAS_STYLE_SMALL},
        {"Large Size", CANVAS_STYLE_LARGE},
    };
    for (guint i = 0; i < G_N_ELEMENTS(MORE); i++) {
        GtkWidget* item = pop_item(pop_box, MORE[i].name, G_CALLBACK(on_style_clicked), bar);
        g_object_set_data(G_OBJECT(item), "style-flag", GINT_TO_POINTER((gint)MORE[i].flag));
    }
    gtk_box_append(GTK_BOX(row), more);
    add_sep(row);

    GtkWidget* b = action_button(icon_image_new("view-list-bullet"), "Bullet List", G_CALLBACK(on_list_clicked), bar);
    g_object_set_data(G_OBJECT(b), "list-prefix", (gpointer)"- ");
    gtk_box_append(GTK_BOX(row), b);
    b = action_button(icon_image_new("view-list-ordered"), "Numbered List", G_CALLBACK(on_list_clicked), bar);
    g_object_set_data(G_OBJECT(b), "list-prefix", (gpointer)"1. ");
    gtk_box_append(GTK_BOX(row), b);
    gtk_box_append(GTK_BOX(row), action_button(icon_image_new("object-select"), "Task Checkbox (Ctrl+Space)",
                                               G_CALLBACK(on_checkbox_clicked), bar));
    b = action_button(icon_image_new("format-indent-less"), "Decrease Indent", G_CALLBACK(on_indent_clicked), bar);
    g_object_set_data(G_OBJECT(b), "indent-step", GINT_TO_POINTER(-1));
    gtk_box_append(GTK_BOX(row), b);
    b = action_button(icon_image_new("format-indent-more"), "Increase Indent", G_CALLBACK(on_indent_clicked), bar);
    g_object_set_data(G_OBJECT(b), "indent-step", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(row), b);
}

/* ------------------------------------------------------------------------- */
/* Placement                                                                 */
/* ------------------------------------------------------------------------- */

/* Above the block, at its left; below it when the tool bar is in the way;
 * inside the overlay */
static void place(CanvasFormatBar* bar) {
    GtkWidget* overlay = gtk_widget_get_parent(bar->box);
    if (overlay == NULL || bar->target == NULL) return;
    graphene_point_t top, bottom;
    gint th = gtk_widget_get_height(bar->target);
    if (!gtk_widget_compute_point(bar->target, overlay, &GRAPHENE_POINT_INIT(0, 0), &top) ||
        !gtk_widget_compute_point(bar->target, overlay, &GRAPHENE_POINT_INIT(0, (float)th), &bottom)) {
        return;
    }
    gint bw = gtk_widget_get_width(bar->box), bh = gtk_widget_get_height(bar->box);
    gint ow = gtk_widget_get_width(overlay), oh = gtk_widget_get_height(overlay);
    gint x = CLAMP((gint)top.x, EDGE, MAX(ow - bw - EDGE, EDGE));
    gint y = (gint)top.y - bh - BAR_GAP;
    if (y < TOOLBAR_ROOM) y = (gint)bottom.y + BAR_GAP;
    y = CLAMP(y, TOOLBAR_ROOM, MAX(oh - bh - EDGE, TOOLBAR_ROOM));
    if (gtk_widget_get_margin_start(bar->box) != x) gtk_widget_set_margin_start(bar->box, x);
    if (gtk_widget_get_margin_top(bar->box) != y) gtk_widget_set_margin_top(bar->box, y);
}

/* Each frame while shown: the block may move, the view scroll or zoom */
static gboolean on_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)widget;
    (void)clock;
    CanvasFormatBar* bar = data;
    if (bar->target == NULL || gtk_widget_get_root(bar->target) == NULL) {
        bar->tick = 0; /* removed by returning G_SOURCE_REMOVE */
        canvas_format_bar_show_for(bar, NULL);
        return G_SOURCE_REMOVE;
    }
    place(bar);
    return G_SOURCE_CONTINUE;
}

void canvas_format_bar_show_for(CanvasFormatBar* bar, GtkWidget* target) {
    if (bar == NULL) return;
    if (target == NULL) {
        g_clear_weak_pointer(&bar->target);
        if (bar->tick != 0) gtk_widget_remove_tick_callback(bar->box, bar->tick);
        bar->tick = 0;
        gtk_widget_set_visible(bar->box, FALSE);
        return;
    }
    g_set_weak_pointer(&bar->target, target);
    gtk_widget_set_visible(bar->box, TRUE);
    place(bar);
    if (bar->tick == 0) bar->tick = gtk_widget_add_tick_callback(bar->box, on_tick, bar, NULL);
}

/* ------------------------------------------------------------------------- */

CanvasFormatBar* canvas_format_bar_new(const CanvasFormatBarCallbacks* cbs, gpointer user_data) {
    ensure_css();
    CanvasFormatBar* bar = g_new0(CanvasFormatBar, 1);
    if (cbs != NULL) bar->cbs = *cbs;
    bar->user_data = user_data;
    bar->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    gtk_widget_add_css_class(bar->box, "canvas-format-bar");
    gtk_widget_set_halign(bar->box, GTK_ALIGN_START);
    gtk_widget_set_valign(bar->box, GTK_ALIGN_START);
    build(bar, bar->box);
    gtk_widget_set_visible(bar->box, FALSE);
    return bar;
}

GtkWidget* canvas_format_bar_get_widget(CanvasFormatBar* bar) {
    return bar != NULL ? bar->box : NULL;
}

void canvas_format_bar_free(CanvasFormatBar* bar) {
    if (bar == NULL) return;
    g_clear_weak_pointer(&bar->target);
    g_free(bar);
}
