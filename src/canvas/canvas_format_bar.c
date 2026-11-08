/* Text format bar of the Canvas page (see canvas_format_bar.h) */
#include "canvas_format_bar.h"
#include "canvas_doc.h"
#include "canvas_float.h"
#include "i18n.h"
#include "icons.h"
#include "theme.h"

/* Text colours: style flags, and the palette role showing them */
static const struct {
    guint flag;
    const gchar* color;
    const gchar* tip;
} TEXT_COLORS[] = {
    {CANVAS_STYLE_MUTED, "fg_subtle", N_("Grey Text")},
    {CANVAS_STYLE_ACCENT, "accent", N_("Accent Text")},
    {CANVAS_STYLE_DANGER, "error", N_("Red Text")},
    {CANVAS_STYLE_SUCCESS, "success", N_("Green Text")},
    {CANVAS_STYLE_WARNING, "warning", N_("Yellow Text")},
};

struct CanvasFormatBar {
    GtkWidget* box;
    CanvasFloat* floating;
    CanvasFormatBarCallbacks cbs;
    gpointer user_data;
    GPtrArray* style_buttons; /* the buttons toggling one style flag */
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    GString* css = g_string_new(".canvas-fmt-swatch { min-width: 14px; min-height: 14px; border-radius: 50%; }");
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
    return canvas_float_button(child, tip, cb, bar);
}

static GtkWidget* style_button(const gchar* icon, const gchar* tip, guint flag, CanvasFormatBar* bar) {
    GtkWidget* b = action_button(icon_image_new(icon), tip, G_CALLBACK(on_style_clicked), bar);
    g_object_set_data(G_OBJECT(b), "style-flag", GINT_TO_POINTER((gint)flag));
    g_ptr_array_add(bar->style_buttons, b);
    return b;
}

/* Menu button; its popover box is filled by the caller */
static GtkWidget* menu_button(GtkWidget* child, const gchar* tip, GtkWidget** pop_box, GtkOrientation o) {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(mb), child);
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(mb), FALSE);
    gtk_widget_add_css_class(mb, "canvas-fmt");
    gtk_widget_set_focus_on_click(mb, FALSE);
    gtk_widget_set_tooltip_text(mb, _(tip));
    GtkWidget* pop = gtk_popover_new();
    *pop_box = gtk_box_new(o, 2);
    gtk_popover_set_child(GTK_POPOVER(pop), *pop_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    return mb;
}

static GtkWidget* pop_item(GtkWidget* pop_box, const gchar* label, GCallback cb, CanvasFormatBar* bar) {
    GtkWidget* item = gtk_button_new_with_label(_(label));
    gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
    gtk_widget_add_css_class(item, "canvas-pop-item");
    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(item))), 0.0f);
    g_signal_connect(item, "clicked", cb, bar);
    gtk_box_append(GTK_BOX(pop_box), item);
    return item;
}

static void add_sep(GtkWidget* row) {
    canvas_float_add_separator(row);
}

static void build(CanvasFormatBar* bar, GtkWidget* row) {
    GtkWidget* pop_box;
    GtkWidget* presets = menu_button(icon_image_new("format-text-rich"), N_("Paragraph Style (Ctrl+Alt+1, 2, 0, Q)"),
                                     &pop_box, GTK_ORIENTATION_VERTICAL);
    static const struct {
        const gchar* name;
        CanvasPreset preset;
    } PRESETS[] = {
        {N_("Heading 1"), CANVAS_PRESET_H1},
        {N_("Heading 2"), CANVAS_PRESET_H2},
        {N_("Body Text"), CANVAS_PRESET_BODY},
        {N_("Quote"), CANVAS_PRESET_QUOTE},
    };
    for (guint i = 0; i < G_N_ELEMENTS(PRESETS); i++) {
        GtkWidget* item = pop_item(pop_box, PRESETS[i].name, G_CALLBACK(on_preset_clicked), bar);
        g_object_set_data(G_OBJECT(item), "preset", GINT_TO_POINTER((gint)PRESETS[i].preset));
    }
    gtk_box_append(GTK_BOX(row), presets);
    add_sep(row);
    gtk_box_append(GTK_BOX(row), style_button("format-text-bold", N_("Bold (Ctrl+B)"), CANVAS_STYLE_BOLD, bar));
    gtk_box_append(GTK_BOX(row), style_button("format-text-italic", N_("Italic (Ctrl+I)"), CANVAS_STYLE_ITALIC, bar));
    gtk_box_append(GTK_BOX(row),
                   style_button("format-text-underline", N_("Underline (Ctrl+U)"), CANVAS_STYLE_UNDERLINE, bar));
    gtk_box_append(GTK_BOX(row), style_button("format-text-strikethrough", N_("Strikethrough (Ctrl+Shift+S)"),
                                              CANVAS_STYLE_STRIKE, bar));

    GtkWidget* colors = menu_button(swatch_new("accent"), N_("Text Color"), &pop_box, GTK_ORIENTATION_HORIZONTAL);
    for (guint i = 0; i < G_N_ELEMENTS(TEXT_COLORS); i++) {
        GtkWidget* sw = action_button(swatch_new(TEXT_COLORS[i].color), TEXT_COLORS[i].tip,
                                      G_CALLBACK(on_style_clicked), bar);
        g_object_set_data(G_OBJECT(sw), "style-flag", GINT_TO_POINTER((gint)TEXT_COLORS[i].flag));
        gtk_box_append(GTK_BOX(pop_box), sw);
    }
    gtk_box_append(GTK_BOX(row), colors);

    GtkWidget* more = menu_button(icon_image_new("view-more"), N_("More Styles"), &pop_box, GTK_ORIENTATION_VERTICAL);
    static const struct {
        const gchar* name;
        guint flag;
    } MORE[] = {
        {N_("Highlight (Ctrl+H)"), CANVAS_STYLE_HIGHLIGHT},
        {N_("Code (Ctrl+E)"), CANVAS_STYLE_CODE},
        {N_("Serif Font"), CANVAS_STYLE_SERIF},
        {N_("Monospace Font"), CANVAS_STYLE_MONO},
        {N_("Small Size"), CANVAS_STYLE_SMALL},
        {N_("Large Size"), CANVAS_STYLE_LARGE},
    };
    for (guint i = 0; i < G_N_ELEMENTS(MORE); i++) {
        GtkWidget* item = pop_item(pop_box, MORE[i].name, G_CALLBACK(on_style_clicked), bar);
        g_object_set_data(G_OBJECT(item), "style-flag", GINT_TO_POINTER((gint)MORE[i].flag));
    }
    gtk_box_append(GTK_BOX(row), more);
    add_sep(row);

    GtkWidget* b = action_button(icon_image_new("view-list-bullet"), N_("Bullet List"), G_CALLBACK(on_list_clicked), bar);
    g_object_set_data(G_OBJECT(b), "list-prefix", (gpointer)"- ");
    gtk_box_append(GTK_BOX(row), b);
    b = action_button(icon_image_new("view-list-ordered"), N_("Numbered List"), G_CALLBACK(on_list_clicked), bar);
    g_object_set_data(G_OBJECT(b), "list-prefix", (gpointer)"1. ");
    gtk_box_append(GTK_BOX(row), b);
    gtk_box_append(GTK_BOX(row), action_button(icon_image_new("object-select"), N_("Task Checkbox (Ctrl+Space)"),
                                               G_CALLBACK(on_checkbox_clicked), bar));
    b = action_button(icon_image_new("format-indent-less"), N_("Decrease Indent"), G_CALLBACK(on_indent_clicked), bar);
    g_object_set_data(G_OBJECT(b), "indent-step", GINT_TO_POINTER(-1));
    gtk_box_append(GTK_BOX(row), b);
    b = action_button(icon_image_new("format-indent-more"), N_("Increase Indent"), G_CALLBACK(on_indent_clicked), bar);
    g_object_set_data(G_OBJECT(b), "indent-step", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(row), b);
}

void canvas_format_bar_show_for(CanvasFormatBar* bar, GtkWidget* target) {
    if (bar != NULL) canvas_float_show_for(bar->floating, target);
}

void canvas_format_bar_set_styles(CanvasFormatBar* bar, guint styles) {
    if (bar == NULL) return;
    for (guint i = 0; i < bar->style_buttons->len; i++) {
        GtkWidget* b = g_ptr_array_index(bar->style_buttons, i);
        guint flag = (guint)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "style-flag"));
        gboolean on = (styles & flag) != 0;
        if (on) {
            gtk_widget_set_state_flags(b, GTK_STATE_FLAG_CHECKED, FALSE);
        } else {
            gtk_widget_unset_state_flags(b, GTK_STATE_FLAG_CHECKED);
        }
        gtk_accessible_update_state(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_STATE_PRESSED,
                                    on ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE, -1);
    }
}

/* ------------------------------------------------------------------------- */

CanvasFormatBar* canvas_format_bar_new(const CanvasFormatBarCallbacks* cbs, gpointer user_data) {
    ensure_css();
    CanvasFormatBar* bar = g_new0(CanvasFormatBar, 1);
    if (cbs != NULL) bar->cbs = *cbs;
    bar->user_data = user_data;
    bar->style_buttons = g_ptr_array_new();
    bar->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    bar->floating = canvas_float_new(bar->box);
    build(bar, bar->box);
    return bar;
}

GtkWidget* canvas_format_bar_get_widget(CanvasFormatBar* bar) {
    return bar != NULL ? bar->box : NULL;
}

void canvas_format_bar_free(CanvasFormatBar* bar) {
    if (bar == NULL) return;
    canvas_float_free(bar->floating);
    g_ptr_array_unref(bar->style_buttons);
    g_free(bar);
}
