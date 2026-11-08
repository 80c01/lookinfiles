/* The editor tabs of a workspace saved with the session (see editor_session.h) */
#include "editor_session.h"
#include <math.h>
#include <string.h>
#include "canvas_view.h"
#include "editor.h"
#include "tab_tree.h"
#include "terminal.h"

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

/* Index of path (a file, or the directory of a Canvas note) in the list,
 * added when new; -1 when it is not saved (in-memory buffers) */
static gint file_index(Capture* c, const gchar* path) {
    if (path == NULL || !g_path_is_absolute(path)) {
        return -1;
    }
    for (guint i = 0; i < c->files->len; i++) {
        if (g_strcmp0(g_ptr_array_index(c->files, i), path) == 0) {
            return (gint)i;
        }
    }
    g_ptr_array_add(c->files, g_strdup(path));
    return (gint)c->files->len - 1;
}

static void add_path(Capture* c, TabTreeNode* node, const gchar* path, gboolean current, gboolean focused) {
    gint index = file_index(c, path);
    if (index < 0) {
        return;
    }
    gint pos = (gint)tab_tree_notebook_add(node, (guint)index);
    if (current) {
        node->current = pos;
    }
    if (focused) {
        node->focused = pos;
    }
}

static void add_tab(Capture* c, TabTreeNode* node, EditorTab* tab, gboolean current) {
    if (tab != NULL) {
        add_path(c, node, tab->path, current, tab == c->focused);
    }
}

#ifdef HAVE_VTE
static void add_terminal(TabTreeNode* node, gboolean current) {
    gint pos = (gint)tab_tree_notebook_add(node, TAB_TREE_TERMINAL);
    if (current) {
        node->current = pos;
    }
}
#endif

/* NULL when it holds no saved tab */
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
        GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
#ifdef HAVE_VTE
        if (VTE_IS_TERMINAL(page)) {
            add_terminal(node, p == current);
            continue;
        }
#endif
        EditorTab* tab = tab_of_page(c->editors, page);
        if (tab != NULL) {
            add_tab(c, node, tab, p == current);
        } else {
            add_path(c, node, canvas_view_page_dir(page), p == current, FALSE);
        }
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
        GPtrArray* canvases = app->workspaces[ws].canvas_pages; /* set aside with the workspace */
        for (guint i = 0; canvases != NULL && i < canvases->len; i++) {
            add_path(&c, preview, canvas_view_page_dir(g_ptr_array_index(canvases, i)), FALSE, FALSE);
        }
#ifdef HAVE_VTE
        GPtrArray* terms = app->workspaces[ws].preview_terms; /* set aside with the workspace */
        for (guint i = 0; terms != NULL && i < terms->len; i++) {
            add_terminal(preview, FALSE);
        }
#endif
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

/* The directory of a Canvas note (canvas_doc.h: "<title>.canvas/") */
static gboolean is_canvas_dir(const gchar* path) {
    return g_str_has_suffix(path, ".canvas") && g_file_test(path, G_FILE_TEST_IS_DIR);
}

/* The Canvas note in dir as a page of nb */
static GtkWidget* place_canvas(AppData* app, const gchar* dir, GtkNotebook* nb) {
    gchar* title = g_path_get_basename(dir);
    title[strlen(title) - strlen(".canvas")] = '\0';
    GtkWidget* page = canvas_view_open(app, dir, title);
    g_free(title);
    GtkWidget* parent = (page != NULL) ? gtk_widget_get_parent(page) : NULL;
    GtkWidget* in = (parent != NULL) ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    if (page != NULL && in != GTK_WIDGET(nb)) {
        editor_move_page_to(app, page, nb, -1);
    }
    return page;
}

typedef struct {
    AppData* app;
    const gchar* const* files;
    gboolean* usable; /* per file: a regular file or a Canvas note now */
    guint n_files;
    GPtrArray* currents; /* GtkWidget*: the current page of each notebook */
    EditorTab* focused;
} Restore;

/* While a restored session settles, the layout of a view can change under its
 * scroll position: GtkTextView then keeps an internal offset that differs from
 * its vertical adjustment (the adjustment says 0, the view is scrolled to the
 * last line: only that line number is drawn, and moving the cursor does nothing
 * as the adjustment already holds the requested value), or the offset is
 * somewhere else than the cursor. During the first moments it is shown, check
 * the view and put it back on its cursor. */
#define RESYNC_FRAMES 60

static void resync_nudge(GtkAdjustment* adj) {
    gdouble lo = gtk_adjustment_get_lower(adj);
    gdouble up = gtk_adjustment_get_upper(adj);
    gdouble page = gtk_adjustment_get_page_size(adj);
    gdouble step = gtk_adjustment_get_step_increment(adj);
    gdouble pinc = gtk_adjustment_get_page_increment(adj);
    /* Two real value changes: the view takes the adjustment's value again */
    gtk_adjustment_configure(adj, lo + 1, lo, MAX(up, page + 2), step, pinc, page);
    gtk_adjustment_configure(adj, lo, lo, up, step, pinc, page);
}

static gboolean resync_view_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)clock;
    guint* frames = data;
    if (!gtk_widget_get_mapped(widget) || gtk_widget_get_height(widget) <= 1) {
        return G_SOURCE_CONTINUE; /* a background tab: wait until it is shown */
    }
    if (++*frames > RESYNC_FRAMES) {
        return G_SOURCE_REMOVE;
    }
    GtkTextView* view = GTK_TEXT_VIEW(widget);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    if (vadj == NULL) {
        return G_SOURCE_REMOVE;
    }
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(view, &visible);
    gdouble offset = visible.y + gtk_text_view_get_top_margin(view);
    if (fabs(offset - gtk_adjustment_get_value(vadj)) > 2.0) {
        resync_nudge(vadj);
        gtk_text_view_get_visible_rect(view, &visible);
    }
    GtkTextIter ins;
    gtk_text_buffer_get_iter_at_mark(buffer, &ins, gtk_text_buffer_get_insert(buffer));
    gint y = 0, h = 0;
    gtk_text_view_get_line_yrange(view, &ins, &y, &h);
    if (y < visible.y || y + h > visible.y + visible.height) {
        gtk_text_view_scroll_to_mark(view, gtk_text_buffer_get_insert(buffer), 0.0, TRUE, 0.0, 0.3);
    }
    return G_SOURCE_CONTINUE; /* the layout may still move for a few frames */
}

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
        if (tab != NULL && tab->view != NULL) {
            gtk_widget_add_tick_callback(tab->view, resync_view_tick, g_new0(guint, 1), g_free);
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
    GtkWidget* current = NULL;
    for (guint i = 0; i < node->tabs->len; i++) {
        guint index = g_array_index(node->tabs, guint, i);
        if (index == TAB_TREE_TERMINAL) {
#ifdef HAVE_VTE
            GtkWidget* vte = terminal_add_tab(r->app, nb);
            if ((gint)i == node->current) {
                current = vte;
            }
#endif
            continue;
        }
        if (index >= r->n_files || !r->usable[index]) {
            continue;
        }
        if (is_canvas_dir(r->files[index])) {
            GtkWidget* page = place_canvas(r->app, r->files[index], nb);
            if (page != NULL && (gint)i == node->current) {
                current = page;
            }
            continue;
        }
        EditorTab* tab = place_view(r->app, r->files[index], nb);
        if (tab != NULL && (gint)i == node->current) {
            current = tab->container;
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

/* Diagnostic (LOOKIN_DEBUG_VIEWS=1): state of every editor view some time after a restore */
static gboolean dump_views(gpointer data) {
    AppData* app = (AppData*)data;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t->view == NULL) continue;
        GtkTextView* v = GTK_TEXT_VIEW(t->view);
        GtkAdjustment* a = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v));
        GdkRectangle vis;
        gtk_text_view_get_visible_rect(v, &vis);
        GtkTextIter ins;
        gtk_text_buffer_get_iter_at_mark(t->buffer, &ins, gtk_text_buffer_get_insert(t->buffer));
        gint y = 0, h = 0;
        gtk_text_view_get_line_yrange(v, &ins, &y, &h);
        g_printerr("view %s: mapped=%d view=%dx%d scroll=%dx%d wrap=%d lines=%d cursor_line=%d "
                   "cursor_y=%d+%d vadj(v=%.0f lo=%.0f up=%.0f page=%.0f) visible(y=%d h=%d) vadj_same_as_scroll=%d\n",
                   t->path != NULL ? t->path : "?", gtk_widget_get_mapped(t->view),
                   gtk_widget_get_width(t->view), gtk_widget_get_height(t->view),
                   t->scroll != NULL ? gtk_widget_get_width(t->scroll) : -1,
                   t->scroll != NULL ? gtk_widget_get_height(t->scroll) : -1,
                   (gint)gtk_text_view_get_wrap_mode(v), gtk_text_buffer_get_line_count(t->buffer),
                   gtk_text_iter_get_line(&ins), y, h,
                   gtk_adjustment_get_value(a), gtk_adjustment_get_lower(a), gtk_adjustment_get_upper(a),
                   gtk_adjustment_get_page_size(a), vis.y, vis.height,
                   t->scroll != NULL &&
                       a == gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->scroll)));
    }
    g_printerr("--\n");
    return G_SOURCE_REMOVE;
}

void editor_session_restore(AppData* app, const gchar* const* files, const gchar* preview_tabs,
                            const gchar* editor_tabs) {
    if (app == NULL || files == NULL) {
        return;
    }
    Restore r = { app, files, NULL, g_strv_length((gchar**)files), g_ptr_array_new(), NULL };
    r.usable = g_new0(gboolean, MAX(r.n_files, 1));
    for (guint i = 0; i < r.n_files; i++) {
        r.usable[i] = g_file_test(files[i], G_FILE_TEST_IS_REGULAR) || is_canvas_dir(files[i]);
    }
    TabTreeNode* preview = tab_tree_parse(preview_tabs);
    TabTreeNode* panel = tab_tree_parse(editor_tabs);
    if (preview == NULL && panel == NULL) {
        /* Older session files: the files only, opened in order */
        for (guint i = 0; i < r.n_files; i++) {
            if (r.usable[i] && !is_canvas_dir(files[i])) {
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
        GtkWidget* page = g_ptr_array_index(r.currents, i);
        GtkWidget* nb = gtk_widget_get_ancestor(gtk_widget_get_parent(page), GTK_TYPE_NOTEBOOK);
        if (nb != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), gtk_notebook_page_num(GTK_NOTEBOOK(nb), page));
        }
    }
    if (r.focused != NULL) {
        app->last_focused_editor = r.focused; /* new files open beside it */
    }
    if (g_getenv("LOOKIN_DEBUG_VIEWS") != NULL) {
        g_timeout_add(1500, dump_views, app);
        g_timeout_add(5000, dump_views, app);
    }
    tab_tree_free(preview);
    tab_tree_free(panel);
    g_ptr_array_unref(r.currents);
    g_free(r.usable);
}
