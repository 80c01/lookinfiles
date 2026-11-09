/* Properties panel of the Canvas page (see canvas_props.h) */
#include "canvas/canvas_props.h"
#include "canvas/canvas_rough_draw.h"
#include "theme/icons.h"
#include "core/i18n.h"
#include "theme/palette.h"
#include "theme/theme.h"

#include <string.h>

#define PREVIEW_W 30
#define PREVIEW_H 22

/* Stroke colours: palette roles */
static const struct {
    const gchar* name;
    const gchar* tip;
} COLORS[] = {
    {"fg", N_("Default")},
    {"fg_subtle", N_("Grey")},
    {"accent", N_("Accent")},
    {"error", N_("Red")},
    {"success", N_("Green")},
    {"warning", N_("Yellow")},
    {"icon_purple", N_("Purple")},
};

static const struct {
    CanvasFillStyle fill;
    const gchar* tip;
} FILLS[] = {
    {CANVAS_FILL_NONE, N_("No Fill")},
    {CANVAS_FILL_SKETCH, N_("Hatched Fill")},
    {CANVAS_FILL_SOLID, N_("Solid Fill")},
};

static const struct {
    gint width;
    const gchar* tip;
} WIDTHS[] = {
    {1, NC_("stroke width", "Thin")},
    {2, NC_("stroke width", "Bold")},
    {4, NC_("stroke width", "Extra Bold")},
};

/* Align and distribute buttons: icon (lookin-<icon>), tooltip */
static const struct {
    CanvasAlignOp op;
    const gchar* icon;
    const gchar* tip;
} ALIGNS[] = {
    {CANVAS_ALIGN_LEFT, "canvas-align-left", N_("Align Left")},
    {CANVAS_ALIGN_CENTER_H, "canvas-align-center-h", N_("Align Centers")},
    {CANVAS_ALIGN_RIGHT, "canvas-align-right", N_("Align Right")},
    {CANVAS_ALIGN_TOP, "canvas-align-top", N_("Align Top")},
    {CANVAS_ALIGN_MIDDLE_V, "canvas-align-middle-v", N_("Align Middles")},
    {CANVAS_ALIGN_BOTTOM, "canvas-align-bottom", N_("Align Bottom")},
};

static const struct {
    CanvasAlignOp op;
    const gchar* icon;
    const gchar* tip;
} DISTRIBUTES[] = {
    {CANVAS_DISTRIBUTE_H, "canvas-distribute-h", N_("Distribute Horizontally")},
    {CANVAS_DISTRIBUTE_V, "canvas-distribute-v", N_("Distribute Vertically")},
};

/* Drawing order buttons: 2 front, 1 forward, -1 backward, -2 back */
static const struct {
    gint where;
    const gchar* icon;
    const gchar* tip;
} ORDERS[] = {
    {2, "canvas-bring-front", N_("Bring to Front (Ctrl+Shift+])")},
    {1, "canvas-bring-forward", N_("Bring Forward (Ctrl+])")},
    {-1, "canvas-send-backward", N_("Send Backward (Ctrl+[)")},
    {-2, "canvas-send-back", N_("Send to Back (Ctrl+Shift+[)")},
};

struct CanvasProps {
    GtkWidget* box;
    GtkWidget* shape_box;
    GtkWidget* selection_box;
    GtkWidget* align_box;
    GtkWidget* lock;
    GtkWidget* group;
    GtkWidget* ungroup;
    GtkWidget* crop;
    GtkWidget* distributes[G_N_ELEMENTS(DISTRIBUTES)];
    GtkWidget* fill_row;
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
        ".canvas-prop:disabled { opacity: 0.4; }"
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
    GtkWidget* l = gtk_label_new(_(text));
    gtk_widget_add_css_class(l, "canvas-props-title");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    return l;
}

static GtkWidget* row_new(void) {
    return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
}

static GtkWidget* shape_section(CanvasProps* props) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gtk_box_append(GTK_BOX(box), section_title(N_("Stroke")));
    GtkWidget* row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(COLORS); i++) {
        props->colors[i] = choice_new(props, "color", (gint)i, swatch_new(COLORS[i].name), _(COLORS[i].tip),
                                      i > 0 ? props->colors[0] : NULL);
        gtk_box_append(GTK_BOX(row), props->colors[i]);
    }
    gtk_box_append(GTK_BOX(box), row);

    props->fill_row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(props->fill_row), section_title(N_("Fill")));
    row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(FILLS); i++) {
        props->fills[i] = choice_new(props, "fill", (gint)i, preview_new(PREVIEW_FILL, FILLS[i].fill), _(FILLS[i].tip),
                                     i > 0 ? props->fills[0] : NULL);
        gtk_box_append(GTK_BOX(row), props->fills[i]);
    }
    gtk_box_append(GTK_BOX(props->fill_row), row);
    gtk_box_append(GTK_BOX(box), props->fill_row);

    gtk_box_append(GTK_BOX(box), section_title(N_("Stroke Width")));
    row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(WIDTHS); i++) {
        props->widths[i] = choice_new(props, "width", (gint)i, preview_new(PREVIEW_WIDTH, WIDTHS[i].width),
                                      g_dpgettext2(NULL, "stroke width", WIDTHS[i].tip), i > 0 ? props->widths[0] : NULL);
        gtk_box_append(GTK_BOX(row), props->widths[i]);
    }
    gtk_box_append(GTK_BOX(box), row);

    gtk_box_append(GTK_BOX(box), section_title(N_("Style")));
    row = row_new();
    props->styles[0] = choice_new(props, "style", 0, preview_new(PREVIEW_STYLE, 1), _("Hand-Drawn"), NULL);
    props->styles[1] = choice_new(props, "style", 1, preview_new(PREVIEW_STYLE, 0), _("Clean"), props->styles[0]);
    gtk_box_append(GTK_BOX(row), props->styles[0]);
    gtk_box_append(GTK_BOX(row), props->styles[1]);
    gtk_box_append(GTK_BOX(box), row);
    return box;
}

/* The panel shows while one of its sections does */
static void update_visible(CanvasProps* props) {
    gtk_widget_set_visible(props->box, gtk_widget_get_visible(props->shape_box) ||
                                           gtk_widget_get_visible(props->selection_box));
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
    gtk_widget_set_visible(props->shape_box, TRUE);
    update_visible(props);
}

void canvas_props_hide_shape(CanvasProps* props) {
    if (props == NULL) return;
    gtk_widget_set_visible(props->shape_box, FALSE);
    update_visible(props);
}

/* ------------------------------------------------------------------------- */
/* Selection section: align, layers, lock, group, crop                       */
/* ------------------------------------------------------------------------- */

static void on_align_clicked(GtkButton* btn, gpointer data) {
    CanvasProps* props = data;
    CanvasAlignOp op = (CanvasAlignOp)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "value"));
    if (props->cbs.on_align) props->cbs.on_align(op, props->user_data);
}

static void on_order_clicked(GtkButton* btn, gpointer data) {
    CanvasProps* props = data;
    gint where = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "value"));
    if (props->cbs.on_order) props->cbs.on_order(where, props->user_data);
}

static void on_lock_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasProps* props = data;
    if (props->cbs.on_lock) props->cbs.on_lock(props->user_data);
}

static void on_group_clicked(GtkButton* btn, gpointer data) {
    CanvasProps* props = data;
    gboolean group = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "value")) != 0;
    if (props->cbs.on_group) props->cbs.on_group(group, props->user_data);
}

static void on_crop_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasProps* props = data;
    if (props->cbs.on_crop) props->cbs.on_crop(props->user_data);
}

/* Icon button (lookin-<icon>); value: what it applies, given to cb */
static GtkWidget* action_button(CanvasProps* props, const gchar* icon, const gchar* tip, gint value, GCallback cb) {
    GtkWidget* b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), icon_image_new(icon));
    gtk_widget_add_css_class(b, "canvas-prop");
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_focusable(b, FALSE);
    gtk_widget_set_tooltip_text(b, _(tip));
    g_object_set_data(G_OBJECT(b), "value", GINT_TO_POINTER(value));
    g_signal_connect(b, "clicked", cb, props);
    return b;
}

static GtkWidget* align_section(CanvasProps* props) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(box), section_title(N_("Align")));
    /* Horizontal ones, then vertical ones */
    for (guint row_start = 0; row_start < G_N_ELEMENTS(ALIGNS); row_start += 3) {
        GtkWidget* row = row_new();
        for (guint i = row_start; i < row_start + 3; i++) {
            gtk_box_append(GTK_BOX(row), action_button(props, ALIGNS[i].icon, ALIGNS[i].tip, (gint)ALIGNS[i].op,
                                                       G_CALLBACK(on_align_clicked)));
        }
        gtk_box_append(GTK_BOX(box), row);
    }
    gtk_box_append(GTK_BOX(box), section_title(N_("Distribute")));
    GtkWidget* row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(DISTRIBUTES); i++) {
        props->distributes[i] = action_button(props, DISTRIBUTES[i].icon, DISTRIBUTES[i].tip,
                                              (gint)DISTRIBUTES[i].op, G_CALLBACK(on_align_clicked));
        gtk_box_append(GTK_BOX(row), props->distributes[i]);
    }
    gtk_box_append(GTK_BOX(box), row);
    gtk_widget_set_visible(box, FALSE);
    return box;
}

static GtkWidget* selection_section(CanvasProps* props) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    props->align_box = align_section(props);
    gtk_box_append(GTK_BOX(box), props->align_box);

    gtk_box_append(GTK_BOX(box), section_title(N_("Layers")));
    GtkWidget* row = row_new();
    for (guint i = 0; i < G_N_ELEMENTS(ORDERS); i++) {
        gtk_box_append(GTK_BOX(row), action_button(props, ORDERS[i].icon, ORDERS[i].tip, ORDERS[i].where,
                                                   G_CALLBACK(on_order_clicked)));
    }
    gtk_box_append(GTK_BOX(box), row);

    gtk_box_append(GTK_BOX(box), section_title(N_("Actions")));
    row = row_new();
    props->lock = action_button(props, "canvas-lock", N_("Lock (Ctrl+Shift+K)"), 0, G_CALLBACK(on_lock_clicked));
    gtk_box_append(GTK_BOX(row), props->lock);
    props->group = action_button(props, "canvas-group", N_("Group (Ctrl+G)"), 1, G_CALLBACK(on_group_clicked));
    gtk_box_append(GTK_BOX(row), props->group);
    props->ungroup = action_button(props, "canvas-ungroup", N_("Ungroup (Ctrl+Shift+G)"), 0,
                                   G_CALLBACK(on_group_clicked));
    gtk_box_append(GTK_BOX(row), props->ungroup);
    props->crop = action_button(props, "canvas-crop", N_("Crop Picture"), 0, G_CALLBACK(on_crop_clicked));
    gtk_box_append(GTK_BOX(row), props->crop);
    gtk_box_append(GTK_BOX(box), row);
    gtk_widget_set_visible(box, FALSE);
    return box;
}

void canvas_props_show_selection(CanvasProps* props, const CanvasPropsSelection* sel) {
    if (props == NULL) return;
    gboolean shown = sel != NULL && sel->selected > 0;
    if (shown) {
        for (guint i = 0; i < G_N_ELEMENTS(DISTRIBUTES); i++) {
            gtk_widget_set_sensitive(props->distributes[i], sel->movable >= 3);
        }
        gtk_widget_set_visible(props->align_box, sel->movable >= 2);
        gtk_button_set_child(GTK_BUTTON(props->lock), icon_image_new(sel->locked ? "canvas-unlock" : "canvas-lock"));
        gtk_widget_set_tooltip_text(props->lock, sel->locked ? _("Unlock (Ctrl+Shift+K)") : _("Lock (Ctrl+Shift+K)"));
        gtk_widget_set_visible(props->group, sel->groupable);
        gtk_widget_set_visible(props->ungroup, sel->grouped);
        gtk_widget_set_visible(props->crop, sel->picture);
    }
    gtk_widget_set_visible(props->selection_box, shown);
    update_visible(props);
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
    gtk_widget_set_halign(props->box, GTK_ALIGN_END);
    gtk_widget_set_valign(props->box, GTK_ALIGN_START);
    props->shape_box = shape_section(props);
    gtk_box_append(GTK_BOX(props->box), props->shape_box);
    props->selection_box = selection_section(props);
    gtk_box_append(GTK_BOX(props->box), props->selection_box);
    gtk_widget_set_visible(props->box, FALSE);
    return props;
}

GtkWidget* canvas_props_get_widget(CanvasProps* props) {
    return props != NULL ? props->box : NULL;
}

void canvas_props_free(CanvasProps* props) {
    g_free(props);
}
