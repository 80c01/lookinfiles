/* The shape of the dedicated editor panel in saved layouts (see editor_layout.h) */
#include "editor_layout.h"
#include "editor.h"
#include "layout_store.h"
#include "tab_tree.h"
#include "terminal.h"

/* Share of a split's first child, kept on the GtkPaned and applied by
 * workspace.c (as for the session and the terminal panes) */
static const gchar* PANED_FRAC_KEY = "term-paned-frac";

static EditorTab* tab_of_page(AppData* app, GtkWidget* child) {
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->container == child) {
            return t;
        }
    }
    return NULL;
}

static gboolean is_terminal(GtkWidget* page) {
#ifdef HAVE_VTE
    return VTE_IS_TERMINAL(page);
#else
    (void)page;
    return FALSE;
#endif
}

/* ------------------------------------------------------------------------- */
/* Capture                                                                   */
/* ------------------------------------------------------------------------- */

/* "[0,t...]": the files marker when it holds files, a "t" per terminal tab;
 * NULL when it holds neither */
static TabTreeNode* capture_notebook(AppData* app, GtkNotebook* nb) {
    TabTreeNode* node = tab_tree_notebook_new();
    gboolean files = FALSE;
    for (gint p = 0; p < gtk_notebook_get_n_pages(nb); p++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
        if (is_terminal(page)) {
            tab_tree_notebook_add(node, TAB_TREE_TERMINAL);
        } else if (tab_of_page(app, page) != NULL) {
            files = TRUE;
        }
    }
    if (files) {
        guint marker = LAYOUT_EDITOR_FILES;
        g_array_prepend_val(node->tabs, marker);
    }
    if (node->tabs->len == 0) {
        tab_tree_free(node);
        return NULL;
    }
    return node;
}

static gdouble paned_frac(GtkWidget* paned) {
    GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(paned));
    gint len = (o == GTK_ORIENTATION_VERTICAL) ? gtk_widget_get_height(paned) : gtk_widget_get_width(paned);
    gdouble* saved = g_object_get_data(G_OBJECT(paned), PANED_FRAC_KEY);
    if (len > 40) {
        return (gdouble)gtk_paned_get_position(GTK_PANED(paned)) / len;
    }
    return (saved != NULL) ? *saved : 0.5;
}

static TabTreeNode* capture_widget(AppData* app, GtkWidget* w) {
    if (w == NULL) {
        return NULL;
    }
    if (GTK_IS_NOTEBOOK(w)) {
        return capture_notebook(app, GTK_NOTEBOOK(w));
    }
    if (!GTK_IS_PANED(w)) {
        return NULL;
    }
    TabTreeNode* first = capture_widget(app, gtk_paned_get_start_child(GTK_PANED(w)));
    TabTreeNode* second = capture_widget(app, gtk_paned_get_end_child(GTK_PANED(w)));
    gboolean vertical = gtk_orientable_get_orientation(GTK_ORIENTABLE(w)) == GTK_ORIENTATION_VERTICAL;
    return tab_tree_split_new(vertical, paned_frac(w), first, second);
}

gchar* editor_layout_capture(AppData* app) {
    if (app == NULL || !app->editor_dedicated || app->editor_box == NULL) {
        return g_strdup("");
    }
    TabTreeNode* tree = capture_widget(app, gtk_widget_get_first_child(app->editor_box));
    gchar* text = tab_tree_to_string(tree);
    tab_tree_free(tree);
    return text;
}

guint editor_layout_terminal_count(AppData* app) {
    if (app == NULL) {
        return 0;
    }
    guint n = 0;
    GPtrArray* nbs = editor_get_all_notebooks(app);
    for (guint i = 0; i < nbs->len; i++) {
        GtkNotebook* nb = g_ptr_array_index(nbs, i);
        for (gint p = 0; p < gtk_notebook_get_n_pages(nb); p++) {
            n += is_terminal(gtk_notebook_get_nth_page(nb, p)) ? 1 : 0;
        }
    }
    g_ptr_array_unref(nbs);
    return n;
}

guint editor_layout_wanted_terminals(const gchar* text) {
    TabTreeNode* tree = tab_tree_parse(text);
    guint n = tab_tree_terminal_count(tree);
    tab_tree_free(tree);
    return n;
}

/* ------------------------------------------------------------------------- */
/* Apply                                                                     */
/* ------------------------------------------------------------------------- */

/* The first notebook of the panel (a new one when the panel is empty) */
static GtkNotebook* panel_root(AppData* app) {
    GPtrArray* nbs = editor_get_all_notebooks(app);
    GtkNotebook* nb = (nbs->len > 0) ? g_ptr_array_index(nbs, 0) : NULL;
    g_ptr_array_unref(nbs);
    if (nb == NULL && gtk_widget_get_first_child(app->editor_box) == NULL) {
        GtkWidget* w = editor_notebook_new(app);
        gtk_box_append(GTK_BOX(app->editor_box), w);
        app->editor_notebook = w;
        nb = GTK_NOTEBOOK(w);
    }
    return nb;
}

/* Split nb as node describes; leaves gets the notebooks, nodes their descriptions */
static void build(AppData* app, const TabTreeNode* node, GtkNotebook* nb, GPtrArray* leaves, GPtrArray* nodes) {
    if (node->is_notebook) {
        g_ptr_array_add(leaves, nb);
        g_ptr_array_add(nodes, (gpointer)node);
        return;
    }
    GtkWidget* second = editor_notebook_split(app, nb, node->vertical);
    if (second == NULL) {
        build(app, node->first, nb, leaves, nodes);
        return;
    }
    gdouble* frac = g_new(gdouble, 1);
    *frac = node->frac;
    g_object_set_data_full(G_OBJECT(gtk_widget_get_parent(second)), PANED_FRAC_KEY, frac, g_free);
    build(app, node->first, nb, leaves, nodes);
    build(app, node->second, GTK_NOTEBOOK(second), leaves, nodes);
}

static gboolean holds_files(const TabTreeNode* node) {
    for (guint i = 0; i < node->tabs->len; i++) {
        if (g_array_index(node->tabs, guint, i) != TAB_TREE_TERMINAL) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Which files pane (index in the panes of files) each file tab goes to: its
 * source pane's rank, the last pane beyond; then each empty pane takes the
 * last tab, not a current page when possible, of the fullest pane */
static guint* assign_files(GPtrArray* files, GArray* ranks, guint n_panes, GPtrArray* currents) {
    guint* target = g_new(guint, MAX(files->len, 1));
    guint* count = g_new0(guint, n_panes);
    for (guint k = 0; k < files->len; k++) {
        target[k] = MIN(g_array_index(ranks, guint, k), n_panes - 1);
        count[target[k]]++;
    }
    for (guint j = 0; j < n_panes; j++) {
        if (count[j] > 0) {
            continue;
        }
        guint m = 0;
        for (guint i = 1; i < n_panes; i++) {
            m = (count[i] > count[m]) ? i : m;
        }
        if (count[m] < 2) {
            break;
        }
        gint pick = -1;
        for (guint k = files->len; k-- > 0;) {
            if (target[k] != m) {
                continue;
            }
            EditorTab* tab = g_ptr_array_index(files, k);
            if (pick < 0) {
                pick = (gint)k;
            }
            if (!g_ptr_array_find(currents, tab->container, NULL)) {
                pick = (gint)k;
                break;
            }
        }
        target[pick] = j;
        count[m]--;
        count[j]++;
    }
    g_free(count);
    return target;
}

/* Gather every tab of the panel in root, split it as tree describes and
 * place the tabs (see editor_layout_apply()) */
static void rebuild(AppData* app, const TabTreeNode* tree, GtkNotebook* root, GPtrArray* files, GArray* ranks,
                    GPtrArray* terms, GPtrArray* currents, EditorTab* focused) {
#ifdef HAVE_VTE
    guint wanted = tab_tree_terminal_count(tree);
#else
    (void)terms;
#endif

    /* Every tab into the first pane, the other panes removed, then split it */
    for (guint k = 0; k < files->len; k++) {
        EditorTab* tab = g_ptr_array_index(files, k);
        if (tab->nb != GTK_WIDGET(root)) {
            editor_move_tab_to(app, tab, root, -1, FALSE);
        }
    }
#ifdef HAVE_VTE
    for (guint i = 0; i < terms->len; i++) {
        terminal_move_tab(g_ptr_array_index(terms, i), root);
    }
#endif
    if (gtk_notebook_get_n_pages(root) > 0) {
        editor_remove_empty_notebooks(app);
    }
    GPtrArray* leaves = g_ptr_array_new();
    GPtrArray* nodes = g_ptr_array_new();
    build(app, tree, root, leaves, nodes);

    /* The file tabs to the panes of files */
    GArray* file_panes = g_array_new(FALSE, FALSE, sizeof(guint));
    for (guint i = 0; i < leaves->len; i++) {
        if (holds_files(g_ptr_array_index(nodes, i))) {
            g_array_append_val(file_panes, i);
        }
    }
    if (file_panes->len == 0) {
        guint first = 0;
        g_array_append_val(file_panes, first);
    }
    guint* target = assign_files(files, ranks, file_panes->len, currents);
    for (guint k = 0; k < files->len; k++) {
        EditorTab* tab = g_ptr_array_index(files, k);
        GtkNotebook* nb = g_ptr_array_index(leaves, g_array_index(file_panes, guint, target[k]));
        if (tab->nb != GTK_WIDGET(nb)) {
            editor_move_tab_to(app, tab, nb, -1, FALSE);
        }
    }
    g_free(target);
    g_array_unref(file_panes);

    /* Two views of a file brought into one pane: the first stays */
    for (guint k = 0; k < files->len; k++) {
        EditorTab* tab = g_ptr_array_index(files, k);
        for (guint q = 0; q < k && tab != NULL; q++) {
            EditorTab* other = g_ptr_array_index(files, q);
            if (other != NULL && other->nb == tab->nb && other->buffer == tab->buffer) {
                g_ptr_array_remove(currents, tab->container);
                if (focused == tab) {
                    focused = other;
                }
                editor_close_tab(app, tab); /* no question: the other view keeps the changes */
                g_ptr_array_index(files, k) = NULL;
                tab = NULL;
            }
        }
    }

#ifdef HAVE_VTE
    /* Running terminals fill the terminal tabs in order, new shells the rest */
    guint next = 0;
    for (guint i = 0; wanted > 0 && i < leaves->len; i++) {
        const TabTreeNode* node = g_ptr_array_index(nodes, i);
        GtkNotebook* nb = g_ptr_array_index(leaves, i);
        for (guint t = 0; t < node->tabs->len; t++) {
            if (g_array_index(node->tabs, guint, t) != TAB_TREE_TERMINAL) {
                continue;
            }
            if (next < terms->len) {
                terminal_move_tab(g_ptr_array_index(terms, next++), nb);
            } else {
                terminal_add_tab(app, nb);
            }
        }
    }
#endif

    /* The pages current before stay current, the focused view last */
    for (guint i = 0; i < currents->len; i++) {
        GtkWidget* page = g_ptr_array_index(currents, i);
        GtkWidget* parent = gtk_widget_get_parent(page);
        GtkWidget* nb = (parent != NULL) ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
        gint num = (nb != NULL) ? gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) : -1;
        if (num >= 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), num);
        }
    }
    if (focused != NULL && focused->nb != NULL) {
        gint num = gtk_notebook_page_num(GTK_NOTEBOOK(focused->nb), focused->container);
        if (num >= 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(focused->nb), num);
        }
        app->last_focused_editor = focused;
    }

#ifdef HAVE_VTE
    /* Terminals beyond the layout's close with their tab (the caller confirmed it) */
    for (; wanted > 0 && next < terms->len; next++) {
        GtkWidget* vte = g_ptr_array_index(terms, next);
        if (app->last_focused_vte == vte) {
            app->last_focused_vte = NULL;
        }
        editor_remove_page(app, vte);
    }
#endif
    editor_remove_empty_notebooks(app);
    g_ptr_array_unref(leaves);
    g_ptr_array_unref(nodes);
}

void editor_layout_apply(AppData* app, const gchar* text) {
    TabTreeNode* tree = tab_tree_parse(text);
    if (tree == NULL || app == NULL || app->editor_box == NULL) {
        tab_tree_free(tree);
        return;
    }
    EditorTab* focused = g_list_find(app->editors, app->last_focused_editor) ? app->last_focused_editor : NULL;

    /* What the panel holds now: file tabs with the rank of their source
     * pane among those holding files, terminals, current pages */
    GPtrArray* files = g_ptr_array_new();
    GArray* ranks = g_array_new(FALSE, FALSE, sizeof(guint));
    GPtrArray* terms = g_ptr_array_new_with_free_func(g_object_unref);
    GPtrArray* currents = g_ptr_array_new();
    GPtrArray* olds = editor_get_all_notebooks(app);
    guint rank = 0;
    for (guint i = 0; i < olds->len; i++) {
        GtkNotebook* nb = g_ptr_array_index(olds, i);
        gint current = gtk_notebook_get_current_page(nb);
        if (current >= 0) {
            g_ptr_array_add(currents, gtk_notebook_get_nth_page(nb, current));
        }
        gboolean had_files = FALSE;
        for (gint p = 0; p < gtk_notebook_get_n_pages(nb); p++) {
            GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
            EditorTab* tab = is_terminal(page) ? NULL : tab_of_page(app, page);
            if (is_terminal(page)) {
                g_ptr_array_add(terms, g_object_ref(page));
            } else if (tab != NULL) {
                g_ptr_array_add(files, tab);
                g_array_append_val(ranks, rank);
                had_files = TRUE;
            }
        }
        rank += had_files ? 1 : 0;
    }
    g_ptr_array_unref(olds);
    guint wanted = tab_tree_terminal_count(tree);
    GtkNotebook* root = (files->len > 0 || terms->len > 0 || wanted > 0) ? panel_root(app) : NULL;
    if (root != NULL) {
        rebuild(app, tree, root, files, ranks, terms, currents, focused);
    }
    g_ptr_array_unref(files);
    g_array_unref(ranks);
    g_ptr_array_unref(terms);
    g_ptr_array_unref(currents);
    tab_tree_free(tree);
}
