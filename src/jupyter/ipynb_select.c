/* The cells selected together in a Jupyter notebook tab (see
 * ipynb_tab_private.h): a run of cells from an anchor to the current cell,
 * and the actions that take the whole run (move, delete, set the type) */
#include "core/i18n.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_tab_private.h"
#include "theme/theme.h"

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".ipynb-tab .ipynb-cell.ipynb-selected { border-left-color: @accent; }"
                  ".ipynb-tab .ipynb-cell.ipynb-selected > .ipynb-cell-head { background-color: @bg_selected;"
                  " border-radius: 3px 3px 0 0; }");
}

/* The selected run, first..last (TRUE), or FALSE without one (no anchor,
 * or its cell deleted) */
static gboolean selected_run(IpynbTab* t, guint* first, guint* last) {
    if (t->anchor == 0) return FALSE;
    gint anchor = ipynb_doc_find_cell(t->doc, t->anchor);
    if (anchor < 0) {
        t->anchor = 0;
        return FALSE;
    }
    *first = MIN((guint)anchor, t->current);
    *last = MAX((guint)anchor, t->current);
    return TRUE;
}

guint ipynb_select_count(IpynbTab* t) {
    guint first, last;
    return selected_run(t, &first, &last) ? last - first + 1 : 1;
}

void ipynb_select_range(IpynbTab* t, guint index, guint* first, guint* last) {
    if (selected_run(t, first, last) && index >= *first && index <= *last) return;
    if (t->anchor != 0) ipynb_select_clear(t); /* an action on another cell */
    *first = *last = index;
}

void ipynb_select_show(IpynbTab* t) {
    ensure_css();
    guint first = 1, last = 0;
    if (!selected_run(t, &first, &last) || first == last) first = 1, last = 0;
    for (guint i = 0; i < t->views->len; i++) {
        GtkWidget* cell = gtk_widget_get_parent(g_ptr_array_index(t->views, i));
        if (i >= first && i <= last) {
            gtk_widget_add_css_class(cell, "ipynb-selected");
        } else {
            gtk_widget_remove_css_class(cell, "ipynb-selected");
        }
    }
    ipynb_tab_update_info(t);
}

void ipynb_select_clear(IpynbTab* t) {
    if (t->anchor == 0) return;
    t->anchor = 0;
    ipynb_select_show(t);
}

void ipynb_select_extend_to(IpynbTab* t, guint index) {
    if (index >= t->views->len) return;
    if (t->anchor == 0) t->anchor = ipynb_doc_cell_key(t->doc, t->current);
    t->extending = TRUE;
    ipynb_tab_focus_cell(t, index);
    t->extending = FALSE;
    ipynb_select_show(t);
}

void ipynb_select_extend(IpynbTab* t, gint step) {
    ipynb_select_extend_to(t, ipynb_fold_step(t, t->current, step));
}

/* A click on a cell's head (not on its buttons) focuses the cell;
 * Shift+click selects the cells from the current one to it */
static void on_head_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    IpynbTab* t = data;
    GtkWidget* head = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(head), "lookin-ipynb-head-index"));
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    if ((state & GDK_SHIFT_MASK) != 0) {
        ipynb_select_extend_to(t, index);
    } else {
        ipynb_tab_focus_cell(t, index);
    }
}

void ipynb_select_add_head(IpynbTab* t, guint index, GtkWidget* head) {
    g_object_set_data(G_OBJECT(head), "lookin-ipynb-head-index", GUINT_TO_POINTER(index));
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_head_pressed), t);
    gtk_widget_add_controller(head, GTK_EVENT_CONTROLLER(click));
}

/* --- Actions on the selected run --- */

void ipynb_select_move(IpynbTab* t, guint index, gint by) {
    guint n = ipynb_doc_n_cells(t->doc);
    guint first, last;
    ipynb_select_range(t, index, &first, &last);
    if (last >= n || (by < 0 && first == 0) || (by > 0 && last + 1 >= n)) return;
    /* The cell next to the run goes to its other side */
    if (by < 0) {
        ipynb_doc_move_cell(t->doc, first - 1, last);
    } else {
        ipynb_doc_move_cell(t->doc, last + 1, first);
    }
    ipynb_tab_mark_changed(t);
    guint focus = first < last ? t->current : index; /* the run kept as it is */
    ipynb_tab_queue_rebuild(t, by < 0 ? focus - 1 : focus + 1);
}

void ipynb_select_drop_deleted(IpynbTab* t) {
    for (guint i = 0; i < t->deleted->len; i++) ipynb_cell_free(g_ptr_array_index(t->deleted, i));
    g_ptr_array_set_size(t->deleted, 0);
}

void ipynb_select_delete(IpynbTab* t, guint index) {
    guint first, last;
    ipynb_select_range(t, index, &first, &last);
    if (last >= ipynb_doc_n_cells(t->doc)) return;
    ipynb_select_drop_deleted(t);
    for (guint i = first; i <= last; i++) {
        IpynbCell* cell = ipynb_doc_take_cell(t->doc, first);
        if (cell != NULL) g_ptr_array_add(t->deleted, cell);
    }
    t->deleted_at = first;
    t->anchor = 0;
    gtk_widget_set_visible(t->undo_button, TRUE);
    /* A notebook keeps one cell to type in */
    if (ipynb_doc_n_cells(t->doc) == 0) ipynb_doc_insert_cell(t->doc, 0, IPYNB_CELL_CODE);
    ipynb_tab_mark_changed(t);
    guint n = ipynb_doc_n_cells(t->doc);
    ipynb_tab_queue_rebuild(t, first > 0 && first >= n ? first - 1 : first);
}

void ipynb_select_undo_delete(IpynbTab* t) {
    if (t->deleted->len == 0) return;
    guint at = t->deleted_at, focus = at;
    for (guint i = 0; i < t->deleted->len; i++) {
        guint put = ipynb_doc_put_cell(t->doc, at + i, g_ptr_array_index(t->deleted, i));
        if (i == 0) focus = put;
    }
    g_ptr_array_set_size(t->deleted, 0); /* the notebook's again */
    gtk_widget_set_visible(t->undo_button, FALSE);
    ipynb_tab_mark_changed(t);
    ipynb_tab_queue_rebuild(t, focus);
}

void ipynb_select_set_type(IpynbTab* t, guint index, IpynbCellType type) {
    guint first, last;
    ipynb_select_range(t, index, &first, &last);
    gboolean changed = FALSE;
    for (guint i = first; i <= last && i < ipynb_doc_n_cells(t->doc); i++) {
        if (ipynb_doc_cell_type(t->doc, i) == type) continue;
        ipynb_doc_set_type(t->doc, i, type);
        /* Made Markdown: still edited until run */
        g_hash_table_add(t->editing, GUINT_TO_POINTER(ipynb_doc_cell_key(t->doc, i)));
        changed = TRUE;
    }
    if (!changed) return;
    ipynb_tab_mark_changed(t);
    ipynb_tab_queue_rebuild(t, first < last ? t->current : index);
}
