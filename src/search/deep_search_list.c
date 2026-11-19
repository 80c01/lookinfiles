/* The results of the Deep Search tab (see deep_search_list.h) */
#include "search/deep_search_list.h"
#include "core/date_bins.h"
#include "core/i18n.h"
#include "core/path_copy.h"
#include "theme/icons.h"
#include "widgets/focus_route.h"

#include <string.h>

#define LIST_KEY "lookin-deep-search-list"
#define COLUMN_KEY "lookin-deep-search-column"
#define ITEM_KEY "lookin-deep-search-item"

typedef enum {
    COL_NAME,
    COL_FOLDER,
    COL_SIZE,
    COL_DATE,
    COL_MATCHES,
    COL_LINE,
} DsColumn;

/* One row: a hit and its name */
#define DS_TYPE_ROW (ds_row_get_type())
G_DECLARE_FINAL_TYPE(DsRow, ds_row, DS, ROW, GObject)

struct _DsRow {
    GObject parent;
    DeepHit* hit;
    const gchar* name;   /* in hit->path */
    const gchar* folder; /* in hit->path: below the folder searched ("" for it) */
    gsize folder_len;
    gint day;            /* day of the last change (YYYYMMDD), 0 until needed */
};

G_DEFINE_FINAL_TYPE(DsRow, ds_row, G_TYPE_OBJECT)

static void ds_row_finalize(GObject* obj) {
    deep_hit_free(DS_ROW(obj)->hit);
    G_OBJECT_CLASS(ds_row_parent_class)->finalize(obj);
}

static void ds_row_class_init(DsRowClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = ds_row_finalize;
}

static void ds_row_init(DsRow* r) {
    (void)r;
}

typedef struct {
    AppData* app;
    DeepSearchListHandlers handlers;
    GListStore* store;
    GtkFilterListModel* filtered; /* the store through the range */
    GtkCustomFilter* filter;
    gboolean ranged;              /* range applies */
    DeepSearchRange range;
    GtkSingleSelection* selection;
    GtkWidget* view;
    GtkColumnViewColumn* matches;
    GtkColumnViewColumn* line;
    gchar* root;
} DsList;

static DsRow* row_new(DeepHit* hit, const gchar* root) {
    DsRow* r = g_object_new(DS_TYPE_ROW, NULL);
    r->hit = hit;
    const gchar* path = hit->path;
    const gchar* slash = strrchr(path, G_DIR_SEPARATOR);
    r->name = slash != NULL && slash[1] != '\0' ? slash + 1 : path;
    gsize n = root != NULL ? strlen(root) : 0;
    const gchar* folder = path;
    if (n > 0 && strncmp(path, root, n) == 0) {
        folder = path + n;
        while (G_IS_DIR_SEPARATOR(*folder)) folder++;
    }
    r->folder = folder;
    r->folder_len = r->name > folder ? (gsize)(r->name - folder - 1) : 0;
    return r;
}

static gchar* folder_text(const DsRow* r) {
    return r->folder_len > 0 ? g_strndup(r->folder, r->folder_len) : g_strdup(".");
}

static gchar* date_text(gint64 mtime) {
    GDateTime* dt = mtime > 0 ? g_date_time_new_from_unix_local(mtime) : NULL;
    if (dt == NULL) return g_strdup("");
    gchar* text = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return text;
}

static gchar* cell_text(const DsRow* r, DsColumn col) {
    const DeepHit* h = r->hit;
    switch (col) {
    case COL_FOLDER: return folder_text(r);
    case COL_SIZE: return h->is_dir ? g_strdup("") : g_format_size(h->size);
    case COL_DATE: return date_text(h->mtime);
    case COL_MATCHES:
        if (h->matches == 0) return g_strdup("");
        return h->more ? g_strdup_printf("%u+", h->matches) : g_strdup_printf("%u", h->matches);
    case COL_LINE: {
        if (h->text == NULL) return g_strdup("");
        const gchar* t = h->text;
        while (*t == ' ') t++; /* the indentation */
        return g_strdup_printf("%d: %s", h->line + 1, t);
    }
    case COL_NAME: break;
    }
    return g_strdup("");
}

static gint cell_chars(DsColumn col) {
    switch (col) {
    case COL_NAME: return 22;
    case COL_FOLDER: return 18;
    case COL_SIZE: return 9;
    case COL_DATE: return 16;
    case COL_MATCHES: return 5;
    case COL_LINE: return 30;
    }
    return 8;
}

static void on_cell_menu(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    DsColumn col = GPOINTER_TO_INT(data);
    GtkWidget* child;
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(label), cell_chars(col));
    if (col == COL_NAME) {
        child = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_append(GTK_BOX(child), file_type_image_new());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_box_append(GTK_BOX(child), label);
    } else {
        child = label;
        gboolean right = col == COL_SIZE || col == COL_MATCHES;
        gtk_label_set_xalign(GTK_LABEL(label), right ? 1.0f : 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(label), col == COL_LINE ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_MIDDLE);
        if (right || col == COL_DATE) gtk_widget_add_css_class(label, "ds-num");
        if (col == COL_FOLDER) gtk_widget_add_css_class(label, "cell-muted");
        if (col == COL_LINE) gtk_widget_add_css_class(label, "ds-code");
    }
    g_object_set_data(G_OBJECT(child), ITEM_KEY, item);
    GtkGesture* menu = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(menu), GDK_BUTTON_SECONDARY);
    g_signal_connect(menu, "pressed", G_CALLBACK(on_cell_menu), NULL);
    gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(menu));
    gtk_list_item_set_child(item, child);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    DsColumn col = GPOINTER_TO_INT(data);
    DsRow* r = gtk_list_item_get_item(item);
    GtkWidget* child = gtk_list_item_get_child(item);
    if (col == COL_NAME) {
        GtkWidget* image = gtk_widget_get_first_child(child);
        GtkWidget* label = gtk_widget_get_next_sibling(image);
        file_type_image_set(image, r->name, r->hit->is_dir);
        gtk_label_set_text(GTK_LABEL(label), r->name);
        gtk_widget_set_tooltip_text(label, r->hit->path);
    } else {
        gchar* text = cell_text(r, col);
        gtk_label_set_text(GTK_LABEL(child), text);
        gtk_widget_set_tooltip_text(child, col == COL_LINE || col == COL_FOLDER ? text : NULL);
        g_free(text);
    }
}

static gint cmp_i64(gint64 a, gint64 b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

static gint compare(gconstpointer pa, gconstpointer pb, gpointer data) {
    const DsRow* a = pa;
    const DsRow* b = pb;
    gint c = 0;
    switch ((DsColumn)GPOINTER_TO_INT(data)) {
    case COL_NAME: c = g_utf8_collate(a->name, b->name); break;
    case COL_FOLDER:
        c = strncmp(a->folder, b->folder, MIN(a->folder_len, b->folder_len));
        if (c == 0) c = cmp_i64((gint64)a->folder_len, (gint64)b->folder_len);
        break;
    case COL_SIZE: c = cmp_i64((gint64)a->hit->size, (gint64)b->hit->size); break;
    case COL_DATE: c = cmp_i64(a->hit->mtime, b->hit->mtime); break;
    case COL_MATCHES: c = cmp_i64(a->hit->matches, b->hit->matches); break;
    case COL_LINE: c = g_strcmp0(a->hit->text, b->hit->text); break;
    }
    return c != 0 ? c : strcmp(a->hit->path, b->hit->path); /* a stable order */
}

static GtkColumnViewColumn* add_column(GtkWidget* view, const gchar* title, DsColumn col, gboolean expand) {
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

static gboolean in_range(gpointer item, gpointer data) {
    DsList* dl = data;
    if (!dl->ranged) return TRUE;
    DsRow* r = item;
    const DeepHit* h = r->hit;
    const DeepSearchRange* g = &dl->range;
    if (h->is_dir) return FALSE;
    if (g->min_size >= 0 && h->size < (guint64)g->min_size) return FALSE;
    if (g->max_size >= 0 && h->size > (guint64)g->max_size) return FALSE;
    if (g->first_day > 0 || g->last_day > 0) {
        if (r->day == 0) r->day = date_bins_day_of(h->mtime);
        if (g->first_day > 0 && r->day < g->first_day) return FALSE;
        if (g->last_day > 0 && r->day > g->last_day) return FALSE;
    }
    return TRUE;
}

static DsList* list_state(GtkWidget* list) {
    return list != NULL ? g_object_get_data(G_OBJECT(list), LIST_KEY) : NULL;
}

static void reveal_row(DsList* dl, DsRow* r) {
    if (r != NULL && dl->handlers.reveal != NULL) dl->handlers.reveal(dl->handlers.data, r->hit->path);
}

/* A file in an editor tab, at its first match; a folder in the explorer */
static void open_row(DsList* dl, DsRow* r) {
    if (r == NULL) return;
    if (r->hit->is_dir || dl->handlers.open == NULL) {
        reveal_row(dl, r);
        return;
    }
    dl->handlers.open(dl->handlers.data, r->hit->path, r->hit->text != NULL ? r->hit->line : 0,
                      r->hit->text != NULL ? r->hit->column : 0);
}

static void on_activate(GtkColumnView* view, guint position, gpointer data) {
    (void)view;
    DsList* dl = data;
    DsRow* r = g_list_model_get_item(G_LIST_MODEL(dl->selection), position);
    reveal_row(dl, r);
    if (r != NULL) g_object_unref(r);
}

/* Ctrl+Enter: open in an editor tab (Enter is the activation) */
static gboolean on_key(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state,
                       gpointer data) {
    (void)controller;
    (void)keycode;
    DsList* dl = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && mods == GDK_CONTROL_MASK) {
        open_row(dl, gtk_single_selection_get_selected_item(dl->selection));
        return TRUE;
    }
    return FALSE;
}

/* The context menu */

typedef enum {
    MENU_REVEAL,
    MENU_OPEN,
    MENU_COPY_PATH,
} DsMenuAction;

typedef struct {
    DsList* dl;
    DsMenuAction action;
    DsRow* row; /* owned ref */
} DsMenuRun;

static gboolean run_menu_action(gpointer data) {
    DsMenuRun* m = data;
    switch (m->action) {
    case MENU_REVEAL: reveal_row(m->dl, m->row); break;
    case MENU_OPEN: open_row(m->dl, m->row); break;
    case MENU_COPY_PATH: path_copy(m->dl->app, m->row->hit->path, PATH_COPY_ABSOLUTE); break;
    }
    g_object_unref(m->row);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void on_menu_item(GtkButton* button, gpointer data) {
    GtkWidget* popover = data;
    DsMenuRun* m = g_new0(DsMenuRun, 1);
    m->dl = g_object_get_data(G_OBJECT(popover), LIST_KEY);
    m->row = g_object_ref(g_object_get_data(G_OBJECT(popover), ITEM_KEY));
    m->action = (DsMenuAction)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), COLUMN_KEY));
    gtk_popover_popdown(GTK_POPOVER(popover));
    g_idle_add(run_menu_action, m);
}

static void menu_item(GtkWidget* box, GtkWidget* popover, const gchar* text, const gchar* hint, DsMenuAction action) {
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
    DsRow* r = item != NULL ? gtk_list_item_get_item(item) : NULL;
    GtkWidget* view = gtk_widget_get_ancestor(cell, GTK_TYPE_COLUMN_VIEW);
    DsList* dl = view != NULL ? g_object_get_data(G_OBJECT(view), LIST_KEY) : NULL;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (r == NULL || dl == NULL) return;
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
    menu_item(box, popover, _("Show in Explorer"), "Enter", MENU_REVEAL);
    if (!r->hit->is_dir) menu_item(box, popover, _("Open in Editor"), "Ctrl+Enter", MENU_OPEN);
    menu_item(box, popover, _("Copy Path"), NULL, MENU_COPY_PATH);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void list_free(gpointer data) {
    DsList* dl = data;
    g_object_unref(dl->selection); /* holds the sorted model, which holds the filtered one, then the store */
    g_object_unref(dl->store);
    g_free(dl->root);
    g_free(dl);
}

GtkWidget* deep_search_list_new(AppData* app, const DeepSearchListHandlers* handlers) {
    DsList* dl = g_new0(DsList, 1);
    dl->app = app;
    if (handlers != NULL) dl->handlers = *handlers;
    dl->store = g_list_store_new(DS_TYPE_ROW);

    dl->view = gtk_column_view_new(NULL);
    gtk_widget_set_name(dl->view, "deep-search-list");
    gtk_widget_add_css_class(dl->view, "data-table");
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(dl->view), FALSE);
    add_column(dl->view, _("Name"), COL_NAME, TRUE);
    add_column(dl->view, _("Folder"), COL_FOLDER, TRUE);
    add_column(dl->view, _("Size"), COL_SIZE, FALSE);
    add_column(dl->view, _("Last Change"), COL_DATE, FALSE);
    dl->matches = add_column(dl->view, _("Matches"), COL_MATCHES, FALSE);
    dl->line = add_column(dl->view, _("First Match"), COL_LINE, TRUE);
    gtk_column_view_column_set_visible(dl->matches, FALSE);
    gtk_column_view_column_set_visible(dl->line, FALSE);

    /* Unsorted: the order of the search, each folder before what is below */
    GtkSorter* sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(dl->view));
    dl->filter = gtk_custom_filter_new(in_range, dl, NULL);
    dl->filtered = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(dl->store)), GTK_FILTER(dl->filter));
    GtkSortListModel* sorted = gtk_sort_list_model_new(G_LIST_MODEL(dl->filtered), g_object_ref(sorter));
    gtk_sort_list_model_set_incremental(sorted, TRUE);
    dl->selection = gtk_single_selection_new(G_LIST_MODEL(sorted));
    gtk_single_selection_set_autoselect(dl->selection, TRUE);
    gtk_column_view_set_model(GTK_COLUMN_VIEW(dl->view), GTK_SELECTION_MODEL(dl->selection));
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

GtkWidget* deep_search_list_view(GtkWidget* list) {
    DsList* dl = list_state(list);
    return dl != NULL ? dl->view : NULL;
}

void deep_search_list_reset(GtkWidget* list, const gchar* root, gboolean content) {
    DsList* dl = list_state(list);
    if (dl == NULL) return;
    g_list_store_remove_all(dl->store);
    if (dl->ranged) {
        dl->ranged = FALSE;
        gtk_filter_changed(GTK_FILTER(dl->filter), GTK_FILTER_CHANGE_LESS_STRICT);
    }
    g_free(dl->root);
    dl->root = g_strdup(root);
    gtk_column_view_column_set_visible(dl->matches, content);
    gtk_column_view_column_set_visible(dl->line, content);
}

void deep_search_list_add(GtkWidget* list, GPtrArray* hits) {
    DsList* dl = list_state(list);
    if (hits == NULL) return;
    if (dl == NULL) {
        g_ptr_array_unref(hits);
        return;
    }
    GPtrArray* rows = g_ptr_array_new_full(hits->len, g_object_unref);
    g_ptr_array_set_free_func(hits, NULL); /* the rows take the hits */
    for (guint i = 0; i < hits->len; i++) g_ptr_array_add(rows, row_new(g_ptr_array_index(hits, i), dl->root));
    g_ptr_array_unref(hits);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(dl->store));
    g_list_store_splice(dl->store, n, 0, rows->pdata, rows->len);
    g_ptr_array_unref(rows);
}

guint deep_search_list_count(GtkWidget* list) {
    DsList* dl = list_state(list);
    return dl != NULL ? g_list_model_get_n_items(G_LIST_MODEL(dl->store)) : 0;
}

void deep_search_list_set_range(GtkWidget* list, const DeepSearchRange* range) {
    DsList* dl = list_state(list);
    if (dl == NULL || (!dl->ranged && range == NULL)) return;
    dl->ranged = range != NULL;
    if (range != NULL) dl->range = *range;
    gtk_filter_changed(GTK_FILTER(dl->filter), GTK_FILTER_CHANGE_DIFFERENT);
}

guint deep_search_list_shown(GtkWidget* list) {
    DsList* dl = list_state(list);
    return dl != NULL ? g_list_model_get_n_items(G_LIST_MODEL(dl->filtered)) : 0;
}
