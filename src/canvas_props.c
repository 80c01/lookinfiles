/* Properties panel of the Canvas page (see canvas_props.h) */
#include "canvas_props.h"
#include "canvas_rough_draw.h"
#include "icons.h"
#include "palette.h"
#include "theme.h"

#include <string.h>

#define PREVIEW_W 30
#define PREVIEW_H 22

/* Stroke colours: palette roles */
static const struct {
    const gchar* name;
    const gchar* tip;
} COLORS[] = {
    {"fg", "Default"},
    {"fg_subtle", "Grey"},
    {"accent", "Accent"},
    {"error", "Red"},
    {"success", "Green"},
    {"warning", "Yellow"},
    {"icon_purple", "Purple"},
};

static const struct {
    CanvasFillStyle fill;
    const gchar* tip;
} FILLS[] = {
    {CANVAS_FILL_NONE, "No Fill"},
    {CANVAS_FILL_SKETCH, "Hatched Fill"},
    {CANVAS_FILL_SOLID, "Solid Fill"},
};

static const struct {
    gint width;
    const gchar* tip;
} WIDTHS[] = {
    {1, "Thin"},
    {2, "Bold"},
    {4, "Extra Bold"},
};

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

struct CanvasProps {
    GtkWidget* box;
    GtkWidget* shape_box;
    GtkWidget* fill_row;
    GtkWidget* text_box;
    GtkWidget* colors[G_N_ELEMENTS(COLORS)];
    GtkWidget* fills[G_N_ELEMENTS(FILLS)];
    GtkWidget* widths[G_N_ELEMENTS(WIDTHS)];
    GtkWidget* styles[2]; /* hand-drawn, clean */
    CanvasShapeStyle style;
    gboolean updating;    /* showing a style: no callback */
    CanvasPropsCallbacks cbs;
    gpointer user_data;
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    GString* css = g_string_new(
        ".canvas-props { background-color: @bg_raised; border: 1px solid @border; border-radius: 10px;"
        " padding: 8px 10px 10px 10px; box-shadow: 0 2px 8px @shadow; }"
        ".canvas-props-title { color: @fg_subtle; font-size: @size_small; margin-top: 6px; }"
        ".canvas-prop { min-width: 28px; min-height: 26px; padding: 2px; border-radius: 6px;"
        " background: none; border: none; box-shadow: none; color: @fg; }"
        ".canvas-prop:hover { background-color: @bg_hover; }"
        ".canvas-prop:checked { background-color: @bg_selected; box-shadow: inset 0 0 0 1px @accent; }"
        ".canvas-swatch { min-width: 16px; min-height: 16px; border-radius: 50%; }");
    for (guint i = 0; i < G_N_ELEMENTS(COLORS); i++) {
        g_string_append_printf(css, ".canvas-c-%s { background-color: @%s; }", COLORS[i].name, COLORS[i].name);
    }
    theme_add_css(css->str);
    g_string_free(css, TRUE);
}

void canvas_props_default_style(CanvasShapeStyle* style) {
    style->stroke_color = "fg";
    style->fill = CANVAS_FILL_NONE;
    style->stroke_width = 2;
    style->rough = TRUE;
}

const gchar* canvas_props_color_name(const gchar* name) {
    if (g_strcmp0(name, "danger") == 0) return "error";
    if (g_strcmp0(name, "muted") == 0) return "fg_subtle";
    for (guint i = 0; i < G_N_ELEMENTS(COLORS); i++) {
        if (g_strcmp0(name, COLORS[i].name) == 0) return COLORS[i].name;
    }
    return "fg";
}

/* ------------------------------------------------------------------------- */
/* Previews of the fill, width and style choices                             */
/* ------------------------------------------------------------------------- */

typedef enum { PREVIEW_FILL, PREVIEW_WIDTH, PREVIEW_STYLE } PreviewKind;

static void on_preview_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)data;
    PreviewKind kind = (PreviewKind)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "kind"));
    gint value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "value"));
    if (kind == PREVIEW_WIDTH) {
        double r, g, b;
        palette_rgb(PAL_FG, &r, &g, &b);
        cairo_set_source_rgb(cr, r, g, b);
        cairo_set_line_width(cr, value);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, 6, height / 2.0);
        cairo_line_to(cr, width - 6, height / 2.0);
        cairo_stroke(cr);
        return;
    }
    CanvasBlock b = {0};
    b.kind = CANVAS_BLOCK_SHAPE;
    b.shape_type = CANVAS_SHAPE_RECT;
    b.stroke_color = (gchar*)"fg";
    b.stroke_width = 1;
    b.fill_style = kind == PREVIEW_FILL ? (CanvasFillStyle)value : CANVAS_FILL_NONE;
    b.rough = kind == PREVIEW_STYLE && value;
    canvas_rough_draw_shape(cr, &b, width, height);
}

static GtkWidget* preview_new(PreviewKind kind, gint value) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, PREVIEW_W, PREVIEW_H);
    g_object_set_data(G_OBJECT(area), "kind", GINT_TO_POINTER(kind));
    g_object_set_data(G_OBJECT(area), "value", GINT_TO_POINTER(value));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_preview_draw, NULL, NULL);
    return area;
}

/* ------------------------------------------------------------------------- */
/* Shape section                                                             */
/* ------------------------------------------------------------------------- */

static void emit_style(CanvasProps* props) {
    if (!props->updating && props->cbs.on_shape_style) props->cbs.on_shape_style(&props->style, props->user_data);
}

static void on_choice_toggled(GtkToggleButton* btn, gpointer data) {
    CanvasProps* props = data;
    if (!gtk_toggle_button_get_active(btn) || props->updating) return;
    const gchar* what = g_object_get_data(G_OBJECT(btn), "what");
    gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "index"));
    if (strcmp(what, "color") == 0) {
        props->style.stroke_color = COLORS[index].name;
    } else if (strcmp(what, "fill") == 0) {
        props->style.fill = FILLS[index].fill;
    } else if (strcmp(what, "width") == 0) {
        props->style.stroke_width = WIDTHS[index].width;
    } else {
        props->style.rough = index == 0;
    }
    emit_style(props);
}

static GtkWidget* choice_new(CanvasProps* props, const gchar* what, gint index, GtkWidget* child,
                             const gchar* tip, GtkWidget* group) {
    GtkWidget* b = gtk_toggle_button_new();
    gtk_button_set_child(GTK_BUTTON(b), child);
    gtk_widget_add_css_class(b, "canvas-prop");
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_focusable(b, FALSE);
    gtk_widget_set_tooltip_text(b, tip);
    if (group != NULL) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(group));
    g_object_set_data(G_OBJECT(b), "what", (gpointer)what);
    g_object_set_data(G_OBJECT(b), "index", GINT_TO_POINTER(index));
    g_signal_connect(b, "toggled", G_CALLBACK(on_choice_toggled), props);
    return b;
}

static GtkWidget* swatch_new(const gchar* color) {
    GtkWidget* dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(dot, "canvas-swatch");
    gchar* cls = g_strconcat("canvas-c-", color, NULL);
    gtk_widget_add_css_class(dot, cls);
    g_free(cls);
    gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    return dot;
}

static GtkWidget* section_title(const gchar* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "canvas-props-title");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    return l;
}

static GtkWidget* row_new(void) {
    return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
}

static GtkWidget* shape_section(CanvasProps* props) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gtk_box_append(GTK_BOX(box), section_title("Stroke"));
    GtkWidget* row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(COLORS); i++) {
        props->colors[i] = choice_new(props, "color", (gint)i, swatch_new(COLORS[i].name), COLORS[i].tip,
                                      i > 0 ? props->colors[0] : NULL);
        gtk_box_append(GTK_BOX(row), props->colors[i]);
    }
    gtk_box_append(GTK_BOX(box), row);

    props->fill_row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(props->fill_row), section_title("Fill"));
    row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(FILLS); i++) {
        props->fills[i] = choice_new(props, "fill", (gint)i, preview_new(PREVIEW_FILL, FILLS[i].fill), FILLS[i].tip,
                                     i > 0 ? props->fills[0] : NULL);
        gtk_box_append(GTK_BOX(row), props->fills[i]);
    }
    gtk_box_append(GTK_BOX(props->fill_row), row);
    gtk_box_append(GTK_BOX(box), props->fill_row);

    gtk_box_append(GTK_BOX(box), section_title("Stroke Width"));
    row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(WIDTHS); i++) {
        props->widths[i] = choice_new(props, "width", (gint)i, preview_new(PREVIEW_WIDTH, WIDTHS[i].width),
                                      WIDTHS[i].tip, i > 0 ? props->widths[0] : NULL);
        gtk_box_append(GTK_BOX(row), props->widths[i]);
    }
    gtk_box_append(GTK_BOX(box), row);

    gtk_box_append(GTK_BOX(box), section_title("Style"));
    row = row_new();
    props->styles[0] = choice_new(props, "style", 0, preview_new(PREVIEW_STYLE, 1), "Hand-Drawn", NULL);
    props->styles[1] = choice_new(props, "style", 1, preview_new(PREVIEW_STYLE, 0), "Clean", props->styles[0]);
    gtk_box_append(GTK_BOX(row), props->styles[0]);
    gtk_box_append(GTK_BOX(row), props->styles[1]);
    gtk_box_append(GTK_BOX(box), row);
    return box;
}

void canvas_props_show_shape(CanvasProps* props, const CanvasShapeStyle* style, gboolean with_fill) {
    if (props == NULL || style == NULL) return;
    props->updating = TRUE;
    props->style = *style;
    props->style.stroke_color = canvas_props_color_name(style->stroke_color);
    for (guint i = 0; i < G_N_ELEMENTS(COLORS); i++) {
        if (strcmp(COLORS[i].name, props->style.stroke_color) == 0) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(props->colors[i]), TRUE);
        }
    }
    for (guint i = 0; i < G_N_ELEMENTS(FILLS); i++) {
        if (FILLS[i].fill == style->fill) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(props->fills[i]), TRUE);
    }
    /* The nearest width (notes may hold others) */
    guint best = 0;
    for (guint i = 1; i < G_N_ELEMENTS(WIDTHS); i++) {
        if (ABS(WIDTHS[i].width - style->stroke_width) < ABS(WIDTHS[best].width - style->stroke_width)) best = i;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(props->widths[best]), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(props->styles[style->rough ? 0 : 1]), TRUE);
    props->updating = FALSE;

    gtk_widget_set_visible(props->fill_row, with_fill);
    gtk_widget_set_visible(props->text_box, FALSE);
    gtk_widget_set_visible(props->shape_box, TRUE);
    gtk_widget_set_visible(props->box, TRUE);
}

/* ------------------------------------------------------------------------- */
/* Text section                                                              */
/* ------------------------------------------------------------------------- */

static GtkWidget* action_button(GtkWidget* child, const gchar* tip, GCallback cb, CanvasProps* props) {
    GtkWidget* b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), child);
    gtk_widget_add_css_class(b, "canvas-prop");
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_focusable(b, FALSE);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, props);
    return b;
}

static void on_style_clicked(GtkButton* btn, gpointer data) {
    CanvasProps* props = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    guint flag = (guint)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "style-flag"));
    if (props->cbs.on_format_style) props->cbs.on_format_style(flag, props->user_data);
}

static void on_preset_clicked(GtkButton* btn, gpointer data) {
    CanvasProps* props = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    CanvasPreset preset = (CanvasPreset)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "preset"));
    if (props->cbs.on_format_preset) props->cbs.on_format_preset(preset, props->user_data);
}

static void on_list_clicked(GtkButton* btn, gpointer data) {
    CanvasProps* props = data;
    const gchar* prefix = g_object_get_data(G_OBJECT(btn), "list-prefix");
    if (props->cbs.on_format_list) props->cbs.on_format_list(prefix, props->user_data);
}

static void on_checkbox_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasProps* props = data;
    if (props->cbs.on_format_checkbox) props->cbs.on_format_checkbox(props->user_data);
}

static void on_indent_clicked(GtkButton* btn, gpointer data) {
    CanvasProps* props = data;
    gint step = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "indent-step"));
    if (props->cbs.on_format_indent) props->cbs.on_format_indent(step, props->user_data);
}

static GtkWidget* style_button(const gchar* icon, const gchar* tip, guint flag, CanvasProps* props) {
    GtkWidget* b = action_button(icon_image_new(icon), tip, G_CALLBACK(on_style_clicked), props);
    g_object_set_data(G_OBJECT(b), "style-flag", GINT_TO_POINTER((gint)flag));
    return b;
}

/* Menu button with an icon; its popover lists text items (filled by the caller) */
static GtkWidget* menu_button(const gchar* icon, const gchar* tip, GtkWidget** pop_box) {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(mb), icon_image_new(icon));
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(mb), FALSE);
    gtk_widget_add_css_class(mb, "canvas-prop");
    gtk_widget_set_focus_on_click(mb, FALSE);
    gtk_widget_set_tooltip_text(mb, tip);
    GtkWidget* pop = gtk_popover_new();
    *pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_popover_set_child(GTK_POPOVER(pop), *pop_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    return mb;
}

static GtkWidget* pop_item(GtkWidget* pop_box, const gchar* label, GCallback cb, CanvasProps* props) {
    GtkWidget* item = gtk_button_new_with_label(label);
    gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
    gtk_widget_add_css_class(item, "canvas-pop-item");
    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(item))), 0.0f);
    g_object_set_data(G_OBJECT(item), "pop", gtk_widget_get_ancestor(pop_box, GTK_TYPE_POPOVER));
    g_signal_connect(item, "clicked", cb, props);
    gtk_box_append(GTK_BOX(pop_box), item);
    return item;
}

static GtkWidget* text_section(CanvasProps* props) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gtk_box_append(GTK_BOX(box), section_title("Text"));
    GtkWidget* row = row_new();
    GtkWidget* pop_box;
    GtkWidget* presets = menu_button("format-text-rich", "Paragraph Style (Ctrl+Alt+1, 2, 0, Q)", &pop_box);
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
        GtkWidget* item = pop_item(pop_box, PRESETS[i].name, G_CALLBACK(on_preset_clicked), props);
        g_object_set_data(G_OBJECT(item), "preset", GINT_TO_POINTER((gint)PRESETS[i].preset));
    }
    gtk_box_append(GTK_BOX(row), presets);
    gtk_box_append(GTK_BOX(row), style_button("format-text-bold", "Bold (Ctrl+B)", CANVAS_STYLE_BOLD, props));
    gtk_box_append(GTK_BOX(row), style_button("format-text-italic", "Italic (Ctrl+I)", CANVAS_STYLE_ITALIC, props));
    gtk_box_append(GTK_BOX(row),
                   style_button("format-text-underline", "Underline (Ctrl+U)", CANVAS_STYLE_UNDERLINE, props));
    gtk_box_append(GTK_BOX(row), style_button("format-text-strikethrough", "Strikethrough (Ctrl+Shift+S)",
                                              CANVAS_STYLE_STRIKE, props));

    GtkWidget* more = menu_button("view-more", "More Styles", &pop_box);
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
        GtkWidget* item = pop_item(pop_box, MORE[i].name, G_CALLBACK(on_style_clicked), props);
        g_object_set_data(G_OBJECT(item), "style-flag", GINT_TO_POINTER((gint)MORE[i].flag));
    }
    gtk_box_append(GTK_BOX(row), more);
    gtk_box_append(GTK_BOX(box), row);

    gtk_box_append(GTK_BOX(box), section_title("Color"));
    row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(TEXT_COLORS); i++) {
        GtkWidget* sw = action_button(swatch_new(TEXT_COLORS[i].color), TEXT_COLORS[i].tip,
                                      G_CALLBACK(on_style_clicked), props);
        g_object_set_data(G_OBJECT(sw), "style-flag", GINT_TO_POINTER((gint)TEXT_COLORS[i].flag));
        gtk_box_append(GTK_BOX(row), sw);
    }
    gtk_box_append(GTK_BOX(box), row);

    gtk_box_append(GTK_BOX(box), section_title("Lists"));
    row = row_new();
    GtkWidget* b = action_button(icon_image_new("view-list-bullet"), "Bullet List", G_CALLBACK(on_list_clicked), props);
    g_object_set_data(G_OBJECT(b), "list-prefix", (gpointer)"- ");
    gtk_box_append(GTK_BOX(row), b);
    b = action_button(icon_image_new("view-list-ordered"), "Numbered List", G_CALLBACK(on_list_clicked), props);
    g_object_set_data(G_OBJECT(b), "list-prefix", (gpointer)"1. ");
    gtk_box_append(GTK_BOX(row), b);
    gtk_box_append(GTK_BOX(row), action_button(icon_image_new("object-select"), "Task Checkbox",
                                               G_CALLBACK(on_checkbox_clicked), props));
    b = action_button(icon_image_new("format-indent-less"), "Decrease Indent", G_CALLBACK(on_indent_clicked), props);
    g_object_set_data(G_OBJECT(b), "indent-step", GINT_TO_POINTER(-1));
    gtk_box_append(GTK_BOX(row), b);
    b = action_button(icon_image_new("format-indent-more"), "Increase Indent", G_CALLBACK(on_indent_clicked), props);
    g_object_set_data(G_OBJECT(b), "indent-step", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(row), b);
    gtk_box_append(GTK_BOX(box), row);
    return box;
}

void canvas_props_show_text(CanvasProps* props) {
    if (props == NULL) return;
    gtk_widget_set_visible(props->shape_box, FALSE);
    gtk_widget_set_visible(props->text_box, TRUE);
    gtk_widget_set_visible(props->box, TRUE);
}

void canvas_props_hide(CanvasProps* props) {
    if (props != NULL) gtk_widget_set_visible(props->box, FALSE);
}

/* ------------------------------------------------------------------------- */

CanvasProps* canvas_props_new(const CanvasPropsCallbacks* cbs, gpointer user_data) {
    ensure_css();
    CanvasProps* props = g_new0(CanvasProps, 1);
    if (cbs != NULL) props->cbs = *cbs;
    props->user_data = user_data;
    canvas_props_default_style(&props->style);

    props->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(props->box, "canvas-props");
    gtk_widget_set_halign(props->box, GTK_ALIGN_START);
    gtk_widget_set_valign(props->box, GTK_ALIGN_START);
    props->shape_box = shape_section(props);
    props->text_box = text_section(props);
    gtk_box_append(GTK_BOX(props->box), props->shape_box);
    gtk_box_append(GTK_BOX(props->box), props->text_box);
    gtk_widget_set_visible(props->box, FALSE);
    return props;
}

GtkWidget* canvas_props_get_widget(CanvasProps* props) {
    return props != NULL ? props->box : NULL;
}

void canvas_props_free(CanvasProps* props) {
    g_free(props);
}
