#include "canvas_table.h"
#include "icons.h"
#include "theme.h"

#define TABLE_STATE_KEY "canvas-table-state"

typedef struct {
    CanvasBlock* block;
    CanvasTableChangeNotify on_change;
    gpointer user_data;
    GtkWidget* box;
    GtkWidget* grid;
    gboolean rebuilding;
} CanvasTableState;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-table-box { background-color: @bg_view; border-radius: 4px; }"
                  ".canvas-table-bar { padding: 2px 4px; gap: 4px; border-bottom: 1px solid @border; }"
                  ".canvas-table-btn { min-width: 20px; min-height: 20px; padding: 1px 4px; font-size: @size_small; }"
                  ".canvas-table-grid { border: 1px solid @border; border-radius: 2px; }"
                  ".canvas-table-cell { background-color: transparent; color: @fg_text; border: 1px solid @rule;"
                  " border-radius: 0; min-height: 24px; padding: 2px 6px; font-family: @font_ui; }"
                  ".canvas-table-cell:focus { border-color: @accent; background-color: @bg_block; }"
                  ".canvas-table-header { font-weight: bold; background-color: @bg_block; }");
}

static void on_cell_changed(GtkEditable* editable, gpointer user_data) {
    CanvasTableState* st = g_object_get_data(G_OBJECT(editable), TABLE_STATE_KEY);
    if (st == NULL || st->rebuilding) return;

    gint row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(editable), "row"));
    gint col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(editable), "col"));
    const gchar* text = gtk_editable_get_text(editable);
    canvas_table_set_cell(st->block, row, col, text);
    if (st->on_change != NULL) st->on_change(st->block, st->user_data);
    (void)user_data;
}

void canvas_table_widget_rebuild(GtkWidget* table_widget) {
    CanvasTableState* st = g_object_get_data(G_OBJECT(table_widget), TABLE_STATE_KEY);
    if (st == NULL) return;

    st->rebuilding = TRUE;

    /* Remove old grid child and make a new one */
    if (st->grid != NULL) {
        gtk_box_remove(GTK_BOX(st->box), st->grid);
    }
    st->grid = gtk_grid_new();
    gtk_widget_add_css_class(st->grid, "canvas-table-grid");
    gtk_grid_set_row_spacing(GTK_GRID(st->grid), 0);
    gtk_grid_set_column_spacing(GTK_GRID(st->grid), 0);
    gtk_box_append(GTK_BOX(st->box), st->grid);

    for (gint r = 0; r < st->block->table_rows; r++) {
        for (gint c = 0; c < st->block->table_cols; c++) {
            GtkWidget* entry = gtk_entry_new();
            gtk_widget_add_css_class(entry, "canvas-table-cell");
            if (r == 0) gtk_widget_add_css_class(entry, "canvas-table-header");
            gint col_w = canvas_table_get_col_width(st->block, c);
            gtk_widget_set_size_request(entry, MAX(col_w, 60), 28);
            gtk_widget_set_hexpand(entry, TRUE); /* widened by the resize corner */
            const gchar* txt = canvas_table_get_cell(st->block, r, c);
            gtk_editable_set_text(GTK_EDITABLE(entry), txt != NULL ? txt : "");

            g_object_set_data(G_OBJECT(entry), TABLE_STATE_KEY, st);
            g_object_set_data(G_OBJECT(entry), "row", GINT_TO_POINTER(r));
            g_object_set_data(G_OBJECT(entry), "col", GINT_TO_POINTER(c));
            g_signal_connect(entry, "changed", G_CALLBACK(on_cell_changed), st);

            gtk_grid_attach(GTK_GRID(st->grid), entry, c, r, 1, 1);
        }
    }

    st->rebuilding = FALSE;
}

static void on_add_row(GtkButton* btn, gpointer user_data) {
    (void)btn;
    CanvasTableState* st = user_data;
    canvas_table_insert_row(st->block, st->block->table_rows);
    canvas_table_widget_rebuild(st->box);
    if (st->on_change != NULL) st->on_change(st->block, st->user_data);
}

static void on_del_row(GtkButton* btn, gpointer user_data) {
    (void)btn;
    CanvasTableState* st = user_data;
    if (st->block->table_rows > 1) {
        canvas_table_delete_row(st->block, st->block->table_rows - 1);
        canvas_table_widget_rebuild(st->box);
        if (st->on_change != NULL) st->on_change(st->block, st->user_data);
    }
}

static void on_add_col(GtkButton* btn, gpointer user_data) {
    (void)btn;
    CanvasTableState* st = user_data;
    canvas_table_insert_col(st->block, st->block->table_cols);
    canvas_table_widget_rebuild(st->box);
    if (st->on_change != NULL) st->on_change(st->block, st->user_data);
}

static void on_del_col(GtkButton* btn, gpointer user_data) {
    (void)btn;
    CanvasTableState* st = user_data;
    if (st->block->table_cols > 1) {
        canvas_table_delete_col(st->block, st->block->table_cols - 1);
        canvas_table_widget_rebuild(st->box);
        if (st->on_change != NULL) st->on_change(st->block, st->user_data);
    }
}

static void on_state_destroy(gpointer data) {
    g_free(data);
}

GtkWidget* canvas_table_widget_new(CanvasBlock* b, CanvasTableChangeNotify on_change, gpointer user_data) {
    ensure_css();
    CanvasTableState* st = g_new0(CanvasTableState, 1);
    st->block = b;
    st->on_change = on_change;
    st->user_data = user_data;

    st->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(st->box, "canvas-table-box");
    g_object_set_data_full(G_OBJECT(st->box), TABLE_STATE_KEY, st, on_state_destroy);

    /* Compact toolbar above table */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(bar, "canvas-table-bar");

    GtkWidget* b_add_row = gtk_button_new_with_label("+ Row");
    gtk_widget_add_css_class(b_add_row, "canvas-table-btn");
    g_signal_connect(b_add_row, "clicked", G_CALLBACK(on_add_row), st);
    gtk_box_append(GTK_BOX(bar), b_add_row);

    GtkWidget* b_del_row = gtk_button_new_with_label("- Row");
    gtk_widget_add_css_class(b_del_row, "canvas-table-btn");
    g_signal_connect(b_del_row, "clicked", G_CALLBACK(on_del_row), st);
    gtk_box_append(GTK_BOX(bar), b_del_row);

    GtkWidget* b_add_col = gtk_button_new_with_label("+ Col");
    gtk_widget_add_css_class(b_add_col, "canvas-table-btn");
    g_signal_connect(b_add_col, "clicked", G_CALLBACK(on_add_col), st);
    gtk_box_append(GTK_BOX(bar), b_add_col);

    GtkWidget* b_del_col = gtk_button_new_with_label("- Col");
    gtk_widget_add_css_class(b_del_col, "canvas-table-btn");
    g_signal_connect(b_del_col, "clicked", G_CALLBACK(on_del_col), st);
    gtk_box_append(GTK_BOX(bar), b_del_col);

    gtk_box_append(GTK_BOX(st->box), bar);

    canvas_table_widget_rebuild(st->box);
    return st->box;
}
