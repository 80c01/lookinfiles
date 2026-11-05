#include "canvas_toolbar.h"
#include "theme.h"

struct CanvasToolbar {
    GtkWidget* box;
    CanvasToolbarCallbacks cbs;
    gpointer user_data;
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-bar { background-color: @bg_bar; padding: 4px 8px; border-bottom: 1px solid @border; }"
                  ".canvas-bar-sep { min-width: 1px; background-color: @border; margin: 2px 4px; }"
                  ".canvas-btn { min-height: 24px; min-width: 24px; padding: 2px 6px; font-size: @size_small; }"
                  ".canvas-pop-item { padding: 4px 8px; font-size: @size_small; }");
}

static void on_text_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasToolbar* tb = data;
    if (tb->cbs.on_add_text) tb->cbs.on_add_text(tb->user_data);
}

static void on_table_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasToolbar* tb = data;
    if (tb->cbs.on_add_table) tb->cbs.on_add_table(3, 3, tb->user_data);
}

static void on_crop_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasToolbar* tb = data;
    if (tb->cbs.on_crop_image) tb->cbs.on_crop_image(tb->user_data);
}

static void on_shape_item_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    CanvasShapeType st = (CanvasShapeType)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "shape-type"));
    if (tb->cbs.on_add_shape) tb->cbs.on_add_shape(st, tb->user_data);
}

static void on_preset_item_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    CanvasPreset p = (CanvasPreset)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "preset"));
    if (tb->cbs.on_format_preset) tb->cbs.on_format_preset(p, tb->user_data);
}

static void on_style_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    guint s = (guint)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "style-flag"));
    if (tb->cbs.on_format_style) tb->cbs.on_format_style(s, tb->user_data);
}

static void on_style_menu_item_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    guint s = (guint)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "style-flag"));
    if (tb->cbs.on_format_style) tb->cbs.on_format_style(s, tb->user_data);
}

static void on_list_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    const gchar* pfx = g_object_get_data(G_OBJECT(btn), "list-prefix");
    if (tb->cbs.on_format_list) tb->cbs.on_format_list(pfx, tb->user_data);
}

static void on_checkbox_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    CanvasToolbar* tb = data;
    if (tb->cbs.on_format_checkbox) tb->cbs.on_format_checkbox(tb->user_data);
}

static void on_indent_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    gint step = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "indent-step"));
    if (tb->cbs.on_format_indent) tb->cbs.on_format_indent(step, tb->user_data);
}

static void on_grid_toggled(GtkToggleButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    gboolean active = gtk_toggle_button_get_active(btn);
    if (tb->cbs.on_toggle_grid) tb->cbs.on_toggle_grid(active, tb->user_data);
}

static void on_align_item_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    CanvasAlignOp op = (CanvasAlignOp)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "align-op"));
    if (tb->cbs.on_align) tb->cbs.on_align(op, tb->user_data);
}

static void add_sep(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(sep, "canvas-bar-sep");
    gtk_box_append(GTK_BOX(box), sep);
}

static GtkWidget* make_btn(const gchar* label, const gchar* tip, GCallback cb, gpointer data) {
    GtkWidget* b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "canvas-btn");
    gtk_widget_set_focus_on_click(b, FALSE); /* the focus stays in the block */
    if (tip != NULL) gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

static GtkWidget* make_style_btn(const gchar* label, const gchar* tip, guint style_flag, CanvasToolbar* tb) {
    GtkWidget* b = make_btn(label, tip, G_CALLBACK(on_style_clicked), tb);
    g_object_set_data(G_OBJECT(b), "style-flag", GINT_TO_POINTER((gint)style_flag));
    return b;
}

CanvasToolbar* canvas_toolbar_new(const CanvasToolbarCallbacks* cbs, gpointer user_data) {
    ensure_css();
    CanvasToolbar* tb = g_new0(CanvasToolbar, 1);
    if (cbs != NULL) tb->cbs = *cbs;
    tb->user_data = user_data;

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(root, "canvas-bar");
    tb->box = root;

    /* Group: Insert */
    GtkWidget* grp_insert = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(grp_insert, "canvas-bar-group");
    gtk_box_append(GTK_BOX(grp_insert), make_btn("+ Text", "Insert Text Box (Ctrl+Enter)", G_CALLBACK(on_text_clicked), tb));

    /* Shape Menu Button */
    GtkWidget* shape_btn = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(shape_btn), "+ Shape ▼");
    gtk_widget_add_css_class(shape_btn, "canvas-btn");
    gtk_widget_set_focus_on_click(shape_btn, FALSE);
    gtk_widget_set_tooltip_text(shape_btn, "Draw Shapes (Rectangle, Ellipse, Diamond, Arrow, Line)");

    GtkWidget* shape_pop = gtk_popover_new();
    GtkWidget* shape_menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    const struct { const gchar* name; CanvasShapeType type; } SHAPES[] = {
        {"Rectangle", CANVAS_SHAPE_RECT},
        {"Ellipse", CANVAS_SHAPE_ELLIPSE},
        {"Diamond", CANVAS_SHAPE_DIAMOND},
        {"Arrow", CANVAS_SHAPE_ARROW},
        {"Line", CANVAS_SHAPE_LINE}
    };
    for (guint i = 0; i < G_N_ELEMENTS(SHAPES); i++) {
        GtkWidget* item = gtk_button_new_with_label(SHAPES[i].name);
        gtk_widget_add_css_class(item, "canvas-pop-item");
        g_object_set_data(G_OBJECT(item), "shape-type", GINT_TO_POINTER((gint)SHAPES[i].type));
        g_object_set_data(G_OBJECT(item), "pop", shape_pop);
        g_signal_connect(item, "clicked", G_CALLBACK(on_shape_item_clicked), tb);
        gtk_box_append(GTK_BOX(shape_menu_box), item);
    }
    gtk_popover_set_child(GTK_POPOVER(shape_pop), shape_menu_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(shape_btn), shape_pop);
    gtk_box_append(GTK_BOX(grp_insert), shape_btn);

    /* Table button */
    gtk_box_append(GTK_BOX(grp_insert), make_btn("+ Table", "Insert Table (3x3)", G_CALLBACK(on_table_clicked), tb));
    /* Crop button */
    gtk_box_append(GTK_BOX(grp_insert), make_btn("Crop", "Crop Selected Image", G_CALLBACK(on_crop_clicked), tb));
    gtk_box_append(GTK_BOX(root), grp_insert);

    add_sep(root);

    /* Group: Style Presets */
    GtkWidget* preset_btn = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(preset_btn), "Style ▼");
    gtk_widget_add_css_class(preset_btn, "canvas-btn");
    gtk_widget_set_focus_on_click(preset_btn, FALSE);
    gtk_widget_set_tooltip_text(preset_btn, "Text Presets (Heading 1, Heading 2, Body, Quote)");

    GtkWidget* preset_pop = gtk_popover_new();
    GtkWidget* preset_menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    const struct { const gchar* name; CanvasPreset p; } PRESETS[] = {
        {"Heading 1", CANVAS_PRESET_H1},
        {"Heading 2", CANVAS_PRESET_H2},
        {"Body Text", CANVAS_PRESET_BODY},
        {"Quote / Note", CANVAS_PRESET_QUOTE}
    };
    for (guint i = 0; i < G_N_ELEMENTS(PRESETS); i++) {
        GtkWidget* item = gtk_button_new_with_label(PRESETS[i].name);
        gtk_widget_add_css_class(item, "canvas-pop-item");
        g_object_set_data(G_OBJECT(item), "preset", GINT_TO_POINTER((gint)PRESETS[i].p));
        g_object_set_data(G_OBJECT(item), "pop", preset_pop);
        g_signal_connect(item, "clicked", G_CALLBACK(on_preset_item_clicked), tb);
        gtk_box_append(GTK_BOX(preset_menu_box), item);
    }
    gtk_popover_set_child(GTK_POPOVER(preset_pop), preset_menu_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(preset_btn), preset_pop);
    gtk_box_append(GTK_BOX(root), preset_btn);

    /* Group: Basic formatting */
    GtkWidget* grp_format = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(grp_format, "canvas-bar-group");
    gtk_box_append(GTK_BOX(grp_format), make_style_btn("B", "Bold (Ctrl+B)", CANVAS_STYLE_BOLD, tb));
    gtk_box_append(GTK_BOX(grp_format), make_style_btn("I", "Italic (Ctrl+I)", CANVAS_STYLE_ITALIC, tb));
    gtk_box_append(GTK_BOX(grp_format), make_style_btn("U", "Underline (Ctrl+U)", CANVAS_STYLE_UNDERLINE, tb));
    gtk_box_append(GTK_BOX(grp_format), make_style_btn("S", "Strikethrough (Ctrl+Shift+S)", CANVAS_STYLE_STRIKE, tb));
    gtk_box_append(GTK_BOX(grp_format), make_style_btn("Mark", "Highlight (Yellow)", CANVAS_STYLE_HIGHLIGHT, tb));
    gtk_box_append(GTK_BOX(grp_format), make_style_btn("`", "Code (Ctrl+E)", CANVAS_STYLE_CODE, tb));
    gtk_box_append(GTK_BOX(root), grp_format);

    /* Group: Font, Size & Color Popover */
    GtkWidget* font_color_btn = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(font_color_btn), "Font/Color ▼");
    gtk_widget_add_css_class(font_color_btn, "canvas-btn");
    gtk_widget_set_focus_on_click(font_color_btn, FALSE);

    GtkWidget* fc_pop = gtk_popover_new();
    GtkWidget* fc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    const struct { const gchar* label; guint flag; } FC_ITEMS[] = {
        {"Font: Serif", CANVAS_STYLE_SERIF},
        {"Font: Monospace", CANVAS_STYLE_MONO},
        {"Size: Small", CANVAS_STYLE_SMALL},
        {"Size: Large", CANVAS_STYLE_LARGE},
        {"Color: Accent", CANVAS_STYLE_ACCENT},
        {"Color: Warning", CANVAS_STYLE_WARNING},
        {"Color: Danger", CANVAS_STYLE_DANGER},
        {"Color: Success", CANVAS_STYLE_SUCCESS},
        {"Color: Subtle", CANVAS_STYLE_MUTED}
    };
    for (guint i = 0; i < G_N_ELEMENTS(FC_ITEMS); i++) {
        GtkWidget* item = gtk_button_new_with_label(FC_ITEMS[i].label);
        gtk_widget_add_css_class(item, "canvas-pop-item");
        g_object_set_data(G_OBJECT(item), "style-flag", GINT_TO_POINTER((gint)FC_ITEMS[i].flag));
        g_object_set_data(G_OBJECT(item), "pop", fc_pop);
        g_signal_connect(item, "clicked", G_CALLBACK(on_style_menu_item_clicked), tb);
        gtk_box_append(GTK_BOX(fc_box), item);
    }
    gtk_popover_set_child(GTK_POPOVER(fc_pop), fc_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(font_color_btn), fc_pop);
    gtk_box_append(GTK_BOX(root), font_color_btn);

    add_sep(root);

    /* Group: Lists & Indent */
    GtkWidget* grp_lists = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(grp_lists, "canvas-bar-group");

    GtkWidget* b_bullet = make_btn("•", "Bullet List", G_CALLBACK(on_list_clicked), tb);
    g_object_set_data(G_OBJECT(b_bullet), "list-prefix", (gpointer)"- ");
    gtk_box_append(GTK_BOX(grp_lists), b_bullet);

    GtkWidget* b_num = make_btn("1.", "Numbered List", G_CALLBACK(on_list_clicked), tb);
    g_object_set_data(G_OBJECT(b_num), "list-prefix", (gpointer)"1. ");
    gtk_box_append(GTK_BOX(grp_lists), b_num);

    GtkWidget* b_check = make_btn("☑", "Checkbox / Task (Toggle)", G_CALLBACK(on_checkbox_clicked), tb);
    gtk_box_append(GTK_BOX(grp_lists), b_check);

    GtkWidget* b_unindent = make_btn("◀", "Decrease Margin", G_CALLBACK(on_indent_clicked), tb);
    g_object_set_data(G_OBJECT(b_unindent), "indent-step", GINT_TO_POINTER(-1));
    gtk_box_append(GTK_BOX(grp_lists), b_unindent);

    GtkWidget* b_indent = make_btn("▶", "Increase Margin", G_CALLBACK(on_indent_clicked), tb);
    g_object_set_data(G_OBJECT(b_indent), "indent-step", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(grp_lists), b_indent);

    gtk_box_append(GTK_BOX(root), grp_lists);

    add_sep(root);

    /* Group: Canvas Arranging */
    GtkWidget* grp_arrange = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(grp_arrange, "canvas-bar-group");

    GtkWidget* toggle_grid = gtk_toggle_button_new_with_label("Grid");
    gtk_widget_add_css_class(toggle_grid, "canvas-btn");
    gtk_widget_set_focus_on_click(toggle_grid, FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_grid), TRUE);
    gtk_widget_set_tooltip_text(toggle_grid, "Toggle Magnetic Snap Grid");
    g_signal_connect(toggle_grid, "toggled", G_CALLBACK(on_grid_toggled), tb);
    gtk_box_append(GTK_BOX(grp_arrange), toggle_grid);

    /* Align Popover */
    GtkWidget* align_btn = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(align_btn), "Align ▼");
    gtk_widget_add_css_class(align_btn, "canvas-btn");
    gtk_widget_set_focus_on_click(align_btn, FALSE);
    gtk_widget_set_tooltip_text(align_btn, "Align & Distribute Objects");

    GtkWidget* align_pop = gtk_popover_new();
    GtkWidget* align_menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    const struct { const gchar* name; CanvasAlignOp op; } ALIGNS[] = {
        {"Align Left", CANVAS_ALIGN_LEFT},
        {"Align Center H", CANVAS_ALIGN_CENTER_H},
        {"Align Right", CANVAS_ALIGN_RIGHT},
        {"Align Top", CANVAS_ALIGN_TOP},
        {"Align Middle V", CANVAS_ALIGN_MIDDLE_V},
        {"Align Bottom", CANVAS_ALIGN_BOTTOM},
        {"Distribute Horizontally", CANVAS_DISTRIBUTE_H},
        {"Distribute Vertically", CANVAS_DISTRIBUTE_V}
    };
    for (guint i = 0; i < G_N_ELEMENTS(ALIGNS); i++) {
        GtkWidget* item = gtk_button_new_with_label(ALIGNS[i].name);
        gtk_widget_add_css_class(item, "canvas-pop-item");
        g_object_set_data(G_OBJECT(item), "align-op", GINT_TO_POINTER((gint)ALIGNS[i].op));
        g_object_set_data(G_OBJECT(item), "pop", align_pop);
        g_signal_connect(item, "clicked", G_CALLBACK(on_align_item_clicked), tb);
        gtk_box_append(GTK_BOX(align_menu_box), item);
    }
    gtk_popover_set_child(GTK_POPOVER(align_pop), align_menu_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(align_btn), align_pop);
    gtk_box_append(GTK_BOX(grp_arrange), align_btn);

    gtk_box_append(GTK_BOX(root), grp_arrange);

    return tb;
}

GtkWidget* canvas_toolbar_get_widget(CanvasToolbar* tb) {
    return tb != NULL ? tb->box : NULL;
}

void canvas_toolbar_free(CanvasToolbar* tb) {
    g_free(tb);
}
