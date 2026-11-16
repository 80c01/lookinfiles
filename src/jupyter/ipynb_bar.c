/* The bar at the top of a Jupyter notebook tab (see ipynb_tab_private.h):
 * run buttons, new cells and Undo Delete on the left, cut on the right in
 * a narrow pane; the kernel menu, the headings button and the menu of the
 * rarer actions (Clear Outputs, Edit as JSON) always in sight at the right
 * end. */
#include "core/i18n.h"
#include "editor/editor.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_tab_private.h"
#include "theme/icons.h"

static void on_add_code(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    ipynb_tab_insert_cell(t, t->current + 1, IPYNB_CELL_CODE);
}

static void on_add_markdown(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    ipynb_tab_insert_cell(t, t->current + 1, IPYNB_CELL_MARKDOWN);
}

static void on_undo_delete(GtkButton* button, gpointer data) {
    (void)button;
    ipynb_select_undo_delete(data);
}

static void on_clear_outputs(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    if (!ipynb_doc_clear_outputs(t->doc, G_MAXUINT)) return;
    ipynb_tab_mark_changed(t);
    ipynb_tab_rebuild_cells(t);
}

static void on_edit_json(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    if (t->path == NULL && !ipynb_save_as(t)) return; /* a file to open first */
    ipynb_tab_save_now(t);
    editor_open_text_file(t->app, t->name, t->path);
}

static void on_tagbar(GtkButton* button, gpointer data) {
    (void)button;
    ipynb_tagbar_toggle(data);
}

/* An icon button, with a text after the icon unless NULL */
static GtkWidget* bar_button(IpynbTab* t, const gchar* icon, const gchar* text, const gchar* tooltip, GCallback cb) {
    GtkWidget* b = icon_button_new(icon, tooltip);
    if (text != NULL) icon_button_set_content(b, icon, text);
    g_signal_connect(b, "clicked", cb, t);
    return b;
}

/* --- The menu of the rarer actions --- */

/* Connected before the action: the menu closes first */
static void on_item_clicked(GtkButton* item, gpointer data) {
    (void)data;
    GtkWidget* popover = gtk_widget_get_ancestor(GTK_WIDGET(item), GTK_TYPE_POPOVER);
    if (popover != NULL) gtk_popover_popdown(GTK_POPOVER(popover));
}

static void add_item(GtkWidget* menu, IpynbTab* t, const gchar* text, const gchar* tooltip, GCallback cb) {
    GtkWidget* item = gtk_button_new_with_label(text);
    gtk_widget_add_css_class(item, "flat");
    gtk_widget_set_tooltip_text(item, tooltip);
    GtkWidget* label = gtk_button_get_child(GTK_BUTTON(item));
    if (GTK_IS_LABEL(label)) gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    g_signal_connect(item, "clicked", G_CALLBACK(on_item_clicked), NULL);
    g_signal_connect(item, "clicked", cb, t);
    gtk_box_append(GTK_BOX(menu), item);
}

static GtkWidget* more_menu_new(IpynbTab* t) {
    GtkWidget* button = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(button), icon_image_new("view-more"));
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(button), FALSE);
    gtk_widget_set_tooltip_text(button, _("More Actions"));
    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    GtkWidget* menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    add_item(menu, t, _("Clear Outputs"), _("Clear the Outputs of Every Cell"), G_CALLBACK(on_clear_outputs));
    add_item(menu, t, _("Edit as JSON"), _("Open the File as Text in an Editor Tab"), G_CALLBACK(on_edit_json));
    gtk_popover_set_child(GTK_POPOVER(popover), menu);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    return button;
}

GtkWidget* ipynb_bar_new(IpynbTab* t) {
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget* kernel_button = ipynb_run_bar(t, actions);
    gtk_box_append(GTK_BOX(actions), bar_button(t, "cell-code", NULL, _("Insert Code Cell Below (Alt+Enter)"),
                                                G_CALLBACK(on_add_code)));
    gtk_box_append(GTK_BOX(actions), bar_button(t, "cell-markdown", NULL, _("Insert Markdown Cell Below"),
                                                G_CALLBACK(on_add_markdown)));
    t->undo_button = bar_button(t, "edit-undo", _("Undo Delete"), _("Put Back the Cells Deleted Last"),
                                G_CALLBACK(on_undo_delete));
    gtk_widget_set_visible(t->undo_button, FALSE);
    gtk_box_append(GTK_BOX(actions), t->undo_button);
    /* In a narrow pane only the actions are cut on the right, instead of
     * widening the tab past the pane: the kernel menu, the headings button
     * and the menu stay in sight at the right end */
    GtkWidget* actions_clip = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(actions_clip), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(actions_clip), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(actions_clip), actions);
    gtk_widget_set_hexpand(actions_clip, TRUE);
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(bar, "ipynb-bar");
    gtk_box_append(GTK_BOX(bar), actions_clip);
    gtk_box_append(GTK_BOX(bar), kernel_button);
    GtkWidget* tagbar = icon_button_new("view-list-bullet", _("Show / Hide the Headings (Ctrl+K Ctrl+O)"));
    g_signal_connect(tagbar, "clicked", G_CALLBACK(on_tagbar), t);
    gtk_box_append(GTK_BOX(bar), tagbar);
    gtk_box_append(GTK_BOX(bar), more_menu_new(t));
    return bar;
}
