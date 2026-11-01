/* The editor tabs of a workspace saved with the session (see editor_session.h) */
#include "editor_session.h"
#include "editor.h"
#include "tab_tree.h"

/* Share of a split's first child, kept on the GtkPaned by panels.c and
 * applied by workspace.c (also used by the terminal panes) */
static const gchar* PANED_FRAC_KEY = "term-paned-frac";

/* ------------------------------------------------------------------------- */
/* Capture                                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    GPtrArray* files;
    GList* editors;
    EditorTab* focused;
} Capture;

static EditorTab* tab_of_page(GList* editors, GtkWidget* child) {
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->container == child) {
            return t;
        }
    }
    return NULL;
}

/* Index of the tab's file in the list, added when new; -1 when it is not
 * saved (notes' buffers, unsaved buffers) */
static gint file_index(Capture* c, const EditorTab* tab) {
    if (tab->is_buffer || tab->path == NULL || !g_path_is_absolute(tab->path)) {
        return -1;
    }
    for (guint i = 0; i < c->files->len; i++) {
        if (g_strcmp0(g_ptr_array_index(c->files, i), tab->path) == 0) {
            return (gint)i;
        }
    }
    g_ptr_array_add(c->files, g_strdup(tab->path));
    return (gint)c->files->len - 1;
}

static void add_tab(Capture* c, TabTreeNode* node, EditorTab* tab, gboolean current) {
    gint index = (tab != NULL) ? file_index(c, tab) : -1;
    if (index < 0) {
        return;
    }
    gint pos = (gint)tab_tree_notebook_add(node, (guint)index);
    if (current) {
        node->current = pos;
    }
    if (tab == c->focused) {
        node->focused = pos;
    }
}

/* NULL when it holds no saved editor tab */
static TabTreeNode* finish_notebook(TabTreeNode* node) {
    if (node->tabs->len == 0) {
        tab_tree_free(node);
        return NULL;
    }
    return node;
}

static TabTreeNode* capture_notebook(Capture* c, GtkNotebook* nb) {
    TabTreeNode* node = tab_tree_notebook_new();
    gint current = gtk_notebook_get_current_page(nb);
    for (gint p = 0; p < gtk_notebook_get_n_pages(nb); p++) {
        add_tab(c, node, tab_of_page(c->editors, gtk_notebook_get_nth_page(nb, p)), p == current);
    }
    return finish_notebook(node);
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

static TabTreeNode* capture_widget(Capture* c, GtkWidget* w) {
    if (w == NULL) {
        return NULL;
    }
    if (GTK_IS_NOTEBOOK(w)) {
        return capture_notebook(c, GTK_NOTEBOOK(w));
    }
    if (!GTK_IS_PANED(w)) {
        return NULL;
    }
    TabTreeNode* first = capture_widget(c, gtk_paned_get_start_child(GTK_PANED(w)));
    TabTreeNode* second = capture_widget(c, gtk_paned_get_end_child(GTK_PANED(w)));
    gboolean vertical = gtk_orientable_get_orientation(GTK_ORIENTABLE(w)) == GTK_ORIENTATION_VERTICAL;
    return tab_tree_split_new(vertical, paned_frac(w), first, second);
}

static gchar* tree_text(TabTreeNode* node) {
    gchar* text = (node != NULL) ? tab_tree_to_string(node) : NULL;
    tab_tree_free(node);
    return text;
}

void editor_session_capture(AppData* app, gint ws, GPtrArray* files, gchar** preview_tabs,
                            gchar** editor_tabs) {
    *preview_tabs = NULL;
    *editor_tabs = NULL;
    if (app == NULL || ws < 0 || ws >= app->num_workspaces) {
        return;
    }
    gboolean shown = (ws == app->current_ws);
    Capture c = { files, shown ? app->editors : app->workspaces[ws].editors,
                  shown ? app->last_focused_editor : NULL };

    /* The preview notebook: its pages when shown; otherwise its editor tabs,
     * taken out in order, are those of the workspace's list */
    TabTreeNode* preview = NULL;
    if (shown && app->notebook != NULL) {
        preview = capture_notebook(&c, GTK_NOTEBOOK(app->notebook));
    } else if (!shown) {
        preview = tab_tree_notebook_new();
        for (GList* l = c.editors; l != NULL; l = l->next) {
            EditorTab* t = l->data;
            if (t != NULL && t->nb != NULL && t->nb == app->notebook) {
                add_tab(&c, preview, t, FALSE);
            }
        }
        preview = finish_notebook(preview);
    }
    *preview_tabs = tree_text(preview);

    GtkWidget* panel = shown ? (app->editor_box != NULL ? gtk_widget_get_first_child(app->editor_box) : NULL)
                             : app->workspaces[ws].editor_panel;
    *editor_tabs = tree_text(capture_widget(&c, panel));
}

/* ------------------------------------------------------------------------- */
/* Restore                                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    const gchar* const* files;
    gboolean* usable; /* per file: a regular file now */
    guint n_files;
    GPtrArray* currents; /* EditorTab*: the current page of each notebook */
    EditorTab* focused;
} Restore;

/* A view of path in nb (an editor notebook or the preview notebook): the
 * file opened and its tab moved there, or a second view of it when it is
 * already open in another editor notebook. NULL when it cannot be edited. */
static EditorTab* place_view(AppData* app, const gchar* path, GtkNotebook* nb) {
    EditorTab* open = editor_find_tab(app, path);
    if (open == NULL) {
        gchar* name = g_path_get_basename(path);
        editor_open_file(app, name, path);
        g_free(name);
        EditorTab* tab = editor_find_tab(app, path);
        if (tab != NULL && tab->nb != GTK_WIDGET(nb)) {
            editor_move_tab_to(app, tab, nb, -1, FALSE); /* the notebooks still empty stay */
        }
        return tab;
    }
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t->nb == GTK_WIDGET(nb) && g_strcmp0(t->path, open->path) == 0) {
            return t; /* one view of a file per notebook */
        }
    }
    /* The preview notebook has no split views */
    return (GTK_WIDGET(nb) != app->notebook) ? editor_add_view(app, open, nb) : NULL;
}

static void restore_notebook(Restore* r, const TabTreeNode* node, GtkNotebook* nb) {
    EditorTab* current = NULL;
    for (guint i = 0; i < node->tabs->len; i++) {
        guint index = g_array_index(node->tabs, guint, i);
        if (index >= r->n_files || !r->usable[index]) {
            continue;
        }
        EditorTab* tab = place_view(r->app, r->files[index], nb);
        if (tab != NULL && (gint)i == node->current) {
            current = tab;
        }
        if (tab != NULL && (gint)i == node->focused) {
            r->focused = tab;
        }
    }
    if (current != NULL) {
        g_ptr_array_add(r->currents, current);
    }
}

static void restore_tree(Restore* r, const TabTreeNode* node, GtkNotebook* nb) {
    if (node->is_notebook) {
        restore_notebook(r, node, nb);
        return;
    }
    GtkWidget* second = editor_notebook_split(r->app, nb, node->vertical);
    if (second == NULL) {
        restore_tree(r, node->first, nb);
        restore_tree(r, node->second, nb);
        return;
    }
    gdouble* frac = g_new(gdouble, 1);
    *frac = node->frac;
    g_object_set_data_full(G_OBJECT(gtk_widget_get_parent(second)), PANED_FRAC_KEY, frac, g_free);
    restore_tree(r, node->first, nb);
    restore_tree(r, node->second, GTK_NOTEBOOK(second));
}

/* The notebook the dedicated panel starts from */
static GtkNotebook* panel_notebook(AppData* app) {
    if (app->editor_box == NULL) {
        return NULL;
    }
    if (gtk_widget_get_first_child(app->editor_box) == NULL) {
        GtkWidget* nb = editor_notebook_new(app);
        gtk_box_append(GTK_BOX(app->editor_box), nb);
        app->editor_notebook = nb;
    }
    GPtrArray* nbs = editor_get_all_notebooks(app);
    GtkNotebook* nb = (nbs->len > 0) ? g_ptr_array_index(nbs, 0) : NULL;
    g_ptr_array_unref(nbs);
    return nb;
}

void editor_session_restore(AppData* app, const gchar* const* files, const gchar* preview_tabs,
                            const gchar* editor_tabs) {
    if (app == NULL || files == NULL) {
        return;
    }
    Restore r = { app, files, NULL, g_strv_length((gchar**)files), g_ptr_array_new(), NULL };
    r.usable = g_new0(gboolean, MAX(r.n_files, 1));
    for (guint i = 0; i < r.n_files; i++) {
        r.usable[i] = g_file_test(files[i], G_FILE_TEST_IS_REGULAR);
    }
    TabTreeNode* preview = tab_tree_parse(preview_tabs);
    TabTreeNode* panel = tab_tree_parse(editor_tabs);
    if (preview == NULL && panel == NULL) {
        /* Older session files: the files only, opened in order */
        for (guint i = 0; i < r.n_files; i++) {
            if (r.usable[i]) {
                gchar* name = g_path_get_basename(files[i]);
                editor_open_file(app, name, files[i]);
                g_free(name);
            }
        }
    }
    GtkNotebook* first = (panel != NULL) ? panel_notebook(app) : NULL;
    if (first != NULL) {
        restore_tree(&r, panel, first);
        editor_remove_empty_notebooks(app);
    }
    if (preview != NULL && app->notebook != NULL) {
        restore_notebook(&r, preview, GTK_NOTEBOOK(app->notebook));
        if (preview->current < 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0); /* the preview */
        }
    }
    for (guint i = 0; i < r.currents->len; i++) {
        EditorTab* tab = g_ptr_array_index(r.currents, i);
        GtkNotebook* nb = GTK_NOTEBOOK(tab->nb);
        gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, tab->container));
    }
    if (r.focused != NULL) {
        app->last_focused_editor = r.focused; /* new files open beside it */
    }
    tab_tree_free(preview);
    tab_tree_free(panel);
    g_ptr_array_unref(r.currents);
    g_free(r.usable);
}
