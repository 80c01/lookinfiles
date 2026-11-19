/* Terminal panes of saved layouts and the exact arrangement put back after
 * a layout preview (declared in terminal.h; shares terminal_private.h) */
#include "terminal/terminal_private.h"
#include "widgets/panels.h"
#include "workspace/layout_store.h"

#ifdef HAVE_VTE

/* ------------------------------------------------------------------------- */
/* Pane tree of saved layouts (layout_store.h)                               */
/* ------------------------------------------------------------------------- */

static const gchar* PANED_FRAC_KEY = "term-paned-frac"; /* also set by panels.c */

static void describe_pane(GtkWidget* w, GString* out) {
    if (GTK_IS_NOTEBOOK(w)) {
        gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(w));
        g_string_append_c(out, 'T');
        if (n > 1) {
            g_string_append_printf(out, "%d", MIN(n, TERM_LAYOUT_MAX_TABS));
        }
        return;
    }
    if (!GTK_IS_PANED(w)) {
        return;
    }
    GtkWidget* a = gtk_paned_get_start_child(GTK_PANED(w));
    GtkWidget* b = gtk_paned_get_end_child(GTK_PANED(w));
    if (a == NULL || b == NULL) {
        describe_pane(a != NULL ? a : b, out);
        return;
    }
    GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(w));
    gdouble frac = 0.5;
    gdouble* saved = g_object_get_data(G_OBJECT(w), PANED_FRAC_KEY);
    gint len = (o == GTK_ORIENTATION_VERTICAL) ? gtk_widget_get_height(w) : gtk_widget_get_width(w);
    if (len > 40) {
        frac = (gdouble)gtk_paned_get_position(GTK_PANED(w)) / len;
    } else if (saved != NULL) {
        frac = *saved;
    }
    gchar num[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(num, sizeof num, "%.3f", CLAMP(frac, 0.05, 0.95));
    g_string_append_printf(out, "%c:%s(", o == GTK_ORIENTATION_VERTICAL ? 'V' : 'H', num);
    describe_pane(a, out);
    g_string_append_c(out, ',');
    describe_pane(b, out);
    g_string_append_c(out, ')');
}

gchar* term_layout_capture(AppData* app) {
    return term_layout_capture_box(term_current_box(app));
}

gchar* term_layout_capture_box(GtkWidget* term_box) {
    GString* out = g_string_new(NULL);
    GtkWidget* root = (term_box != NULL) ? gtk_widget_get_first_child(term_box) : NULL;
    if (root != NULL) {
        describe_pane(root, out);
    }
    return g_string_free(out, FALSE);
}

guint term_pane_count(AppData* app) {
    GPtrArray* panes = term_all_panes(app);
    guint n = panes->len;
    g_ptr_array_unref(panes);
    return n;
}

static GtkWidget* build_panes(AppData* app, const TermLayoutNode* node, GPtrArray* existing,
                              guint* next) {
    if (node->is_terminal) {
        GtkWidget* nb = term_notebook_new(app);
        for (guint i = 0; i < MAX(node->tabs, 1); i++) {
            GtkWidget* vte = (*next < existing->len) ? g_ptr_array_index(existing, (*next)++)
                                                     : term_new_pane(app, NULL);
            term_notebook_add(GTK_NOTEBOOK(nb), vte, NULL, -1);
        }
        gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), 0);
        return nb;
    }
    GtkWidget* paned = gtk_paned_new(node->vertical ? GTK_ORIENTATION_VERTICAL
                                                    : GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    wire_paned_controllers(paned, app);
    gdouble* frac = g_new(gdouble, 1);
    *frac = node->frac;
    g_object_set_data_full(G_OBJECT(paned), PANED_FRAC_KEY, frac, g_free);
    gtk_paned_set_start_child(GTK_PANED(paned), build_panes(app, node->first, existing, next));
    gtk_paned_set_end_child(GTK_PANED(paned), build_panes(app, node->second, existing, next));
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), TRUE);
    return paned;
}

/* Take every terminal of the panel out of its notebook (referenced, in
 * order) and empty the panel */
static GPtrArray* take_all_out(AppData* app) {
    GPtrArray* taken = g_ptr_array_new_with_free_func(g_object_unref);
    GPtrArray* panes = term_all_panes(app);
    for (guint i = 0; i < panes->len; i++) {
        GtkWidget* vte = terminal_detach_tab(g_ptr_array_index(panes, i));
        if (vte != NULL) {
            g_ptr_array_add(taken, vte);
        }
    }
    g_ptr_array_unref(panes);
    for (GtkWidget* c = gtk_widget_get_first_child(term_current_box(app)); c != NULL;
         c = gtk_widget_get_first_child(term_current_box(app))) {
        gtk_box_remove(GTK_BOX(term_current_box(app)), c);
    }
    return taken;
}

/* Terminals not placed again: kept in spare, else closed with their widget */
static void set_aside(AppData* app, GPtrArray* left, GPtrArray* spare) {
    for (guint i = 0; i < left->len; i++) {
        GtkWidget* vte = g_ptr_array_index(left, i);
        terminal_forget(app, vte);
        if (spare != NULL) {
            g_ptr_array_add(spare, g_object_ref(vte));
        }
    }
}

/* Running terminals the arrangement has no place for become the last tabs
 * of the last pane (a new pane if there is none): nothing closes */
static void stack_left(AppData* app, GPtrArray* left) {
    if (left->len == 0) {
        return;
    }
    GPtrArray* panes = term_all_panes(app);
    GtkNotebook* nb = (panes->len > 0) ? term_notebook_of(g_ptr_array_index(panes, panes->len - 1)) : NULL;
    g_ptr_array_unref(panes);
    if (nb == NULL) {
        nb = GTK_NOTEBOOK(term_notebook_new(app));
        gtk_box_append(GTK_BOX(term_current_box(app)), GTK_WIDGET(nb));
    }
    gint current = gtk_notebook_get_current_page(nb);
    for (guint i = 0; i < left->len; i++) {
        term_notebook_add(nb, g_ptr_array_index(left, i), NULL, -1);
    }
    if (current >= 0) {
        gtk_notebook_set_current_page(nb, current); /* the arranged tab stays in front */
    }
    g_ptr_array_set_size(left, 0);
}

void term_layout_apply_spare(AppData* app, const gchar* text, GPtrArray* spare) {
    TermLayoutNode* tree = term_layout_parse(text);
    if (tree == NULL || term_current_box(app) == NULL) {
        term_layout_free(tree);
        return;
    }
    /* The running terminals in order, then those set aside before */
    GPtrArray* existing = take_all_out(app);
    guint running = existing->len;
    while (spare != NULL && spare->len > 0) {
        g_ptr_array_add(existing, g_ptr_array_steal_index(spare, 0));
    }
    guint next = 0;
    gtk_box_append(GTK_BOX(term_current_box(app)), build_panes(app, tree, existing, &next));
    /* Running terminals beyond the tree are stacked, those of spare go back */
    GPtrArray* left = g_ptr_array_new_with_free_func(g_object_unref);
    for (guint i = next; i < running; i++) {
        g_ptr_array_add(left, g_object_ref(g_ptr_array_index(existing, i)));
    }
    stack_left(app, left);
    g_ptr_array_unref(left);
    g_ptr_array_remove_range(existing, 0, MAX(next, running));
    set_aside(app, existing, spare);
    g_ptr_array_unref(existing);
    term_layout_free(tree);
}

void term_layout_apply(AppData* app, const gchar* text) {
    term_layout_apply_spare(app, text, NULL);
}

GtkWidget* terminal_detach_tab(GtkWidget* vte) {
    GtkNotebook* nb = (vte != NULL) ? term_notebook_of(vte) : NULL;
    if (nb == NULL) {
        return NULL;
    }
    g_object_ref(vte);
    if (gtk_notebook_get_tab_detachable(nb, vte)) {
        gtk_notebook_detach_tab(nb, vte);
    } else {
        gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, vte));
    }
    return vte;
}

void terminal_attach_tab(GtkWidget* vte, GtkNotebook* nb) {
    term_notebook_add(nb, vte, NULL, -1);
}

/* ------------------------------------------------------------------------- */
/* Exact arrangement of the panel, put back after a layout preview           */
/* ------------------------------------------------------------------------- */

struct TermSnapshot {
    gboolean is_pane;
    GPtrArray* terms; /* pane: its terminals in tab order (referenced) */
    gint current;     /* pane: current tab */
    gboolean vertical;
    gdouble frac;
    TermSnapshot* first;
    TermSnapshot* second;
};

static TermSnapshot* snapshot_of(GtkWidget* w) {
    if (GTK_IS_NOTEBOOK(w)) {
        GtkNotebook* nb = GTK_NOTEBOOK(w);
        gint n = gtk_notebook_get_n_pages(nb);
        if (n == 0) {
            return NULL;
        }
        TermSnapshot* snap = g_new0(TermSnapshot, 1);
        snap->is_pane = TRUE;
        snap->terms = g_ptr_array_new_with_free_func(g_object_unref);
        for (gint p = 0; p < n; p++) {
            g_ptr_array_add(snap->terms, g_object_ref(gtk_notebook_get_nth_page(nb, p)));
        }
        snap->current = gtk_notebook_get_current_page(nb);
        return snap;
    }
    if (!GTK_IS_PANED(w)) {
        return NULL;
    }
    TermSnapshot* a = snapshot_of(gtk_paned_get_start_child(GTK_PANED(w)));
    TermSnapshot* b = snapshot_of(gtk_paned_get_end_child(GTK_PANED(w)));
    if (a == NULL || b == NULL) {
        return a != NULL ? a : b;
    }
    TermSnapshot* snap = g_new0(TermSnapshot, 1);
    snap->vertical = gtk_orientable_get_orientation(GTK_ORIENTABLE(w)) == GTK_ORIENTATION_VERTICAL;
    gint len = snap->vertical ? gtk_widget_get_height(w) : gtk_widget_get_width(w);
    gdouble* saved = g_object_get_data(G_OBJECT(w), PANED_FRAC_KEY);
    snap->frac = (len > 40) ? (gdouble)gtk_paned_get_position(GTK_PANED(w)) / len
               : (saved != NULL) ? *saved : 0.5;
    snap->first = a;
    snap->second = b;
    return snap;
}

TermSnapshot* term_snapshot_take(AppData* app) {
    GtkWidget* root = (term_current_box(app) != NULL) ? gtk_widget_get_first_child(term_current_box(app)) : NULL;
    return (root != NULL) ? snapshot_of(root) : NULL;
}

void term_snapshot_free(TermSnapshot* snap) {
    if (snap == NULL) {
        return;
    }
    if (snap->terms != NULL) {
        g_ptr_array_unref(snap->terms);
    }
    term_snapshot_free(snap->first);
    term_snapshot_free(snap->second);
    g_free(snap);
}

/* Take vte from pool (the terminals out of their notebooks); FALSE if absent */
static gboolean take_from(GPtrArray* pool, GtkWidget* vte) {
    guint i = 0;
    if (pool == NULL || !g_ptr_array_find(pool, vte, &i)) {
        return FALSE;
    }
    g_object_unref(g_ptr_array_steal_index(pool, i)); /* the notebook holds it now */
    return TRUE;
}

static GtkWidget* rebuild_snapshot(AppData* app, const TermSnapshot* snap, GPtrArray* taken,
                                   GPtrArray* spare) {
    if (snap->is_pane) {
        GtkWidget* nb = term_notebook_new(app);
        for (guint i = 0; i < snap->terms->len; i++) {
            GtkWidget* vte = g_ptr_array_index(snap->terms, i);
            g_object_ref(vte);
            if (take_from(taken, vte) || take_from(spare, vte)) {
                term_notebook_add(GTK_NOTEBOOK(nb), vte, NULL, -1);
            }
            g_object_unref(vte);
        }
        gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb));
        if (n == 0) {
            g_object_ref_sink(nb);
            g_object_unref(nb);
            return NULL; /* its shells exited meanwhile */
        }
        gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), MIN(snap->current, n - 1));
        return nb;
    }
    GtkWidget* a = rebuild_snapshot(app, snap->first, taken, spare);
    GtkWidget* b = rebuild_snapshot(app, snap->second, taken, spare);
    if (a == NULL || b == NULL) {
        return a != NULL ? a : b;
    }
    GtkWidget* paned = gtk_paned_new(snap->vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    wire_paned_controllers(paned, app);
    gdouble* frac = g_new(gdouble, 1);
    *frac = snap->frac;
    g_object_set_data_full(G_OBJECT(paned), PANED_FRAC_KEY, frac, g_free);
    gtk_paned_set_start_child(GTK_PANED(paned), a);
    gtk_paned_set_end_child(GTK_PANED(paned), b);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), TRUE);
    return paned;
}

void term_snapshot_restore(AppData* app, const TermSnapshot* snap, GPtrArray* spare) {
    if (term_current_box(app) == NULL) {
        return;
    }
    GPtrArray* taken = take_all_out(app);
    GtkWidget* root = (snap != NULL) ? rebuild_snapshot(app, snap, taken, spare) : NULL;
    if (root != NULL) {
        gtk_box_append(GTK_BOX(term_current_box(app)), root);
    }
    if (spare != NULL) {
        set_aside(app, taken, spare);
    } else {
        stack_left(app, taken); /* terminals opened since the snapshot */
    }
    g_ptr_array_unref(taken);
}

#endif /* HAVE_VTE */
