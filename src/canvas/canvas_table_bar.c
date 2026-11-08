/* Table bar of the Canvas page (see canvas_table_bar.h) */
#include "canvas/canvas_table_bar.h"
#include "canvas/canvas_float.h"
#include "icons.h"
#include "core/i18n.h"

#define ACTION_KEY "canvas-table-action"

struct CanvasTableBar {
    GtkWidget* box;
    GtkWidget* del_row;
    GtkWidget* del_col;
    CanvasFloat* floating;
    CanvasTableBarAction on_action;
    gpointer user_data;
};

static void on_clicked(GtkButton* btn, gpointer data) {
    CanvasTableBar* bar = data;
    CanvasTableAction action = (CanvasTableAction)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), ACTION_KEY));
    if (bar->on_action != NULL) bar->on_action(action, bar->user_data);
}

static GtkWidget* add_button(CanvasTableBar* bar, const gchar* icon, const gchar* tip, CanvasTableAction action) {
    GtkWidget* b = canvas_float_button(icon_image_new(icon), tip, G_CALLBACK(on_clicked), bar);
    g_object_set_data(G_OBJECT(b), ACTION_KEY, GINT_TO_POINTER((gint)action));
    gtk_box_append(GTK_BOX(bar->box), b);
    return b;
}

CanvasTableBar* canvas_table_bar_new(CanvasTableBarAction on_action, gpointer user_data) {
    CanvasTableBar* bar = g_new0(CanvasTableBar, 1);
    bar->on_action = on_action;
    bar->user_data = user_data;
    bar->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    bar->floating = canvas_float_new(bar->box);
    add_button(bar, "canvas-row-add-above", N_("Insert Row Above"), CANVAS_TABLE_ROW_ABOVE);
    add_button(bar, "canvas-row-add-below", N_("Insert Row Below"), CANVAS_TABLE_ROW_BELOW);
    bar->del_row = add_button(bar, "canvas-row-delete", N_("Delete Row"), CANVAS_TABLE_ROW_DELETE);
    canvas_float_add_separator(bar->box);
    add_button(bar, "canvas-column-add-left", N_("Insert Column Left"), CANVAS_TABLE_COL_LEFT);
    add_button(bar, "canvas-column-add-right", N_("Insert Column Right"), CANVAS_TABLE_COL_RIGHT);
    bar->del_col = add_button(bar, "canvas-column-delete", N_("Delete Column"), CANVAS_TABLE_COL_DELETE);
    return bar;
}

GtkWidget* canvas_table_bar_get_widget(CanvasTableBar* bar) {
    return bar != NULL ? bar->box : NULL;
}

void canvas_table_bar_show_for(CanvasTableBar* bar, GtkWidget* target, GtkWidget* table) {
    if (bar == NULL) return;
    if (target != NULL && table != NULL) {
        gtk_widget_set_sensitive(bar->del_row, canvas_table_widget_can(table, CANVAS_TABLE_ROW_DELETE));
        gtk_widget_set_sensitive(bar->del_col, canvas_table_widget_can(table, CANVAS_TABLE_COL_DELETE));
    }
    canvas_float_show_for(bar->floating, table != NULL ? target : NULL);
}

void canvas_table_bar_free(CanvasTableBar* bar) {
    if (bar == NULL) return;
    canvas_float_free(bar->floating);
    g_free(bar);
}
