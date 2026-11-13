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
};

G_DEFINE_FINAL_TYPE(SysmonProc, sysmon_proc, G_TYPE_OBJECT)

static void sysmon_proc_finalize(GObject* obj) {
    SysmonProc* p = SYSMON_PROC(obj);
    g_free(p->name);
    g_free(p->user);
    G_OBJECT_CLASS(sysmon_proc_parent_class)->finalize(obj);
}

static void sysmon_proc_class_init(SysmonProcClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = sysmon_proc_finalize;
}

static void sysmon_proc_init(SysmonProc* p) {
    (void)p;
}

typedef struct {
    GListStore* store;
    GtkSingleSelection* selection;
    GtkWidget* view;
    guint64 page_size;
} Procs;

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
    gtk_list_item_set_child(item, label);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    SysmonProc* p = gtk_list_item_get_item(item);
    gchar* text = cell_text(p, (ProcColumn)GPOINTER_TO_INT(data));
    gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)), text);
    g_free(text);
}

static gint cmp_num(gdouble a, gdouble b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

static gint compare(gconstpointer a, gconstpointer b, gpointer data) {
    const SysmonProc* p = a;
    const SysmonProc* q = b;
    switch ((ProcColumn)GPOINTER_TO_INT(data)) {
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

static GtkColumnViewColumn* add_column(GtkWidget* view, const gchar* title, ProcColumn col, gboolean expand) {
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

static void procs_free(gpointer data) {
    Procs* ps = data;
    g_object_unref(ps->selection); /* holds the sorted model, which holds the store */
    g_object_unref(ps->store);
    g_free(ps);
}

GtkWidget* sysmon_procs_new(AppData* app) {
    Procs* ps = g_new0(Procs, 1);
    ps->page_size = sysstat_page_size();
    ps->store = g_list_store_new(SYSMON_TYPE_PROC);

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

void sysmon_procs_update(GtkWidget* procs, GPtrArray* list) {
    Procs* ps = procs != NULL ? g_object_get_data(G_OBJECT(procs), PROCS_KEY) : NULL;
    if (ps == NULL || list == NULL) return;

    /* The selected process, found again after the rows change */
    gint selected_pid = -1;
    SysmonProc* sel = gtk_single_selection_get_selected_item(ps->selection);
    if (sel != NULL) selected_pid = sel->pid;

    GPtrArray* rows = g_ptr_array_new_with_free_func(g_object_unref);
    for (guint i = 0; i < list->len; i++) {
        const SysProc* sp = g_ptr_array_index(list, i);
        SysmonProc* p = g_object_new(SYSMON_TYPE_PROC, NULL);
        p->pid = sp->pid;
        p->name = g_strdup(sp->name != NULL ? sp->name : "");
        p->user = g_strdup(sp->user != NULL ? sp->user : "");
        p->cpu = sp->cpu_percent;
        p->rss = sp->rss_pages * ps->page_size;
        p->threads = sp->threads;
        p->state = sp->state;
        g_ptr_array_add(rows, p);
    }
    g_list_store_splice(ps->store, 0, g_list_model_get_n_items(G_LIST_MODEL(ps->store)), rows->pdata, rows->len);
    g_ptr_array_unref(rows);

    guint pos = GTK_INVALID_LIST_POSITION;
    if (selected_pid >= 0) {
        GListModel* model = G_LIST_MODEL(ps->selection);
        guint n = g_list_model_get_n_items(model);
        for (guint i = 0; i < n && pos == GTK_INVALID_LIST_POSITION; i++) {
            SysmonProc* p = g_list_model_get_item(model, i);
            if (p->pid == selected_pid) pos = i;
            g_object_unref(p);
        }
    }
    gtk_single_selection_set_selected(ps->selection, pos);
}
