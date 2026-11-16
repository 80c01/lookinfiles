/* The menus of a Jupyter notebook tab's cells (see ipynb_tab_private.h):
 * the cell menu, on a right click outside the text being edited (the run,
 * clipboard, insert, move and delete commands of the cell, or of the
 * selected cells it is in), and the extra entry of the source's own text
 * menu (Split Cell, which takes the cursor) */
#include "core/i18n.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_tab_private.h"

#define MENU_INDEX_KEY "lookin-ipynb-menu-cell"
#define MENU_POPOVER_KEY "lookin-ipynb-menu-popover"
#define MENU_ACTION_KEY "lookin-ipynb-menu-action"

typedef void (*CellAction)(IpynbTab* t, guint index);

static guint menu_index(gpointer object) {
    return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(object), MENU_INDEX_KEY));
}

/* --- The cell menu --- */

static void run_cells(IpynbTab* t, guint index) {
    ipynb_run_range(t, index);
}

static void insert_above(IpynbTab* t, guint index) {
    ipynb_select_clear(t);
    ipynb_tab_insert_cell(t, index, IPYNB_CELL_CODE);
}

static void insert_below(IpynbTab* t, guint index) {
    ipynb_select_clear(t);
    ipynb_tab_insert_cell(t, index + 1, IPYNB_CELL_CODE);
}

static void move_up(IpynbTab* t, guint index) {
    ipynb_select_move(t, index, -1);
}

static void move_down(IpynbTab* t, guint index) {
    ipynb_select_move(t, index, 1);
}

static void on_item_clicked(GtkButton* button, gpointer data) {
    IpynbTab* t = data;
    GtkWidget* popover = g_object_get_data(G_OBJECT(button), MENU_POPOVER_KEY);
    CellAction run = (CellAction)g_object_get_data(G_OBJECT(button), MENU_ACTION_KEY);
    guint index = menu_index(button);
    gtk_popover_popdown(GTK_POPOVER(popover));
    if (!t->closed && run != NULL) run(t, index);
}

/* An entry: its label, then its keys in a badge, as in the editor tab menu */
static void add_item(IpynbTab* t, GtkWidget* box, GtkWidget* popover, guint index, const gchar* label,
                     const gchar* keys, CellAction run) {
    GtkWidget* button = gtk_button_new();
    gtk_widget_set_name(button, "tab-context-btn");
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* text = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_append(GTK_BOX(row), text);
    if (keys != NULL) {
        GtkWidget* badge = gtk_label_new(keys);
        gtk_widget_add_css_class(badge, "shortcut-badge");
        gtk_box_append(GTK_BOX(row), badge);
    }
    gtk_button_set_child(GTK_BUTTON(button), row);
    g_object_set_data(G_OBJECT(button), MENU_INDEX_KEY, GUINT_TO_POINTER(index));
    g_object_set_data(G_OBJECT(button), MENU_POPOVER_KEY, popover);
    g_object_set_data(G_OBJECT(button), MENU_ACTION_KEY, (gpointer)run);
    g_signal_connect(button, "clicked", G_CALLBACK(on_item_clicked), t);
    gtk_box_append(GTK_BOX(box), button);
}

static void add_separator(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 4);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(box), sep);
}

/* The cell menu of the cell at index, at x, y of widget. The popover hangs
 * on the tab's root: the cells (and widget) are built again by most of its
 * commands. */
static void show_cell_menu(IpynbTab* t, guint index, GtkWidget* widget, gdouble x, gdouble y) {
    graphene_point_t at;
    if (!gtk_widget_compute_point(widget, t->root, &GRAPHENE_POINT_INIT((float)x, (float)y), &at)) return;
    gboolean many = ipynb_select_count_at(t, index) > 1;
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, t->root);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    /* Beside the pointer: a menu taller than the room above and below it
     * slides up or down to stay on the screen (above or below, it would
     * not show at all) */
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_RIGHT);
    GdkRectangle rect = { (gint)at.x, (gint)at.y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    add_item(t, box, popover, index, many ? _("Run Cells") : _("Run Cell"), "Ctrl+Enter", run_cells);
    add_item(t, box, popover, index, _("Run This Cell and the Ones Below"), NULL, ipynb_run_from);
    add_separator(box);
    add_item(t, box, popover, index, many ? _("Copy Cells") : _("Copy Cell"), "Ctrl+Shift+C", ipynb_select_copy);
    add_item(t, box, popover, index, many ? _("Cut Cells") : _("Cut Cell"), "Ctrl+Shift+X", ipynb_select_cut);
    add_item(t, box, popover, index, _("Paste Cells Below"), "Ctrl+Shift+V", ipynb_select_paste);
    add_item(t, box, popover, index, many ? _("Merge Cells") : _("Merge with the Next Cell"), "Ctrl+Shift+M",
             ipynb_select_merge);
    add_separator(box);
    add_item(t, box, popover, index, _("Insert Code Cell Above"), "Alt+Shift+Enter", insert_above);
    add_item(t, box, popover, index, _("Insert Code Cell Below"), "Alt+Enter", insert_below);
    add_item(t, box, popover, index, many ? _("Move Cells Up") : _("Move Cell Up"), "Alt+Up", move_up);
    add_item(t, box, popover, index, many ? _("Move Cells Down") : _("Move Cell Down"), "Alt+Down", move_down);
    add_separator(box);
    add_item(t, box, popover, index, many ? _("Delete Cells") : _("Delete Cell"), "Alt+Delete",
             ipynb_select_delete);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    IpynbTab* t = data;
    if (t->closed) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    show_cell_menu(t, menu_index(widget), widget, x, y);
}

void ipynb_menu_attach(IpynbTab* t, guint index, GtkWidget* widget) {
    g_object_set_data(G_OBJECT(widget), MENU_INDEX_KEY, GUINT_TO_POINTER(index));
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_right_click), t);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));
}

/* --- The text menu of the source --- */

#define MENU_TAB_KEY "lookin-ipynb-menu-tab"

/* Split Cell: at the cursor, or around the selection, as in JupyterLab */
static void on_split_cell(GSimpleAction* action, GVariant* param, gpointer data) {
    (void)action;
    (void)param;
    IpynbTab* t = g_object_get_data(G_OBJECT(data), MENU_TAB_KEY);
    guint index = menu_index(data);
    if (t == NULL || t->closed) return;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data));
    GtkTextIter start, end;
    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    glong from = gtk_text_iter_get_offset(&start);
    glong to = gtk_text_iter_get_offset(&end);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gchar** parts = ipynb_split_source(text, from, to);
    g_free(text);
    if (parts == NULL) {
        ipynb_tab_set_status(t, _("Nothing to split here"), FALSE);
        return;
    }
    /* Every part shows the same: the split ends the edit of a Markdown cell
     * (the parts inserted above are rendered, the cell kept is the last) */
    g_hash_table_remove(t->editing, GUINT_TO_POINTER(ipynb_doc_cell_key(t->doc, index)));
    index = ipynb_doc_split_cell(t->doc, index, parts);
    g_strfreev(parts);
    ipynb_tab_mark_changed(t);
    ipynb_tab_queue_rebuild(t, index);
}

void ipynb_menu_add_text(IpynbTab* t, guint index, GtkWidget* view) {
    g_object_set_data(G_OBJECT(view), MENU_TAB_KEY, t);
    g_object_set_data(G_OBJECT(view), MENU_INDEX_KEY, GUINT_TO_POINTER(index));
    GSimpleActionGroup* group = g_simple_action_group_new();
    GSimpleAction* split = g_simple_action_new("split", NULL);
    g_signal_connect(split, "activate", G_CALLBACK(on_split_cell), view);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(split));
    g_object_unref(split);
    gtk_widget_insert_action_group(view, "cell", G_ACTION_GROUP(group));
    g_object_unref(group);
    GMenu* menu = g_menu_new();
    g_menu_append(menu, _("Split Cell"), "cell.split");
    gtk_text_view_set_extra_menu(GTK_TEXT_VIEW(view), G_MENU_MODEL(menu));
    g_object_unref(menu);
}
