/* The cells selected together in a Jupyter notebook tab (see
 * ipynb_tab_private.h): a run of cells from an anchor to the current cell,
 * and the actions that take the whole run (move, delete, set the type,
 * copy, cut, paste, merge), from the keys and the source's context menu */
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
    t->deleted_merged = 0;
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
    guint at = t->deleted_at;
    if (t->deleted_merged != 0) {
        /* The merged cell gives way to the cells it was made of */
        gint merged = ipynb_doc_find_cell(t->doc, t->deleted_merged);
        if (merged >= 0) {
            at = (guint)merged;
            ipynb_doc_remove_cell(t->doc, at);
        }
        t->deleted_merged = 0;
    }
    guint focus = at;
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

/* --- Copy, cut, paste, merge --- */

/* The cells copied last (IpynbCell*, owned), for every notebook tab */
static GPtrArray* clipboard;

void ipynb_select_copy(IpynbTab* t, guint index) {
    guint first, last;
    ipynb_select_range(t, index, &first, &last);
    if (last >= ipynb_doc_n_cells(t->doc)) return;
    if (clipboard == NULL) clipboard = g_ptr_array_new_with_free_func((GDestroyNotify)ipynb_cell_free);
    g_ptr_array_set_size(clipboard, 0);
    for (guint i = first; i <= last; i++) g_ptr_array_add(clipboard, ipynb_doc_copy_cell(t->doc, i));
    gchar* text = g_strdup_printf(ngettext("%u cell copied", "%u cells copied", clipboard->len), clipboard->len);
    ipynb_tab_set_status(t, text, FALSE);
    g_free(text);
}

void ipynb_select_cut(IpynbTab* t, guint index) {
    ipynb_select_copy(t, index);
    ipynb_select_delete(t, index);
}

void ipynb_select_paste(IpynbTab* t, guint index) {
    if (clipboard == NULL || clipboard->len == 0) {
        ipynb_tab_set_status(t, _("No cells to paste: copy some first"), FALSE);
        return;
    }
    guint first, last;
    ipynb_select_range(t, index, &first, &last);
    guint at = MIN(last + 1, ipynb_doc_n_cells(t->doc));
    for (guint i = 0; i < clipboard->len; i++) ipynb_doc_paste_cell(t->doc, at + i, g_ptr_array_index(clipboard, i));
    /* The pasted cells selected, the focus on the last */
    t->anchor = ipynb_doc_cell_key(t->doc, at);
    ipynb_tab_mark_changed(t);
    ipynb_tab_queue_rebuild(t, at + clipboard->len - 1);
}

void ipynb_select_merge(IpynbTab* t, guint index) {
    guint first, last;
    ipynb_select_range(t, index, &first, &last);
    if (first == last) last = first + 1; /* a cell alone: with the next one */
    if (last >= ipynb_doc_n_cells(t->doc)) {
        ipynb_tab_set_status(t, _("No cell below to merge with"), FALSE);
        return;
    }
    /* Kept for Undo, which puts them back in place of the merged cell */
    ipynb_select_drop_deleted(t);
    for (guint i = first; i <= last; i++) g_ptr_array_add(t->deleted, ipynb_doc_copy_cell(t->doc, i));
    t->deleted_at = first;
    t->deleted_merged = ipynb_doc_cell_key(t->doc, first);
    ipynb_doc_merge_cells(t->doc, first, last);
    gtk_widget_set_visible(t->undo_button, TRUE);
    t->anchor = 0;
    gchar* text = g_strdup_printf(ngettext("%u cell merged", "%u cells merged", last - first + 1), last - first + 1);
    ipynb_tab_set_status(t, text, FALSE);
    g_free(text);
    ipynb_tab_mark_changed(t);
    ipynb_tab_queue_rebuild(t, first);
}

gboolean ipynb_select_key(IpynbTab* t, guint keyval, GdkModifierType mods) {
    if (keyval == GDK_KEY_Escape && mods == 0 && ipynb_select_count(t) > 1) {
        ipynb_select_clear(t);
        return TRUE;
    }
    if (mods != (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) return FALSE;
    switch (gdk_keyval_to_lower(keyval)) {
    case GDK_KEY_Up:
    case GDK_KEY_Down:
        ipynb_select_extend(t, keyval == GDK_KEY_Up ? -1 : 1);
        return TRUE;
    case GDK_KEY_c:
        ipynb_select_copy(t, t->current);
        return TRUE;
    case GDK_KEY_x:
        ipynb_select_cut(t, t->current);
        return TRUE;
    case GDK_KEY_v:
        ipynb_select_paste(t, t->current);
        return TRUE;
    case GDK_KEY_m:
        ipynb_select_merge(t, t->current);
        return TRUE;
    default:
        return FALSE;
    }
}

/* --- Context menu of the source --- */

#define MENU_TAB_KEY "lookin-ipynb-menu-tab"
#define MENU_INDEX_KEY "lookin-ipynb-menu-cell"

static guint menu_cell(GtkWidget* view, IpynbTab** t) {
    *t = g_object_get_data(G_OBJECT(view), MENU_TAB_KEY);
    return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(view), MENU_INDEX_KEY));
}

/* Split Cell: at the cursor, or around the selection, as in JupyterLab */
static void on_split_cell(GSimpleAction* action, GVariant* param, gpointer data) {
    (void)action;
    (void)param;
    IpynbTab* t;
    guint index = menu_cell(data, &t);
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

typedef void (*CellAction)(IpynbTab* t, guint index);

static void on_cell_action(GSimpleAction* action, GVariant* param, gpointer data) {
    (void)param;
    IpynbTab* t;
    guint index = menu_cell(data, &t);
    CellAction run = g_object_get_data(G_OBJECT(action), "lookin-ipynb-action");
    if (t != NULL && !t->closed && run != NULL) run(t, index);
}

void ipynb_select_add_menu(IpynbTab* t, guint index, GtkWidget* view) {
    static const struct {
        const gchar* name;
        const gchar* label;
        const gchar* accel;
        CellAction run;
    } ITEMS[] = {
        { "copy", N_("Copy Cells"), "<Control><Shift>c", ipynb_select_copy },
        { "cut", N_("Cut Cells"), "<Control><Shift>x", ipynb_select_cut },
        { "paste", N_("Paste Cells Below"), "<Control><Shift>v", ipynb_select_paste },
        { "merge", N_("Merge Cells"), "<Control><Shift>m", ipynb_select_merge },
    };
    g_object_set_data(G_OBJECT(view), MENU_TAB_KEY, t);
    g_object_set_data(G_OBJECT(view), MENU_INDEX_KEY, GUINT_TO_POINTER(index));
    GSimpleActionGroup* group = g_simple_action_group_new();
    GMenu* menu = g_menu_new();
    GSimpleAction* split = g_simple_action_new("split", NULL);
    g_signal_connect(split, "activate", G_CALLBACK(on_split_cell), view);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(split));
    g_object_unref(split);
    g_menu_append(menu, _("Split Cell"), "cell.split");
    GMenu* cells = g_menu_new();
    for (gsize i = 0; i < G_N_ELEMENTS(ITEMS); i++) {
        GSimpleAction* action = g_simple_action_new(ITEMS[i].name, NULL);
        g_object_set_data(G_OBJECT(action), "lookin-ipynb-action", (gpointer)ITEMS[i].run);
        g_signal_connect(action, "activate", G_CALLBACK(on_cell_action), view);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));
        g_object_unref(action);
        gchar* detailed = g_strconcat("cell.", ITEMS[i].name, NULL);
        GMenuItem* item = g_menu_item_new(_(ITEMS[i].label), detailed);
        g_menu_item_set_attribute(item, "accel", "s", ITEMS[i].accel);
        g_menu_append_item(cells, item);
        g_object_unref(item);
        g_free(detailed);
    }
    g_menu_append_section(menu, NULL, G_MENU_MODEL(cells));
    g_object_unref(cells);
    gtk_widget_insert_action_group(view, "cell", G_ACTION_GROUP(group));
    g_object_unref(group);
    gtk_text_view_set_extra_menu(GTK_TEXT_VIEW(view), G_MENU_MODEL(menu));
    g_object_unref(menu);
}
