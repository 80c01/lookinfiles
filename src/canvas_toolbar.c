/* Tool bar of the Canvas page (see canvas_toolbar.h) */
#include "canvas_toolbar.h"
#include "icons.h"
#include "theme.h"

struct CanvasToolbar {
    GtkWidget* box;
    GtkWidget* tools[CANVAS_TOOL_COUNT];
    gboolean updating; /* set_tool: no on_tool call */
    CanvasToolbarCallbacks cbs;
    gpointer user_data;
};

static const struct {
    const gchar* icon;
    const gchar* tip;
    guint key;
} TOOLS[CANVAS_TOOL_COUNT] = {
    [CANVAS_TOOL_SELECT] = {"canvas-select", "Select and Move (V)", GDK_KEY_v},
    [CANVAS_TOOL_RECT] = {"canvas-rectangle", "Rectangle (R)", GDK_KEY_r},
    [CANVAS_TOOL_DIAMOND] = {"canvas-diamond", "Diamond (D)", GDK_KEY_d},
    [CANVAS_TOOL_ELLIPSE] = {"canvas-ellipse", "Ellipse (O)", GDK_KEY_o},
    [CANVAS_TOOL_ARROW] = {"canvas-arrow", "Arrow (A)", GDK_KEY_a},
    [CANVAS_TOOL_LINE] = {"canvas-line", "Line (L)", GDK_KEY_l},
    [CANVAS_TOOL_PENCIL] = {"document-edit", "Pencil (P)", GDK_KEY_p},
    [CANVAS_TOOL_TEXT] = {"insert-text", "Text Box (T)", GDK_KEY_t},
    [CANVAS_TOOL_TABLE] = {"canvas-table", "Table (B)", GDK_KEY_b},
    [CANVAS_TOOL_ERASER] = {"edit-clear", "Eraser (E)", GDK_KEY_e},
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-island { background-color: @bg_raised; border: 1px solid @border;"
                  " border-radius: 10px; padding: 4px; box-shadow: 0 2px 8px @shadow; }"
                  ".canvas-tool { min-width: 30px; min-height: 30px; padding: 0; border-radius: 7px;"
                  " background: none; border: none; box-shadow: none; color: @fg; }"
                  ".canvas-tool:hover { background-color: @bg_hover; }"
                  ".canvas-tool:checked { background-color: @accent; color: @accent_fg; }"
                  ".canvas-island-sep { min-width: 1px; background-color: @border; margin: 6px 4px; }"
                  ".canvas-pop-item { padding: 4px 8px; font-size: @size_small; }");
}

gint canvas_tool_for_key(guint keyval) {
    guint lower = gdk_keyval_to_lower(keyval);
    for (gint i = 0; i < CANVAS_TOOL_COUNT; i++) {
        if (TOOLS[i].key == lower) return i;
    }
    return -1;
}

gboolean canvas_tool_draws(CanvasTool tool) {
    return tool >= CANVAS_TOOL_RECT && tool <= CANVAS_TOOL_PENCIL;
}

CanvasShapeType canvas_tool_shape(CanvasTool tool) {
    switch (tool) {
    case CANVAS_TOOL_DIAMOND: return CANVAS_SHAPE_DIAMOND;
    case CANVAS_TOOL_ELLIPSE: return CANVAS_SHAPE_ELLIPSE;
    case CANVAS_TOOL_ARROW: return CANVAS_SHAPE_ARROW;
    case CANVAS_TOOL_LINE: return CANVAS_SHAPE_LINE;
    case CANVAS_TOOL_PENCIL: return CANVAS_SHAPE_FREEHAND;
    default: return CANVAS_SHAPE_RECT;
    }
}

static void on_tool_toggled(GtkToggleButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    if (!gtk_toggle_button_get_active(btn) || tb->updating) return;
    CanvasTool tool = (CanvasTool)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "tool"));
    if (tb->cbs.on_tool) tb->cbs.on_tool(tool, tb->user_data);
}

void canvas_toolbar_set_tool(CanvasToolbar* tb, CanvasTool tool) {
    if (tb == NULL || tool >= CANVAS_TOOL_COUNT) return;
    tb->updating = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tb->tools[tool]), TRUE);
    tb->updating = FALSE;
}

static void on_grid_toggled(GtkCheckButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    if (tb->cbs.on_toggle_grid) tb->cbs.on_toggle_grid(gtk_check_button_get_active(btn), tb->user_data);
}

static void on_crop_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    if (tb->cbs.on_crop_image) tb->cbs.on_crop_image(tb->user_data);
}

static void on_align_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    CanvasAlignOp op = (CanvasAlignOp)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "align-op"));
    if (tb->cbs.on_align) tb->cbs.on_align(op, tb->user_data);
}

static void on_order_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    gint where = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "order"));
    if (tb->cbs.on_order) tb->cbs.on_order(where, tb->user_data);
}

static void on_lock_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    if (tb->cbs.on_lock) tb->cbs.on_lock(tb->user_data);
}

static void on_export_clicked(GtkButton* btn, gpointer data) {
    CanvasToolbar* tb = data;
    GtkPopover* pop = g_object_get_data(G_OBJECT(btn), "pop");
    if (pop != NULL) gtk_popover_popdown(pop);
    gboolean svg = g_object_get_data(G_OBJECT(btn), "svg") != NULL;
    if (tb->cbs.on_export) tb->cbs.on_export(svg, tb->user_data);
}

static GtkWidget* menu_item(const gchar* label, GtkWidget* pop, GCallback cb, CanvasToolbar* tb) {
    GtkWidget* item = gtk_button_new_with_label(label);
    gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
    gtk_widget_add_css_class(item, "canvas-pop-item");
    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(item))), 0.0f);
    g_object_set_data(G_OBJECT(item), "pop", pop);
    g_signal_connect(item, "clicked", cb, tb);
    return item;
}

/* The menu at the end of the bar: snap to grid, align, crop */
static GtkWidget* more_menu(CanvasToolbar* tb) {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(mb), icon_image_new("view-more"));
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(mb), FALSE);
    gtk_widget_add_css_class(mb, "canvas-tool");
    gtk_widget_set_focus_on_click(mb, FALSE);
    gtk_widget_set_tooltip_text(mb, "Grid, Align, Order, Lock, Crop and Export");

    GtkWidget* pop = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    GtkWidget* grid = gtk_check_button_new_with_label("Snap to Grid");
    gtk_widget_add_css_class(grid, "canvas-pop-item");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(grid), TRUE);
    g_signal_connect(grid, "toggled", G_CALLBACK(on_grid_toggled), tb);
    gtk_box_append(GTK_BOX(box), grid);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    static const struct {
        const gchar* name;
        CanvasAlignOp op;
    } ALIGNS[] = {
        {"Align Left", CANVAS_ALIGN_LEFT},
        {"Align Centers", CANVAS_ALIGN_CENTER_H},
        {"Align Right", CANVAS_ALIGN_RIGHT},
        {"Align Top", CANVAS_ALIGN_TOP},
        {"Align Middles", CANVAS_ALIGN_MIDDLE_V},
        {"Align Bottom", CANVAS_ALIGN_BOTTOM},
        {"Distribute Horizontally", CANVAS_DISTRIBUTE_H},
        {"Distribute Vertically", CANVAS_DISTRIBUTE_V},
    };
    for (guint i = 0; i < G_N_ELEMENTS(ALIGNS); i++) {
        GtkWidget* item = menu_item(ALIGNS[i].name, pop, G_CALLBACK(on_align_clicked), tb);
        g_object_set_data(G_OBJECT(item), "align-op", GINT_TO_POINTER((gint)ALIGNS[i].op));
        gtk_box_append(GTK_BOX(box), item);
    }
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    static const struct {
        const gchar* name;
        gint where;
    } ORDERS[] = {
        {"Bring to Front (Ctrl+Shift+])", 2},
        {"Bring Forward (Ctrl+])", 1},
        {"Send Backward (Ctrl+[)", -1},
        {"Send to Back (Ctrl+Shift+[)", -2},
    };
    for (guint i = 0; i < G_N_ELEMENTS(ORDERS); i++) {
        GtkWidget* item = menu_item(ORDERS[i].name, pop, G_CALLBACK(on_order_clicked), tb);
        g_object_set_data(G_OBJECT(item), "order", GINT_TO_POINTER(ORDERS[i].where));
        gtk_box_append(GTK_BOX(box), item);
    }
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item("Lock / Unlock (Ctrl+Shift+K)", pop, G_CALLBACK(on_lock_clicked), tb));
    gtk_box_append(GTK_BOX(box), menu_item("Crop Selected Picture", pop, G_CALLBACK(on_crop_clicked), tb));

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item("Export as PNG", pop, G_CALLBACK(on_export_clicked), tb));
    GtkWidget* svg = menu_item("Export as SVG", pop, G_CALLBACK(on_export_clicked), tb);
    g_object_set_data(G_OBJECT(svg), "svg", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(box), svg);

    gtk_popover_set_child(GTK_POPOVER(pop), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    return mb;
}

static void add_sep(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(sep, "canvas-island-sep");
    gtk_box_append(GTK_BOX(box), sep);
}

CanvasToolbar* canvas_toolbar_new(const CanvasToolbarCallbacks* cbs, gpointer user_data) {
    ensure_css();
    CanvasToolbar* tb = g_new0(CanvasToolbar, 1);
    if (cbs != NULL) tb->cbs = *cbs;
    tb->user_data = user_data;

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(box, "canvas-island");
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    tb->box = box;

    for (gint i = 0; i < CANVAS_TOOL_COUNT; i++) {
        /* Groups: select | shapes and pencil | text, table | eraser */
        if (i == CANVAS_TOOL_RECT || i == CANVAS_TOOL_TEXT || i == CANVAS_TOOL_ERASER) add_sep(box);
        GtkWidget* b = gtk_toggle_button_new();
        icon_button_set_content(b, TOOLS[i].icon, NULL);
        gtk_widget_add_css_class(b, "canvas-tool");
        gtk_widget_set_focus_on_click(b, FALSE);
        gtk_widget_set_tooltip_text(b, TOOLS[i].tip);
        if (i > 0) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(tb->tools[0]));
        g_object_set_data(G_OBJECT(b), "tool", GINT_TO_POINTER(i));
        g_signal_connect(b, "toggled", G_CALLBACK(on_tool_toggled), tb);
        tb->tools[i] = b;
        gtk_box_append(GTK_BOX(box), b);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tb->tools[CANVAS_TOOL_SELECT]), TRUE);
    add_sep(box);
    gtk_box_append(GTK_BOX(box), more_menu(tb));
    return tb;
}

GtkWidget* canvas_toolbar_get_widget(CanvasToolbar* tb) {
    return tb != NULL ? tb->box : NULL;
}

void canvas_toolbar_free(CanvasToolbar* tb) {
    g_free(tb);
}
