#include "canvas/canvas_table.h"
#include "theme.h"

#define TABLE_STATE_KEY "canvas-table-state"

typedef struct {
    CanvasBlock* block;
    CanvasTableChangeNotify on_change;
    gpointer user_data;
    GtkWidget* box;
    GtkWidget* grid;
    gboolean rebuilding;
    gint cur_row, cur_col; /* current cell; -1: none yet */
} CanvasTableState;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-table-box { background-color: @bg_view; border-radius: 4px; }"
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

static void on_cell_focus(GtkEventControllerFocus* ctrl, gpointer user_data) {
    CanvasTableState* st = user_data;
    GtkWidget* entry = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    st->cur_row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "row"));
    st->cur_col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "col"));
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
            GtkEventController* focus = gtk_event_controller_focus_new();
            g_signal_connect(focus, "enter", G_CALLBACK(on_cell_focus), st);
            gtk_widget_add_controller(entry, focus);

            gtk_grid_attach(GTK_GRID(st->grid), entry, c, r, 1, 1);
        }
    }

    st->rebuilding = FALSE;
}

/* The current cell, kept inside the table */
static void current_cell(CanvasTableState* st, gint* row, gint* col) {
    gint rows = st->block->table_rows, cols = st->block->table_cols;
    *row = st->cur_row >= 0 ? CLAMP(st->cur_row, 0, rows - 1) : rows - 1;
    *col = st->cur_col >= 0 ? CLAMP(st->cur_col, 0, cols - 1) : cols - 1;
}

gboolean canvas_table_widget_can(GtkWidget* table_widget, CanvasTableAction action) {
    CanvasTableState* st = g_object_get_data(G_OBJECT(table_widget), TABLE_STATE_KEY);
    if (st == NULL) return FALSE;
    if (action == CANVAS_TABLE_ROW_DELETE) return st->block->table_rows > 1;
    if (action == CANVAS_TABLE_COL_DELETE) return st->block->table_cols > 1;
    return TRUE;
}

void canvas_table_widget_apply(GtkWidget* table_widget, CanvasTableAction action) {
    CanvasTableState* st = g_object_get_data(G_OBJECT(table_widget), TABLE_STATE_KEY);
    if (st == NULL || !canvas_table_widget_can(table_widget, action)) return;
    GtkRoot* root = gtk_widget_get_root(table_widget);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    gboolean typing = focus != NULL && st->grid != NULL && gtk_widget_is_ancestor(focus, st->grid);
    gint row, col;
    current_cell(st, &row, &col);
    switch (action) {
    case CANVAS_TABLE_ROW_ABOVE: canvas_table_insert_row(st->block, row); break;
    case CANVAS_TABLE_ROW_BELOW: canvas_table_insert_row(st->block, ++row); break;
    case CANVAS_TABLE_ROW_DELETE: canvas_table_delete_row(st->block, row); break;
    case CANVAS_TABLE_COL_LEFT: canvas_table_insert_col(st->block, col); break;
    case CANVAS_TABLE_COL_RIGHT: canvas_table_insert_col(st->block, ++col); break;
    case CANVAS_TABLE_COL_DELETE: canvas_table_delete_col(st->block, col); break;
    }
    row = MIN(row, st->block->table_rows - 1);
    col = MIN(col, st->block->table_cols - 1);
    st->cur_row = row;
    st->cur_col = col;
    canvas_table_widget_rebuild(st->box);
    if (typing) {
        GtkWidget* cell = gtk_grid_get_child_at(GTK_GRID(st->grid), col, row);
        if (cell != NULL) gtk_widget_grab_focus(cell);
    }
    if (st->on_change != NULL) st->on_change(st->block, st->user_data);
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
    st->cur_row = -1;
    st->cur_col = -1;

    st->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(st->box, "canvas-table-box");
    g_object_set_data_full(G_OBJECT(st->box), TABLE_STATE_KEY, st, on_state_destroy);

    canvas_table_widget_rebuild(st->box);
    return st->box;
}
