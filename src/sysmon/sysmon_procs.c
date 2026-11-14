/* The process list of the System Monitor tab (see sysmon_procs.h) */
#include "sysmon/sysmon_procs.h"
#include "core/i18n.h"
#include "widgets/focus_route.h"

#define PROCS_KEY "lookin-sysmon-procs"

typedef enum {
    COL_NAME,
    COL_PID,
    COL_USER,
    COL_CPU,
    COL_MEM,
    COL_THREADS,
    COL_STATE,
} ProcColumn;

/* One process, as shown */
#define SYSMON_TYPE_PROC (sysmon_proc_get_type())
G_DECLARE_FINAL_TYPE(SysmonProc, sysmon_proc, SYSMON, PROC, GObject)

struct _SysmonProc {
    GObject parent;
    gint pid;
    gchar* name;
    gchar* user;
    gdouble cpu;
    guint64 rss;
    guint threads;
    gchar state;
    guint seen; /* generation of the last update that listed it */
};

G_DEFINE_FINAL_TYPE(SysmonProc, sysmon_proc, G_TYPE_OBJECT)

static void sysmon_proc_finalize(GObject* obj) {
    SysmonProc* p = SYSMON_PROC(obj);
    g_free(p->name);
    g_free(p->user);
    G_OBJECT_CLASS(sysmon_proc_parent_class)->finalize(obj);
}

static guint changed_signal;

static void sysmon_proc_class_init(SysmonProcClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = sysmon_proc_finalize;
    /* A value of the row changed: its cells show it again */
    changed_signal = g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

static void sysmon_proc_init(SysmonProc* p) {
    (void)p;
}

typedef struct {
    GListStore* store;
    GHashTable* by_pid; /* pid -> SysmonProc in the store (borrowed) */
    GtkSingleSelection* selection;
    GtkWidget* view;
    guint64 page_size;
    guint generation;
} Procs;

#define COLUMN_KEY "lookin-sysmon-column"
#define HANDLER_KEY "lookin-sysmon-handler"

static const gchar* state_name(gchar state) {
    switch (state) {
    case 'R': return _("Running");
    case 'S': return _("Sleeping");
    case 'D': return _("Waiting");
    case 'Z': return _("Zombie");
    case 'T':
    case 't': return _("Stopped");
    case 'I': return _("Idle");
    default: return "?";
    }
}

/* Text of a cell */
static gchar* cell_text(SysmonProc* p, ProcColumn col) {
    switch (col) {
    case COL_NAME: return g_strdup(p->name);
    case COL_PID: return g_strdup_printf("%d", p->pid);
    case COL_USER: return g_strdup(p->user);
    case COL_CPU: return g_strdup_printf("%.1f", p->cpu);
    case COL_MEM: return g_format_size(p->rss);
    case COL_THREADS: return g_strdup_printf("%u", p->threads);
    case COL_STATE: return g_strdup(state_name(p->state));
    }
    return g_strdup("");
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    ProcColumn col = GPOINTER_TO_INT(data);
    GtkWidget* label = gtk_label_new(NULL);
    gboolean numeric = col == COL_PID || col == COL_CPU || col == COL_MEM || col == COL_THREADS;
    gtk_label_set_xalign(GTK_LABEL(label), numeric ? 1.0f : 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    if (numeric) gtk_widget_add_css_class(label, "sysmon-num");
    g_object_set_data(G_OBJECT(label), COLUMN_KEY, data);
    gtk_list_item_set_child(item, label);
}

/* The text of a cell, set only when it differs (no relayout otherwise) */
static void show_cell(SysmonProc* p, GtkWidget* label) {
    gchar* text = cell_text(p, (ProcColumn)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(label), COLUMN_KEY)));
    if (g_strcmp0(gtk_label_get_text(GTK_LABEL(label)), text) != 0) gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
}

static void on_proc_changed(SysmonProc* p, gpointer label) {
    show_cell(p, label);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    SysmonProc* p = gtk_list_item_get_item(item);
    GtkWidget* label = gtk_list_item_get_child(item);
    show_cell(p, label);
    gulong id = g_signal_connect(p, "changed", G_CALLBACK(on_proc_changed), label);
    g_object_set_data(G_OBJECT(label), HANDLER_KEY, GSIZE_TO_POINTER(id));
}

static void on_unbind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    SysmonProc* p = gtk_list_item_get_item(item);
    GtkWidget* label = gtk_list_item_get_child(item);
    gulong id = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(label), HANDLER_KEY));
    if (p != NULL && id != 0) g_signal_handler_disconnect(p, id);
    g_object_set_data(G_OBJECT(label), HANDLER_KEY, NULL);
}

static gint cmp_num(gdouble a, gdouble b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

static gint compare_column(const SysmonProc* p, const SysmonProc* q, ProcColumn col) {
    switch (col) {
    case COL_NAME: return g_utf8_collate(p->name, q->name);
    case COL_PID: return cmp_num(p->pid, q->pid);
    case COL_USER: return g_utf8_collate(p->user, q->user);
    case COL_CPU: {
        gint c = cmp_num(p->cpu, q->cpu);
        return c != 0 ? c : cmp_num((gdouble)p->rss, (gdouble)q->rss);
    }
    case COL_MEM: return cmp_num((gdouble)p->rss, (gdouble)q->rss);
    case COL_THREADS: return cmp_num(p->threads, q->threads);
    case COL_STATE: return cmp_num(p->state, q->state);
    }
    return 0;
}

/* Ties by PID: a stable order, rows do not swap places at each update */
static gint compare(gconstpointer a, gconstpointer b, gpointer data) {
    const SysmonProc* p = a;
    const SysmonProc* q = b;
    gint c = compare_column(p, q, (ProcColumn)GPOINTER_TO_INT(data));
    return c != 0 ? c : cmp_num(p->pid, q->pid);
}

static GtkColumnViewColumn* add_column(GtkWidget* view, const gchar* title, ProcColumn col, gboolean expand) {
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), GINT_TO_POINTER(col));
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), GINT_TO_POINTER(col));
    g_signal_connect(factory, "unbind", G_CALLBACK(on_unbind), GINT_TO_POINTER(col));
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

static void procs_free(gpointer data) {
    Procs* ps = data;
    g_object_unref(ps->selection); /* holds the sorted model, which holds the store */
    g_object_unref(ps->store);
    g_hash_table_unref(ps->by_pid);
    g_free(ps);
}

GtkWidget* sysmon_procs_new(AppData* app) {
    Procs* ps = g_new0(Procs, 1);
    ps->page_size = sysstat_page_size();
    ps->store = g_list_store_new(SYSMON_TYPE_PROC);
    ps->by_pid = g_hash_table_new(g_direct_hash, g_direct_equal);

    ps->view = gtk_column_view_new(NULL);
    gtk_widget_set_name(ps->view, "sysmon-procs");
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(ps->view), FALSE);
    add_column(ps->view, _("Process"), COL_NAME, TRUE);
    add_column(ps->view, _("PID"), COL_PID, FALSE);
    add_column(ps->view, _("User"), COL_USER, FALSE);
    GtkColumnViewColumn* cpu = add_column(ps->view, _("CPU %"), COL_CPU, FALSE);
    add_column(ps->view, _("Memory"), COL_MEM, FALSE);
    add_column(ps->view, _("Threads"), COL_THREADS, FALSE);
    add_column(ps->view, _("State"), COL_STATE, FALSE);

    GtkSorter* sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(ps->view));
    GtkSortListModel* sorted = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(ps->store)), g_object_ref(sorter));
    ps->selection = gtk_single_selection_new(G_LIST_MODEL(sorted));
    gtk_single_selection_set_autoselect(ps->selection, FALSE);
    gtk_single_selection_set_can_unselect(ps->selection, TRUE);
    gtk_column_view_set_model(GTK_COLUMN_VIEW(ps->view), GTK_SELECTION_MODEL(ps->selection));
    gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(ps->view), cpu, GTK_SORT_DESCENDING);
    focus_route_attach_list(app, ps->view, ps->selection);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), ps->view);
    g_object_set_data_full(G_OBJECT(scroll), PROCS_KEY, ps, procs_free);
    return scroll;
}

GtkWidget* sysmon_procs_view(GtkWidget* procs) {
    Procs* ps = procs != NULL ? g_object_get_data(G_OBJECT(procs), PROCS_KEY) : NULL;
    return ps != NULL ? ps->view : NULL;
}

/* Copy a reading into a row: TRUE when a value changed */
static gboolean fill_proc(SysmonProc* p, const SysProc* sp, guint64 page_size) {
    const gchar* name = sp->name != NULL ? sp->name : "";
    const gchar* user = sp->user != NULL ? sp->user : "";
    guint64 rss = sp->rss_pages * page_size;
    gboolean changed = FALSE;
    if (g_strcmp0(p->name, name) != 0) {
        g_free(p->name);
        p->name = g_strdup(name);
        changed = TRUE;
    }
    if (g_strcmp0(p->user, user) != 0) {
        g_free(p->user);
        p->user = g_strdup(user);
        changed = TRUE;
    }
    if (p->cpu != sp->cpu_percent || p->rss != rss || p->threads != sp->threads || p->state != sp->state) {
        p->cpu = sp->cpu_percent;
        p->rss = rss;
        p->threads = sp->threads;
        p->state = sp->state;
        changed = TRUE;
    }
    return changed;
}

/* Is the row at pos of the sorted list in order with its neighbours? */
static gboolean in_order(GListModel* sorted, GtkSorter* sorter, guint pos, gpointer item) {
    guint n = g_list_model_get_n_items(sorted);
    gboolean ok = TRUE;
    if (pos > 0) {
        gpointer prev = g_list_model_get_item(sorted, pos - 1);
        ok = gtk_sorter_compare(sorter, prev, item) != GTK_ORDERING_LARGER;
        g_object_unref(prev);
    }
    if (ok && pos + 1 < n) {
        gpointer next = g_list_model_get_item(sorted, pos + 1);
        ok = gtk_sorter_compare(sorter, item, next) != GTK_ORDERING_LARGER;
        g_object_unref(next);
    }
    return ok;
}

/* Move the changed rows whose place changed, and only them: each one is
 * replaced by itself in the store, which the sorted list inserts at its new
 * place (a resort of the whole list would rebuild every row widget, the
 * rows that did not move too). Unchanged rows stay in order between them,
 * so the list is sorted once every changed row is in order with its
 * neighbours; a few passes, then a resort if it still is not. */
static void reorder(Procs* ps, GPtrArray* updated) {
    if (updated->len == 0) return;
    GtkSorter* sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(ps->view));
    GListModel* sorted = gtk_single_selection_get_model(ps->selection);
    GHashTable* changed = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (guint i = 0; i < updated->len; i++) g_hash_table_add(changed, g_ptr_array_index(updated, i));
    GPtrArray* moving = g_ptr_array_new();
    for (guint pass = 0; pass < 4; pass++) {
        g_ptr_array_set_size(moving, 0);
        guint n = g_list_model_get_n_items(sorted);
        for (guint pos = 0; pos < n; pos++) {
            gpointer item = g_list_model_get_item(sorted, pos);
            if (g_hash_table_contains(changed, item) && !in_order(sorted, sorter, pos, item)) g_ptr_array_add(moving, item);
            g_object_unref(item);
        }
        for (guint i = 0; i < moving->len; i++) {
            gpointer item = g_ptr_array_index(moving, i);
            guint pos;
            if (g_list_store_find(ps->store, item, &pos)) g_list_store_splice(ps->store, pos, 1, &item, 1);
        }
        if (moving->len == 0) break;
    }
    if (moving->len > 0) gtk_sorter_changed(sorter, GTK_SORTER_CHANGE_DIFFERENT);
    g_ptr_array_unref(moving);
    g_hash_table_unref(changed);
}

/* The rows stay from one update to the next (found by PID): only the cells
 * whose text changes are set, and the sorted list moves only the rows whose
 * place changes (the selection and the scroll position follow) */
void sysmon_procs_update(GtkWidget* procs, GPtrArray* list) {
    Procs* ps = procs != NULL ? g_object_get_data(G_OBJECT(procs), PROCS_KEY) : NULL;
    if (ps == NULL || list == NULL) return;
    guint gen = ++ps->generation;

    GPtrArray* added = g_ptr_array_new_with_free_func(g_object_unref);
    GPtrArray* updated = g_ptr_array_new();
    for (guint i = 0; i < list->len; i++) {
        const SysProc* sp = g_ptr_array_index(list, i);
        SysmonProc* p = g_hash_table_lookup(ps->by_pid, GINT_TO_POINTER(sp->pid));
        if (p != NULL && p->seen == gen) continue; /* listed twice */
        if (p == NULL) {
            p = g_object_new(SYSMON_TYPE_PROC, NULL);
            p->pid = sp->pid;
            fill_proc(p, sp, ps->page_size);
            g_hash_table_insert(ps->by_pid, GINT_TO_POINTER(p->pid), p);
            g_ptr_array_add(added, p);
        } else if (fill_proc(p, sp, ps->page_size)) {
            g_ptr_array_add(updated, p);
        }
        p->seen = gen;
    }

    /* The processes gone, then the new ones, a row at a time: rows removed
     * or added together would be scattered in the sorted list, which would
     * report one change spanning them all and rebuild every row between */
    GListModel* model = G_LIST_MODEL(ps->store);
    for (guint i = g_list_model_get_n_items(model); i > 0; i--) {
        SysmonProc* p = g_list_model_get_item(model, i - 1);
        if (p->seen != gen) {
            g_hash_table_remove(ps->by_pid, GINT_TO_POINTER(p->pid));
            g_list_store_remove(ps->store, i - 1);
        }
        g_object_unref(p);
    }
    if (g_list_model_get_n_items(model) == 0) {
        g_list_store_splice(ps->store, 0, 0, added->pdata, added->len); /* first reading: in one go */
    } else {
        for (guint i = 0; i < added->len; i++) g_list_store_append(ps->store, g_ptr_array_index(added, i));
    }
    g_ptr_array_unref(added);

    /* New values: the cells shown, then the order */
    for (guint i = 0; i < updated->len; i++) g_signal_emit(g_ptr_array_index(updated, i), changed_signal, 0);
    reorder(ps, updated);
    g_ptr_array_unref(updated);
}
