/* The breakdown of the Disk Usage tab (see disk_usage_list.h) */
#include "diskuse/disk_usage_list.h"
#include "core/disk_stats.h"
#include "core/i18n.h"
#include "core/path_copy.h"
#include "explorer/nav_keys.h"
#include "theme/icons.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"

#define LIST_KEY "lookin-disk-usage-list"
#define COLUMN_KEY "lookin-disk-usage-column"
#define ITEM_KEY "lookin-disk-usage-item"

typedef enum {
    COL_NAME,
    COL_SHARE,
    COL_SIZE,
    COL_FILES,
    COL_DATE,
} DuColumn;

typedef enum {
    ROW_DIR,
    ROW_FILE,
    ROW_OTHER, /* the files not listed, summed up */
} DuRowKind;

/* One row, as shown */
#define DU_TYPE_ROW (du_row_get_type())
G_DECLARE_FINAL_TYPE(DuRow, du_row, DU, ROW, GObject)

struct _DuRow {
    GObject parent;
    DuRowKind kind;
    gchar* name;       /* shown name */
    gchar* path;       /* full path, NULL for ROW_OTHER */
    guint64 size;
    guint64 total;     /* size of the folder shown: the share */
    guint64 files;     /* files inside (folders), counted (ROW_OTHER), 0 (files) */
    gint64 mtime;
    DiskNode* node;    /* folder, borrowed; NULL while the scan runs */
    gboolean unreadable;
};

G_DEFINE_FINAL_TYPE(DuRow, du_row, G_TYPE_OBJECT)

static void du_row_finalize(GObject* obj) {
    DuRow* r = DU_ROW(obj);
    g_free(r->name);
    g_free(r->path);
    G_OBJECT_CLASS(du_row_parent_class)->finalize(obj);
}

static void du_row_class_init(DuRowClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = du_row_finalize;
}

static void du_row_init(DuRow* r) {
    (void)r;
}

typedef struct {
    AppData* app;
    DiskUsageListHandlers handlers;
    GListStore* store;
    GtkSingleSelection* selection;
    GtkWidget* view;
    guint shown_top;      /* folders of the root shown while the scan runs */
    gboolean shown_files; /* the row of the root's files shown while it runs */
} DuList;

static gint cell_chars(DuColumn col) {
    switch (col) {
    case COL_NAME: return 24;
    case COL_SHARE: return 7;
    case COL_SIZE: return 10;
    case COL_FILES: return 8;
    case COL_DATE: return 10;
    }
    return 8;
}

static gchar* date_text(gint64 mtime) {
    if (mtime <= 0) return g_strdup("");
    GDateTime* dt = g_date_time_new_from_unix_local(mtime);
    if (dt == NULL) return g_strdup("");
    gchar* text = g_date_time_format(dt, "%Y-%m-%d");
    g_date_time_unref(dt);
    return text;
}

/* Text of a cell (not the name or the share) */
static gchar* cell_text(DuRow* r, DuColumn col) {
    switch (col) {
    case COL_SIZE: return r->unreadable ? g_strdup("?") : g_format_size(r->size);
    case COL_FILES:
        if (r->kind == ROW_FILE) return g_strdup("");
        return r->unreadable ? g_strdup("?") : g_strdup_printf("%" G_GUINT64_FORMAT, r->files);
    case COL_DATE: return date_text(r->mtime);
    case COL_NAME:
    case COL_SHARE: break;
    }
    return g_strdup("");
}

/* A right click on a row: select it, then its menu */
static void on_cell_menu(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    DuColumn col = GPOINTER_TO_INT(data);
    GtkWidget* child;
    if (col == COL_NAME) {
        child = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_append(GTK_BOX(child), file_type_image_new());
        GtkWidget* label = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_width_chars(GTK_LABEL(label), cell_chars(col));
        gtk_widget_set_hexpand(label, TRUE);
        gtk_box_append(GTK_BOX(child), label);
    } else if (col == COL_SHARE) {
        child = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* bar = gtk_level_bar_new_for_interval(0.0, 1.0);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_LOW);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_HIGH);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_FULL);
        gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request(bar, 90, -1);
        gtk_widget_set_hexpand(bar, TRUE);
        gtk_box_append(GTK_BOX(child), bar);
        GtkWidget* label = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(label), 1.0f);
        gtk_label_set_width_chars(GTK_LABEL(label), cell_chars(col));
        gtk_widget_add_css_class(label, "du-num");
        gtk_box_append(GTK_BOX(child), label);
    } else {
        child = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(child), col == COL_DATE ? 0.0f : 1.0f);
        gtk_label_set_width_chars(GTK_LABEL(child), cell_chars(col));
        gtk_widget_add_css_class(child, "du-num");
    }
    g_object_set_data(G_OBJECT(child), COLUMN_KEY, data);
    g_object_set_data(G_OBJECT(child), ITEM_KEY, item);
    GtkGesture* menu = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(menu), GDK_BUTTON_SECONDARY);
    g_signal_connect(menu, "pressed", G_CALLBACK(on_cell_menu), NULL);
    gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(menu));
    gtk_list_item_set_child(item, child);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    DuColumn col = GPOINTER_TO_INT(data);
    DuRow* r = gtk_list_item_get_item(item);
    GtkWidget* child = gtk_list_item_get_child(item);
    if (col == COL_NAME) {
        GtkWidget* image = gtk_widget_get_first_child(child);
        GtkWidget* label = gtk_widget_get_next_sibling(image);
        file_type_image_set(image, r->kind == ROW_OTHER ? "" : r->name, r->kind == ROW_DIR);
        if (r->unreadable) {
            /* TRANSLATORS: a folder that could not be read */
            gchar* text = g_strdup_printf(_("%s (unreadable)"), r->name);
            gtk_label_set_text(GTK_LABEL(label), text);
            g_free(text);
        } else {
            gtk_label_set_text(GTK_LABEL(label), r->name);
        }
        gtk_widget_set_tooltip_text(label, r->path);
        if (r->kind == ROW_OTHER || r->unreadable) {
            gtk_widget_add_css_class(label, "du-muted");
        } else {
            gtk_widget_remove_css_class(label, "du-muted");
        }
    } else if (col == COL_SHARE) {
        GtkWidget* bar = gtk_widget_get_first_child(child);
        GtkWidget* label = gtk_widget_get_next_sibling(bar);
        gtk_level_bar_set_value(GTK_LEVEL_BAR(bar), CLAMP(disk_percent(r->size, r->total) / 100.0, 0.0, 1.0));
        gchar* text = disk_format_percent(r->size, r->total);
        gtk_label_set_text(GTK_LABEL(label), text);
        g_free(text);
    } else {
        gchar* text = cell_text(r, col);
        gtk_label_set_text(GTK_LABEL(child), text);
        g_free(text);
    }
}

static gint cmp_u64(guint64 a, guint64 b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

static gint compare_column(const DuRow* a, const DuRow* b, DuColumn col) {
    switch (col) {
    case COL_NAME: return g_utf8_collate(a->name, b->name);
    case COL_SHARE:
    case COL_SIZE: return cmp_u64(a->size, b->size);
    case COL_FILES: return cmp_u64(a->files, b->files);
    case COL_DATE: return a->mtime < b->mtime ? -1 : a->mtime > b->mtime ? 1 : 0;
    }
    return 0;
}

/* Ties by name: a stable order */
static gint compare(gconstpointer a, gconstpointer b, gpointer data) {
    gint c = compare_column(a, b, (DuColumn)GPOINTER_TO_INT(data));
    return c != 0 ? c : g_utf8_collate(((const DuRow*)a)->name, ((const DuRow*)b)->name);
}

static GtkColumnViewColumn* add_column(GtkWidget* view, const gchar* title, DuColumn col, gboolean expand) {
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), GINT_TO_POINTER(col));
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), GINT_TO_POINTER(col));
    GtkColumnViewColumn* c = gtk_column_view_column_new(title, factory);
    gtk_column_view_column_set_expand(c, expand);
    gtk_column_view_column_set_resizable(c, TRUE);
    GtkCustomSorter* sorter = gtk_custom_sorter_new(compare, GINT_TO_POINTER(col), NULL);
    gtk_column_view_column_set_sorter(c, GTK_SORTER(sorter));
    g_object_unref(sorter);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), c);
    g_object_unref(c);
    return c;
}

static DuList* list_state(GtkWidget* list) {
    return list != NULL ? g_object_get_data(G_OBJECT(list), LIST_KEY) : NULL;
}

static DuRow* selected_row(DuList* dl) {
    return dl != NULL ? gtk_single_selection_get_selected_item(dl->selection) : NULL;
}

static void activate_row(DuList* dl, DuRow* r) {
    if (r == NULL || r->kind == ROW_OTHER || dl->handlers.activate == NULL) return;
    dl->handlers.activate(dl->handlers.data, r->kind == ROW_DIR ? r->node : NULL, r->path);
}

static void on_activate(GtkColumnView* view, guint position, gpointer data) {
    (void)view;
    DuList* dl = data;
    DuRow* r = g_list_model_get_item(G_LIST_MODEL(dl->selection), position);
    activate_row(dl, r);
    if (r != NULL) g_object_unref(r);
}

static void go_up(DuList* dl) {
    if (dl->handlers.up != NULL) dl->handlers.up(dl->handlers.data);
}

/* After the list's navigation keys (focus_route_attach_list()), which leave
 * Left / Right and Enter: into the folder, back to its parent */
static gboolean on_key(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state,
                       gpointer data) {
    (void)controller;
    (void)keycode;
    DuList* dl = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    if (keyval == GDK_KEY_BackSpace && mods == 0) {
        go_up(dl);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) && mods == GDK_ALT_MASK) {
        go_up(dl);
        return TRUE;
    }
    NavPrefix next = NAV_PREFIX_NONE;
    switch (nav_keys_lookup(app_nav_style(dl->app), NAV_PREFIX_NONE, keyval, state, &next)) {
    case NAV_FORWARD: {
        DuRow* r = selected_row(dl);
        if (r != NULL && r->kind == ROW_DIR) activate_row(dl, r);
        return TRUE;
    }
    case NAV_BACKWARD:
        go_up(dl);
        return TRUE;
    default:
        return FALSE;
    }
}

/* The context menu */

typedef enum {
    MENU_OPEN,
    MENU_REVEAL,
    MENU_COPY_PATH,
} DuMenuAction;

typedef struct {
    DuList* dl;
    DuMenuAction action;
    DuRow* row; /* owned ref */
} DuMenuRun;

static gboolean run_menu_action(gpointer data) {
    DuMenuRun* m = data;
    switch (m->action) {
    case MENU_OPEN: activate_row(m->dl, m->row); break;
    case MENU_REVEAL:
        if (m->dl->handlers.reveal != NULL) m->dl->handlers.reveal(m->dl->handlers.data, m->row->path);
        break;
    case MENU_COPY_PATH: path_copy(m->dl->app, m->row->path, PATH_COPY_ABSOLUTE); break;
    }
    g_object_unref(m->row);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void on_menu_item(GtkButton* button, gpointer data) {
    GtkWidget* popover = data;
    DuMenuRun* m = g_new0(DuMenuRun, 1);
    m->dl = g_object_get_data(G_OBJECT(popover), LIST_KEY);
    m->row = g_object_ref(g_object_get_data(G_OBJECT(popover), ITEM_KEY));
    m->action = (DuMenuAction)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), COLUMN_KEY));
    gtk_popover_popdown(GTK_POPOVER(popover));
    g_idle_add(run_menu_action, m);
}

static void menu_item(GtkWidget* box, GtkWidget* popover, const gchar* text, const gchar* hint, DuMenuAction action) {
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    if (hint != NULL) {
        GtkWidget* key = gtk_label_new(hint);
        gtk_widget_add_css_class(key, "dim-label");
        gtk_box_append(GTK_BOX(row), key);
    }
    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_object_set_data(G_OBJECT(btn), COLUMN_KEY, GINT_TO_POINTER(action));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_menu_item), popover);
    gtk_box_append(GTK_BOX(box), btn);
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

static void on_cell_menu(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)data;
    GtkWidget* cell = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkListItem* item = g_object_get_data(G_OBJECT(cell), ITEM_KEY);
    DuRow* r = item != NULL ? gtk_list_item_get_item(item) : NULL;
    GtkWidget* view = gtk_widget_get_ancestor(cell, GTK_TYPE_COLUMN_VIEW);
    DuList* dl = view != NULL ? g_object_get_data(G_OBJECT(view), LIST_KEY) : NULL;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (r == NULL || dl == NULL || r->kind == ROW_OTHER) return;
    gtk_single_selection_set_selected(dl->selection, gtk_list_item_get_position(item));
    gtk_widget_grab_focus(view);

    graphene_point_t at;
    if (!gtk_widget_compute_point(cell, view, &GRAPHENE_POINT_INIT((float)x, (float)y), &at)) {
        at = GRAPHENE_POINT_INIT(0.0f, 0.0f);
    }
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, view);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = { (gint)at.x, (gint)at.y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(popover), LIST_KEY, dl);
    g_object_set_data_full(G_OBJECT(popover), ITEM_KEY, g_object_ref(r), g_object_unref);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (r->kind == ROW_DIR && r->node != NULL) menu_item(box, popover, _("Open Folder"), "Enter", MENU_OPEN);
    menu_item(box, popover, _("Show in Explorer"), r->kind == ROW_FILE ? "Enter" : NULL, MENU_REVEAL);
    menu_item(box, popover, _("Copy Path"), NULL, MENU_COPY_PATH);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void list_free(gpointer data) {
    DuList* dl = data;
    g_object_unref(dl->selection); /* holds the sorted model, which holds the store */
    g_object_unref(dl->store);
    g_free(dl);
}

GtkWidget* disk_usage_list_new(AppData* app, const DiskUsageListHandlers* handlers) {
    DuList* dl = g_new0(DuList, 1);
    dl->app = app;
    if (handlers != NULL) dl->handlers = *handlers;
    dl->store = g_list_store_new(DU_TYPE_ROW);

    dl->view = gtk_column_view_new(NULL);
    gtk_widget_set_name(dl->view, "disk-usage-list");
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(dl->view), FALSE);
    add_column(dl->view, _("Name"), COL_NAME, TRUE);
    add_column(dl->view, _("Share"), COL_SHARE, TRUE);
    GtkColumnViewColumn* size = add_column(dl->view, _("Size"), COL_SIZE, FALSE);
    add_column(dl->view, _("Files"), COL_FILES, FALSE);
    add_column(dl->view, _("Last Change"), COL_DATE, FALSE);

    GtkSorter* sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(dl->view));
    GtkSortListModel* sorted = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(dl->store)), g_object_ref(sorter));
    dl->selection = gtk_single_selection_new(G_LIST_MODEL(sorted));
    gtk_single_selection_set_autoselect(dl->selection, TRUE);
    gtk_column_view_set_model(GTK_COLUMN_VIEW(dl->view), GTK_SELECTION_MODEL(dl->selection));
    gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(dl->view), size, GTK_SORT_DESCENDING);
    g_signal_connect(dl->view, "activate", G_CALLBACK(on_activate), dl);
    g_object_set_data(G_OBJECT(dl->view), LIST_KEY, dl);
    focus_route_attach_list(app, dl->view, dl->selection);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), dl);
    gtk_widget_add_controller(dl->view, keys);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), dl->view);
    g_object_set_data_full(G_OBJECT(scroll), LIST_KEY, dl, list_free);
    return scroll;
}

GtkWidget* disk_usage_list_view(GtkWidget* list) {
    DuList* dl = list_state(list);
    return dl != NULL ? dl->view : NULL;
}

static DuRow* row_new(DuRowKind kind, const gchar* name, gchar* path, guint64 size, guint64 total) {
    DuRow* r = g_object_new(DU_TYPE_ROW, NULL);
    r->kind = kind;
    r->name = g_strdup(name);
    r->path = path;
    r->size = size;
    r->total = total;
    return r;
}

static DuRow* folder_row(const DiskNode* n, const gchar* parent_path, guint64 total) {
    DuRow* r = row_new(ROW_DIR, n->name, g_build_filename(parent_path, n->name, NULL), n->size, total);
    r->files = n->n_files;
    r->mtime = n->mtime;
    r->unreadable = n->unreadable;
    return r;
}

/* The row of the files not listed: "12 other files" or, when none of them
 * is listed, "12 files" */
static DuRow* other_row(guint64 count, guint64 size, guint64 total, gboolean all) {
    gchar* num = g_strdup_printf("%" G_GUINT64_FORMAT, count);
    gulong n = (gulong)MIN(count, G_MAXULONG);
    /* TRANSLATORS: %s is a number of files */
    gchar* name = all ? g_strdup_printf(ngettext("%s file", "%s files", n), num)
                      : g_strdup_printf(ngettext("%s other file", "%s other files", n), num);
    DuRow* r = row_new(ROW_OTHER, name, NULL, size, total);
    r->files = count;
    g_free(name);
    g_free(num);
    return r;
}

/* Put the rows in the list, the one named select selected (else the first) */
static void set_rows(DuList* dl, GPtrArray* rows, const gchar* select) {
    g_list_store_splice(dl->store, 0, g_list_model_get_n_items(G_LIST_MODEL(dl->store)), rows->pdata, rows->len);
    GListModel* model = G_LIST_MODEL(dl->selection);
    guint n = g_list_model_get_n_items(model);
    guint pos = 0;
    for (guint i = 0; select != NULL && i < n; i++) {
        DuRow* r = g_list_model_get_item(model, i);
        gboolean found = g_strcmp0(r->name, select) == 0;
        g_object_unref(r);
        if (found) {
            pos = i;
            break;
        }
    }
    if (n > 0) {
        gtk_single_selection_set_selected(dl->selection, pos);
        gtk_column_view_scroll_to(GTK_COLUMN_VIEW(dl->view), pos, NULL, GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT, NULL);
    }
}

void disk_usage_list_show_node(GtkWidget* list, DiskNode* node, const gchar* select) {
    DuList* dl = list_state(list);
    if (dl == NULL) return;
    dl->shown_top = 0;
    dl->shown_files = FALSE;
    GPtrArray* rows = g_ptr_array_new_with_free_func(g_object_unref);
    if (node != NULL) {
        gchar* path = disk_node_path(node);
        for (guint i = 0; node->children != NULL && i < node->children->len; i++) {
            DiskNode* c = g_ptr_array_index(node->children, i);
            DuRow* r = folder_row(c, path, node->size);
            r->node = c;
            g_ptr_array_add(rows, r);
        }
        for (guint i = 0; node->files != NULL && i < node->files->len; i++) {
            const DiskFile* f = &g_array_index(node->files, DiskFile, i);
            DuRow* r = row_new(ROW_FILE, f->name, g_build_filename(path, f->name, NULL), f->size, node->size);
            r->mtime = f->mtime;
            g_ptr_array_add(rows, r);
        }
        guint64 other = disk_node_other_files_count(node);
        if (other > 0) g_ptr_array_add(rows, other_row(other, disk_node_other_files_size(node), node->size, FALSE));
        g_free(path);
    }
    set_rows(dl, rows, select);
    g_ptr_array_unref(rows);
}

void disk_usage_list_show_progress(GtkWidget* list, const gchar* root, const DiskScanSnapshot* snap) {
    DuList* dl = list_state(list);
    if (dl == NULL || snap == NULL || snap->top == NULL) return;
    gboolean files = snap->top_total > 0 || snap->root_files_count > 0;
    if (snap->top->len == dl->shown_top && files == dl->shown_files) return; /* nothing new */
    gchar* selected = disk_usage_list_selected_name(list);
    GPtrArray* rows = g_ptr_array_new_with_free_func(g_object_unref);
    for (guint i = 0; i < snap->top->len; i++) {
        g_ptr_array_add(rows, folder_row(g_ptr_array_index(snap->top, i), root, snap->size));
    }
    if (snap->root_files_count > 0) {
        g_ptr_array_add(rows, other_row(snap->root_files_count, snap->root_files_size, snap->size, TRUE));
    }
    set_rows(dl, rows, selected);
    g_ptr_array_unref(rows);
    g_free(selected);
    dl->shown_top = snap->top->len;
    dl->shown_files = files;
}

void disk_usage_list_clear(GtkWidget* list) {
    DuList* dl = list_state(list);
    if (dl == NULL) return;
    g_list_store_remove_all(dl->store);
    dl->shown_top = 0;
    dl->shown_files = FALSE;
}

gchar* disk_usage_list_selected_name(GtkWidget* list) {
    DuRow* r = selected_row(list_state(list));
    return r != NULL ? g_strdup(r->name) : NULL;
}
