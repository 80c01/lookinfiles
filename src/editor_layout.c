/* The shape of the dedicated editor panel in saved layouts (see editor_layout.h) */
#include "editor_layout.h"
#include "editor/editor.h"
#include "layout_store.h"
#include "tab_tree.h"
#include "terminal/terminal.h"

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

/* A page that moves like an editor tab but is neither one nor a terminal (a
 * canvas): laid out with the files */
static gboolean is_other(AppData* app, GtkWidget* page) {
    return !is_terminal(page) && editor_page_is_movable(page) && tab_of_page(app, page) == NULL;
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
        } else if (tab_of_page(app, page) != NULL || is_other(app, page)) {
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
                    GPtrArray* terms, GPtrArray* others, GPtrArray* currents, EditorTab* focused,
                    GPtrArray* spare) {
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
    for (guint i = 0; i < others->len; i++) {
        editor_move_page_to(app, g_ptr_array_index(others, i), root, -1);
    }
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
    /* The other pages to the first pane of files */
    GtkNotebook* first_files = g_ptr_array_index(leaves, g_array_index(file_panes, guint, 0));
    for (guint i = 0; i < others->len; i++) {
        editor_move_page_to(app, g_ptr_array_index(others, i), first_files, -1);
    }
    g_array_unref(file_panes);

    /* Two views of a file brought into one pane: the first stays (both in
     * a preview, which closes nothing) */
    for (guint k = 0; k < files->len && spare == NULL; k++) {
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
            } else if (spare != NULL && spare->len > 0) {
                GtkWidget* vte = g_ptr_array_steal_index(spare, 0);
                terminal_attach_tab(vte, nb);
                g_object_unref(vte);
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
    /* Terminals beyond the layout's close with their tab (the caller
     * confirmed it), or go to spare */
    for (; wanted > 0 && next < terms->len; next++) {
        GtkWidget* vte = g_ptr_array_index(terms, next);
        terminal_forget(app, vte);
        if (spare != NULL) {
            GtkWidget* kept = terminal_detach_tab(vte);
            if (kept != NULL) {
                g_ptr_array_add(spare, kept);
            }
        } else {
            editor_remove_page(app, vte);
        }
    }
#endif
    editor_remove_empty_notebooks(app);
    g_ptr_array_unref(leaves);
    g_ptr_array_unref(nodes);
}

void editor_layout_apply(AppData* app, const gchar* text) {
    editor_layout_apply_spare(app, text, NULL);
}

void editor_layout_apply_spare(AppData* app, const gchar* text, GPtrArray* spare) {
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
    GPtrArray* others = g_ptr_array_new();
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
            } else if (is_other(app, page)) {
                g_ptr_array_add(others, page);
            }
        }
        rank += had_files ? 1 : 0;
    }
    g_ptr_array_unref(olds);
    guint wanted = tab_tree_terminal_count(tree);
    GtkNotebook* root = (files->len > 0 || terms->len > 0 || others->len > 0 || wanted > 0) ? panel_root(app)
                                                                                            : NULL;
    if (root != NULL) {
        rebuild(app, tree, root, files, ranks, terms, others, currents, focused, spare);
    }
    g_ptr_array_unref(others);
    g_ptr_array_unref(files);
    g_array_unref(ranks);
    g_ptr_array_unref(terms);
    g_ptr_array_unref(currents);
    tab_tree_free(tree);
}

/* ------------------------------------------------------------------------- */
/* Exact arrangement, put back after a layout preview                        */
/* ------------------------------------------------------------------------- */

struct EditorSnapshot {
    GPtrArray* pages;      /* editor tabs (containers) and terminals, referenced:
                            * the panel's first, then the preview notebook's */
    guint panel_count;     /* pages of the panel */
    TabTreeNode* panel;    /* the panel's splits, tabs being indices in pages; NULL if empty */
    GArray* preview;       /* indices in pages of the preview notebook's tabs, in order */
    GtkWidget* preview_current; /* current page of the preview notebook */
    GtkWidget* focused;    /* container of the editor view focused last */
};

#ifdef HAVE_VTE
/* The notebook holding page, or NULL */
static GtkNotebook* notebook_of(GtkWidget* page) {
    GtkWidget* parent = gtk_widget_get_parent(page);
    GtkWidget* nb = (parent != NULL) ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    return (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) ? GTK_NOTEBOOK(nb) : NULL;
}
#endif

static gboolean is_placeable(AppData* app, GtkWidget* page) {
    return is_terminal(page) || tab_of_page(app, page) != NULL || is_other(app, page);
}

static TabTreeNode* snapshot_widget(AppData* app, GtkWidget* w, GPtrArray* pages) {
    if (w == NULL) {
        return NULL;
    }
    if (GTK_IS_NOTEBOOK(w)) {
        GtkNotebook* nb = GTK_NOTEBOOK(w);
        TabTreeNode* node = tab_tree_notebook_new();
        gint current = gtk_notebook_get_current_page(nb);
        for (gint p = 0; p < gtk_notebook_get_n_pages(nb); p++) {
            GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
            if (!is_placeable(app, page)) {
                continue;
            }
            guint pos = tab_tree_notebook_add(node, pages->len);
            if (p == current) {
                node->current = (gint)pos;
            }
            g_ptr_array_add(pages, g_object_ref(page));
        }
        if (node->tabs->len == 0) {
            tab_tree_free(node);
            return NULL;
        }
        return node;
    }
    if (!GTK_IS_PANED(w)) {
        return NULL;
    }
    TabTreeNode* first = snapshot_widget(app, gtk_paned_get_start_child(GTK_PANED(w)), pages);
    TabTreeNode* second = snapshot_widget(app, gtk_paned_get_end_child(GTK_PANED(w)), pages);
    gboolean vertical = gtk_orientable_get_orientation(GTK_ORIENTABLE(w)) == GTK_ORIENTATION_VERTICAL;
    return tab_tree_split_new(vertical, paned_frac(w), first, second);
}

EditorSnapshot* editor_layout_snapshot(AppData* app) {
    EditorSnapshot* snap = g_new0(EditorSnapshot, 1);
    snap->pages = g_ptr_array_new_with_free_func(g_object_unref);
    snap->preview = g_array_new(FALSE, FALSE, sizeof(guint));
    if (app->editor_box != NULL) {
        snap->panel = snapshot_widget(app, gtk_widget_get_first_child(app->editor_box), snap->pages);
    }
    snap->panel_count = snap->pages->len;
    if (app->notebook != NULL) {
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        for (gint p = 0; p < gtk_notebook_get_n_pages(nb); p++) {
            GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
            guint index = snap->pages->len;
            g_array_append_val(snap->preview, index);
            g_ptr_array_add(snap->pages, g_object_ref(page));
        }
        gint current = gtk_notebook_get_current_page(nb);
        snap->preview_current = (current >= 0) ? g_object_ref(gtk_notebook_get_nth_page(nb, current)) : NULL;
    }
    EditorTab* focused = g_list_find(app->editors, app->last_focused_editor) ? app->last_focused_editor : NULL;
    snap->focused = (focused != NULL && focused->container != NULL) ? g_object_ref(focused->container) : NULL;
    return snap;
}

void editor_snapshot_free(EditorSnapshot* snap) {
    if (snap == NULL) {
        return;
    }
    g_ptr_array_unref(snap->pages);
    tab_tree_free(snap->panel);
    g_array_unref(snap->preview);
    g_clear_object(&snap->preview_current);
    g_clear_object(&snap->focused);
    g_free(snap);
}

/* Take a terminal out of its notebook into spare (closed without spare) */
static void set_aside(GtkWidget* page, GPtrArray* spare) {
#ifdef HAVE_VTE
    GtkWidget* vte = terminal_detach_tab(page);
    if (vte != NULL && spare != NULL) {
        g_ptr_array_add(spare, vte);
    } else if (vte != NULL) {
        g_object_unref(vte);
    }
#else
    (void)page;
    (void)spare;
#endif
}

/* Put page (an editor tab or a terminal, in a notebook or in spare, or
 * another movable page of a notebook) into nb */
static void place_page(AppData* app, GtkWidget* page, GtkNotebook* nb, GPtrArray* spare) {
    EditorTab* tab = tab_of_page(app, page);
    if (tab != NULL) {
        if (tab->nb != GTK_WIDGET(nb)) {
            editor_move_tab_to(app, tab, nb, -1, FALSE);
        }
        return;
    }
    if (is_other(app, page)) {
        editor_move_page_to(app, page, nb, -1);
        return;
    }
#ifdef HAVE_VTE
    if (!is_terminal(page)) {
        return;
    }
    GtkNotebook* from = notebook_of(page);
    guint i = 0;
    if (from != NULL) {
        if (from != nb) {
            terminal_move_tab(page, nb);
        }
    } else if (spare != NULL && g_ptr_array_find(spare, page, &i)) {
        GtkWidget* vte = g_ptr_array_steal_index(spare, i);
        terminal_attach_tab(vte, nb);
        g_object_unref(vte);
    }
#else
    (void)spare;
#endif
}

/* Give nb's pages the order of indices (positions in pages) and its current page */
static void order_pages(GtkNotebook* nb, const EditorSnapshot* snap, GArray* indices, GtkWidget* current) {
    gint pos = 0;
    for (guint i = 0; i < indices->len; i++) {
        GtkWidget* page = g_ptr_array_index(snap->pages, g_array_index(indices, guint, i));
        if (gtk_notebook_page_num(nb, page) >= 0) {
            gtk_notebook_reorder_child(nb, page, pos++);
        }
    }
    gint num = (current != NULL) ? gtk_notebook_page_num(nb, current) : -1;
    if (num >= 0) {
        gtk_notebook_set_current_page(nb, num);
    }
}

void editor_layout_restore(AppData* app, const EditorSnapshot* snap, GPtrArray* spare) {
    if (app == NULL || snap == NULL || app->editor_box == NULL) {
        return;
    }
    /* Every page of the panel into its first notebook, the editor tabs of
     * the preview notebook back there */
    GtkNotebook* root = (snap->panel != NULL) ? panel_root(app) : NULL;
    for (guint i = 0; i < snap->panel_count && root != NULL; i++) {
        place_page(app, g_ptr_array_index(snap->pages, i), root, spare);
    }
    for (guint i = 0; i < snap->preview->len && app->notebook != NULL; i++) {
        GtkWidget* page = g_ptr_array_index(snap->pages, g_array_index(snap->preview, guint, i));
        if (tab_of_page(app, page) != NULL || is_other(app, page)) {
            place_page(app, page, GTK_NOTEBOOK(app->notebook), spare);
        }
    }
    /* What the panel holds besides: terminals go to spare, editor tabs to
     * the first notebook (or the preview notebook) */
    GPtrArray* nbs = editor_get_all_notebooks(app);
    for (guint n = 0; n < nbs->len; n++) {
        GtkNotebook* nb = g_ptr_array_index(nbs, n);
        for (gint p = gtk_notebook_get_n_pages(nb) - 1; p >= 0; p--) {
            GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
            if (nb == root || g_ptr_array_find(snap->pages, page, NULL)) {
                continue;
            }
            if (is_terminal(page)) {
                set_aside(page, spare);
            } else if (tab_of_page(app, page) != NULL) {
                GtkWidget* to = (root != NULL) ? GTK_WIDGET(root) : app->notebook;
                editor_move_tab_to(app, tab_of_page(app, page), GTK_NOTEBOOK(to), -1, FALSE);
            } else if (is_other(app, page)) {
                GtkWidget* to = (root != NULL) ? GTK_WIDGET(root) : app->notebook;
                editor_move_page_to(app, page, GTK_NOTEBOOK(to), -1);
            }
        }
    }
    g_ptr_array_unref(nbs);
    if (root != NULL) {
        for (gint p = gtk_notebook_get_n_pages(root) - 1; p >= 0; p--) {
            GtkWidget* page = gtk_notebook_get_nth_page(root, p);
            if (is_terminal(page) && !g_ptr_array_find(snap->pages, page, NULL)) {
                set_aside(page, spare);
            }
        }
    }

    /* The splits as they were, each page in its notebook and place */
    if (root != NULL && gtk_notebook_get_n_pages(root) > 0) {
        editor_remove_empty_notebooks(app);
        GPtrArray* leaves = g_ptr_array_new();
        GPtrArray* nodes = g_ptr_array_new();
        build(app, snap->panel, root, leaves, nodes);
        for (guint i = 0; i < leaves->len; i++) {
            GtkNotebook* nb = g_ptr_array_index(leaves, i);
            const TabTreeNode* node = g_ptr_array_index(nodes, i);
            for (guint t = 0; t < node->tabs->len; t++) {
                place_page(app, g_ptr_array_index(snap->pages, g_array_index(node->tabs, guint, t)), nb, spare);
            }
            GtkWidget* current = (node->current >= 0)
                                     ? g_ptr_array_index(snap->pages, g_array_index(node->tabs, guint, node->current))
                                     : NULL;
            order_pages(nb, snap, node->tabs, current);
        }
        g_ptr_array_unref(leaves);
        g_ptr_array_unref(nodes);
    }
    editor_remove_empty_notebooks(app);
    if (app->notebook != NULL) {
        order_pages(GTK_NOTEBOOK(app->notebook), snap, snap->preview, snap->preview_current);
    }
    EditorTab* focused = (snap->focused != NULL) ? tab_of_page(app, snap->focused) : NULL;
    if (focused != NULL) {
        app->last_focused_editor = focused;
    }
}
