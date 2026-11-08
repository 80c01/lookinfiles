#include "terminal/terminal.h"
#include "widgets/panels.h"
#include "core/app_events.h"
#include "explorer/remote_store.h"
#include "workspace/layout_store.h"
#include "theme/palette.h"
#include "theme/fonts.h"
#include "widgets/tab_drop.h"
#include "editor/editor.h"
#include "chrome/focus_route.h"

#ifdef HAVE_VTE

/* The terminal panel: a stack with one box of panes per workspace */
struct Terminal {
    GtkWidget* stack;
};

/* Terminals of one workspace (VirtualLayout.term), private to this file */
struct TermWorkspace {
    GtkWidget* box;          /* root of its panes, a child of the stack */
    GtkWidget* last_focused; /* last focused terminal pane, for splitting; NULL if none */
    GPtrArray* preview_tabs; /* its terminal tabs of the preview notebook while hidden (owned refs) */
};

static struct TermWorkspace* cur_term(AppData* app) {
    if (app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return NULL;
    }
    return app->workspaces[app->current_ws].term;
}

/* Box of the shown workspace's panes, NULL before its first workspace */
static GtkWidget* cur_box(AppData* app) {
    struct TermWorkspace* t = cur_term(app);
    return t != NULL ? t->box : NULL;
}

static GtkWidget* cur_focus(AppData* app) {
    struct TermWorkspace* t = cur_term(app);
    return t != NULL ? t->last_focused : NULL;
}

static void set_focus(AppData* app, GtkWidget* vte) {
    struct TermWorkspace* t = cur_term(app);
    if (t != NULL) {
        t->last_focused = vte;
    }
}

static GtkWidget* term_stack(AppData* app) {
    return app->terminal != NULL ? app->terminal->stack : NULL;
}

/* Terminal colours from the palette (text, background, cursor, selection
 * and the 16 ANSI colours: normal row then bright row) and the code font */
static void parse_role(GdkRGBA* rgba, PaletteRole role) {
    gdk_rgba_parse(rgba, palette_color(role));
}

static void apply_terminal_theme(VteTerminal* term) {
    GdkRGBA fg, bg;
    GdkRGBA ansi[16];
    parse_role(&fg, PAL_TERM_FG);
    parse_role(&bg, PAL_BG_TERMINAL);
    for (gint i = 0; i < 16; i++) {
        parse_role(&ansi[i], (PaletteRole)(PAL_ANSI_BLACK + i));
    }
    vte_terminal_set_colors(term, &fg, &bg, ansi, 16);

    GdkRGBA cursor, selection;
    parse_role(&cursor, PAL_TERM_CURSOR);
    vte_terminal_set_color_cursor(term, &cursor);
    parse_role(&selection, PAL_EDITOR_SELECTION);
    vte_terminal_set_color_highlight(term, &selection);

    /* The code font, like file contents in the preview and editors */
    PangoFontDescription* font = pango_font_description_new();
    pango_font_description_set_family(font, fonts_family(FONT_CODE));
    pango_font_description_set_absolute_size(font, fonts_size(FONT_CODE) * PANGO_SCALE);
    vte_terminal_set_font(term, font);
    pango_font_description_free(font);
}

/* Every terminal (weak references), recoloured on theme changes */
static GPtrArray* all_terminals = NULL;

static void on_terminal_finalized(gpointer data, GObject* where) {
    AppData* app = (AppData*)data;
    if (all_terminals != NULL) {
        g_ptr_array_remove_fast(all_terminals, where);
    }
    /* No pointer to a closed terminal may stay behind */
    terminal_forget(app, (GtkWidget*)where);
}

void terminal_refresh_theme(void) {
    for (guint i = 0; all_terminals != NULL && i < all_terminals->len; i++) {
        apply_terminal_theme(VTE_TERMINAL(g_ptr_array_index(all_terminals, i)));
    }
}

/* Async spawn callback: report failures. VTE already watches the child (and
 * hangs it up when the terminal was closed meanwhile: term is then NULL); the
 * error belongs to VTE. */
static void on_spawn_result(VteTerminal* term, GPid pid, GError* error,
                            gpointer data) {
    (void)term;
    (void)pid;
    (void)data;
    if (error != NULL) {
        g_printerr("[ Cannot start terminal shell: %s ]\n", error->message);
    }
}

/* The shell to run: $SHELL, else bash; on Windows $SHELL when it is a
 * Windows program (not MSYS2's /usr/bin/bash), else PowerShell 7, Windows
 * PowerShell, then %COMSPEC% (cmd.exe). Caller frees. */
static gchar* user_shell(void) {
    const gchar* shell = g_getenv("SHELL");
#ifdef G_OS_WIN32
    gchar* found = (shell != NULL && shell[0] != '\0') ? g_find_program_in_path(shell) : NULL;
    const gchar* fallbacks[] = {"pwsh.exe", "powershell.exe", g_getenv("COMSPEC"), "cmd.exe"};
    for (gsize i = 0; found == NULL && i < G_N_ELEMENTS(fallbacks); i++) {
        found = fallbacks[i] != NULL ? g_find_program_in_path(fallbacks[i]) : NULL;
    }
    return found != NULL ? found : g_strdup("cmd.exe");
#else
    return g_strdup((shell != NULL && shell[0] != '\0') ? shell : "/bin/bash");
#endif
}

/* Shell in cwd (NULL: the explorer's directory), or ssh to the active remote */
static void spawn_shell(AppData* app, VteTerminal* term, const gchar* cwd) {
    RemoteMachine* rm = remote_get_active_machine(app);
    if (rm != NULL && rm->host != NULL && rm->host[0] != '\0') {
        GPtrArray* args = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_add(args, g_strdup("ssh"));
        g_ptr_array_add(args, g_strdup("-t"));
        if (rm->port > 0 && rm->port != 22) {
            g_ptr_array_add(args, g_strdup("-p"));
            g_ptr_array_add(args, g_strdup_printf("%d", rm->port));
        }
        if (rm->identity_file != NULL && rm->identity_file[0] != '\0') {
            g_ptr_array_add(args, g_strdup("-i"));
            g_ptr_array_add(args, g_strdup(rm->identity_file));
        }

        gchar* target = NULL;
        if (rm->user != NULL && rm->user[0] != '\0') {
            target = g_strdup_printf("%s@%s", rm->user, rm->host);
        } else {
            target = g_strdup(rm->host);
        }
        g_ptr_array_add(args, target);

        if (rm->remote_path != NULL && rm->remote_path[0] != '\0' &&
            g_strcmp0(rm->remote_path, "~") != 0 && g_strcmp0(rm->remote_path, "") != 0) {
            g_ptr_array_add(args, g_strdup_printf("cd %s 2>/dev/null; exec $SHELL -l", rm->remote_path));
        }
        g_ptr_array_add(args, NULL);

        vte_terminal_spawn_async(term, VTE_PTY_DEFAULT, NULL, (gchar**)args->pdata, NULL,
                                 G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                                 on_spawn_result, app);
        g_ptr_array_free(args, TRUE);
        return;
    }

    gchar* shell = user_shell();
    gchar* argv[2] = { shell, NULL };
    vte_terminal_spawn_async(term, VTE_PTY_DEFAULT, cwd != NULL ? cwd : app->base_dir, argv, NULL,
                             G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                             on_spawn_result, app);
    g_free(shell);
}

/* ------------------------------------------------------------------------- */
/* Terminal panes: notebooks of terminal tabs, split by GtkPaned             */
/* ------------------------------------------------------------------------- */

/* Each workspace's term_box holds one terminal notebook, or a tree of
 * GtkPaned whose leaves are terminal notebooks. Their tabs (one terminal
 * each) are reordered and dragged between notebooks with the mouse (see
 * tab_drop.h); a drop near a page edge splits that notebook. */

static void collect_notebooks(GtkWidget* widget, GPtrArray* out) {
    if (widget == NULL) {
        return;
    }
    if (GTK_IS_NOTEBOOK(widget)) {
        g_ptr_array_add(out, widget);
        return;
    }
    if (GTK_IS_PANED(widget)) {
        collect_notebooks(gtk_paned_get_start_child(GTK_PANED(widget)), out);
        collect_notebooks(gtk_paned_get_end_child(GTK_PANED(widget)), out);
        return;
    }
    if (GTK_IS_BOX(widget)) {
        for (GtkWidget* c = gtk_widget_get_first_child(widget); c != NULL;
             c = gtk_widget_get_next_sibling(c)) {
            collect_notebooks(c, out);
        }
    }
}

/* Every terminal under box (a workspace's term_box): notebooks in tree
 * order, tabs in order */
static GPtrArray* panes_in(GtkWidget* box) {
    GPtrArray* nbs = g_ptr_array_new();
    collect_notebooks(box, nbs);
    GPtrArray* panes = g_ptr_array_new();
    for (guint i = 0; i < nbs->len; i++) {
        GtkNotebook* nb = GTK_NOTEBOOK(g_ptr_array_index(nbs, i));
        gint n = gtk_notebook_get_n_pages(nb);
        for (gint p = 0; p < n; p++) {
            g_ptr_array_add(panes, gtk_notebook_get_nth_page(nb, p));
        }
    }
    g_ptr_array_unref(nbs);
    return panes;
}

static GPtrArray* term_all_panes(AppData* app) {
    return panes_in(cur_box(app));
}

/* The terminal shown by each notebook (its current tab) */
GPtrArray* term_visible_panes(AppData* app) {
    GPtrArray* nbs = g_ptr_array_new();
    collect_notebooks(cur_box(app), nbs);
    GPtrArray* panes = g_ptr_array_new();
    for (guint i = 0; i < nbs->len; i++) {
        GtkNotebook* nb = GTK_NOTEBOOK(g_ptr_array_index(nbs, i));
        gint cur = gtk_notebook_get_current_page(nb);
        if (cur >= 0 && gtk_widget_get_visible(GTK_WIDGET(nb))) {
            g_ptr_array_add(panes, gtk_notebook_get_nth_page(nb, cur));
        }
    }
    g_ptr_array_unref(nbs);
    return panes;
}

/* The terminal notebook holding vte as a page, or NULL */
static GtkNotebook* term_notebook_of(GtkWidget* vte) {
    GtkWidget* parent = gtk_widget_get_parent(vte);
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    if (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), vte) >= 0) {
        return GTK_NOTEBOOK(nb);
    }
    return NULL;
}

/* The workspace term_box w is in, or NULL */
static GtkWidget* term_root_box(AppData* app, GtkWidget* w) {
    for (; w != NULL; w = gtk_widget_get_parent(w)) {
        GtkWidget* parent = gtk_widget_get_parent(w);
        if (parent != NULL && parent == term_stack(app)) {
            return w;
        }
    }
    return NULL;
}

/* Tab label: the title the shell sets (xterm title), else "Terminal" */
static void term_update_tab_label(VteTerminal* term) {
    GtkNotebook* nb = term_notebook_of(GTK_WIDGET(term));
    GtkWidget* label = nb != NULL ? gtk_notebook_get_tab_label(nb, GTK_WIDGET(term)) : NULL;
    if (label == NULL || !GTK_IS_LABEL(label)) {
        return;
    }
    const char* title = NULL;
#if VTE_CHECK_VERSION(0, 78, 0)
    title = vte_terminal_get_termprop_string_by_id(term, VTE_PROPERTY_ID_XTERM_TITLE, NULL);
#endif
    gchar* text = (title != NULL && title[0] != '\0') ? g_utf8_make_valid(title, -1)
                                                       : g_strdup("Terminal");
    gtk_label_set_text(GTK_LABEL(label), text);
    gtk_widget_set_tooltip_text(label, text);
    g_free(text);
}

#if VTE_CHECK_VERSION(0, 78, 0)
static void on_term_title_changed(VteTerminal* term, const char* prop, gpointer data) {
    (void)prop;
    (void)data;
    term_update_tab_label(term);
}
#endif

static GtkWidget* term_tab_label_new(void) {
    GtkWidget* label = gtk_label_new("Terminal");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(label), 12); /* minimum width when tabs are crowded */
    gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
    return label;
}

/* Put vte in nb at pos (-1: append) as the current tab; label NULL: a new one */
static void term_notebook_add(GtkNotebook* nb, GtkWidget* vte, GtkWidget* label, gint pos) {
    if (label == NULL) {
        label = term_tab_label_new();
    }
    gint page = (pos < 0) ? gtk_notebook_append_page(nb, vte, label)
                          : gtk_notebook_insert_page(nb, vte, label, pos);
    gtk_notebook_set_tab_reorderable(nb, vte, TRUE);
    gtk_notebook_set_tab_detachable(nb, vte, TRUE);
    gtk_notebook_set_current_page(nb, page);
    term_update_tab_label(VTE_TERMINAL(vte));
}

/* Move vte, a tab of from, into to at index (-1: append) */
static void term_move_page(GtkNotebook* from, GtkNotebook* to, GtkWidget* vte, gint index) {
    GtkWidget* label = gtk_notebook_get_tab_label(from, vte);
    g_object_ref(vte);
    g_object_ref(label);
    if (gtk_notebook_get_tab_detachable(from, vte)) {
        gtk_notebook_detach_tab(from, vte);
    } else {
        gtk_notebook_remove_page(from, gtk_notebook_page_num(from, vte));
    }
    term_notebook_add(to, vte, label, index);
    g_object_unref(label);
    g_object_unref(vte);
}

/* Put `added` on the `side` half of cur's place (PANE_ZONE_LEFT/RIGHT/TOP/BOTTOM) */
static void term_split_beside(AppData* app, GtkWidget* cur, GtkWidget* added, PaneZone side) {
    GtkWidget* parent = gtk_widget_get_parent(cur);
    if (parent == NULL) {
        return;
    }
    GtkOrientation orient = (side == PANE_ZONE_RIGHT || side == PANE_ZONE_LEFT)
        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    GtkWidget* paned = gtk_paned_new(orient);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    wire_paned_controllers(paned, app);

    g_object_ref(cur);
    if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), cur);
        gtk_box_append(GTK_BOX(parent), paned);
    } else if (GTK_IS_PANED(parent)) {
        if (gtk_paned_get_start_child(GTK_PANED(parent)) == cur) {
            gtk_paned_set_start_child(GTK_PANED(parent), paned);
        } else {
            gtk_paned_set_end_child(GTK_PANED(parent), paned);
        }
    }
    gboolean after = (side == PANE_ZONE_RIGHT || side == PANE_ZONE_BOTTOM);
    gtk_paned_set_start_child(GTK_PANED(paned), after ? cur : added);
    gtk_paned_set_end_child(GTK_PANED(paned), after ? added : cur);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), TRUE);
    g_object_unref(cur);
}

/* Take w out of the pane tree; the split that held it gives way to its sibling */
static void term_remove_leaf(GtkWidget* w) {
    GtkWidget* parent = gtk_widget_get_parent(w);
    if (parent == NULL) {
        return;
    }
    /* A GtkPaned losing the child it last focused warns when the window
     * focus is elsewhere: clear the focus meanwhile, then restore it */
    GtkRoot* root = gtk_widget_get_root(w);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL) {
        g_object_ref(focus);
        gtk_root_set_focus(root, NULL);
    }
    if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), w);
    } else if (GTK_IS_PANED(parent)) {
        GtkPaned* paned = GTK_PANED(parent);
        GtkWidget* sibling = (gtk_paned_get_start_child(paned) == w)
                                 ? gtk_paned_get_end_child(paned)
                                 : gtk_paned_get_start_child(paned);
        GtkWidget* grandparent = gtk_widget_get_parent(parent);
        if (sibling != NULL) {
            g_object_ref(sibling);
        }
        gtk_paned_set_start_child(paned, NULL);
        gtk_paned_set_end_child(paned, NULL);
        if (GTK_IS_BOX(grandparent)) {
            gtk_box_remove(GTK_BOX(grandparent), parent);
            if (sibling != NULL) {
                gtk_box_append(GTK_BOX(grandparent), sibling);
            }
        } else if (GTK_IS_PANED(grandparent)) {
            if (gtk_paned_get_start_child(GTK_PANED(grandparent)) == parent) {
                gtk_paned_set_start_child(GTK_PANED(grandparent), sibling);
            } else {
                gtk_paned_set_end_child(GTK_PANED(grandparent), sibling);
            }
        }
        if (sibling != NULL) {
            g_object_unref(sibling);
        }
    }
    if (focus != NULL) {
        if (gtk_widget_get_root(focus) == root) {
            gtk_root_set_focus(root, focus);
        }
        g_object_unref(focus);
    }
}

/* The last terminal of the shown workspace is gone: hide the panel */
static void term_panel_closed(AppData* app) {
    /* The focus moves only if it was in the panel (not on a terminal that
     * went to the editor area) */
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    gboolean move_focus = (focus == NULL || gtk_widget_is_ancestor(focus, app->term_scroll));
    app_layout_begin(app);
    gtk_widget_set_visible(app->term_scroll, FALSE);
    gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) &&
                          gtk_widget_get_visible(app->editor_box);
    if (!files_vis && !preview_vis && !editor_vis && app->viewer_box != NULL) {
        gtk_widget_set_visible(app->viewer_box, TRUE);
        preview_vis = TRUE;
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    if (!move_focus) {
        return;
    }
    if (files_vis) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    } else if (preview_vis) {
        gtk_widget_grab_focus(app->text_view);
    }
}

/* Close the panel if the shown workspace has no terminal left (they all
 * went to the editor area) */
static void term_close_panel_if_empty(AppData* app) {
    if (cur_box(app) == NULL || app->term_scroll == NULL ||
        !gtk_widget_get_visible(app->term_scroll)) {
        return;
    }
    GPtrArray* panes = panes_in(cur_box(app));
    gboolean empty = (panes->len == 0);
    g_ptr_array_unref(panes);
    if (empty) {
        term_panel_closed(app);
    }
}

static guint close_if_empty_id = 0;

static gboolean term_close_if_empty_idle(gpointer data) {
    close_if_empty_id = 0;
    term_close_panel_if_empty((AppData*)data);
    return G_SOURCE_REMOVE;
}

/* A terminal notebook left without tabs: its last terminal may have gone to
 * the editor area. The panel closes now, not when the tab drag ends: GTK may
 * report that late or not at all. */
static void on_term_page_removed(GtkNotebook* nb, GtkWidget* child, guint num, gpointer data) {
    (void)child;
    (void)num;
    if (gtk_notebook_get_n_pages(nb) == 0 && close_if_empty_id == 0) {
        close_if_empty_id = g_idle_add(term_close_if_empty_idle, data);
    }
}

static void on_vte_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    AppData* app = (AppData*)data;
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    if (widget != NULL) {
        set_focus(app, widget);
        update_active_panel_highlight(app);
    }
}

/* After a click on a tab GTK leaves the focus on the tab bar: give it to
 * the terminal shown */
static gboolean term_focus_page_idle(gpointer data) {
    GtkNotebook* nb = GTK_NOTEBOOK(data);
    GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(nb));
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && (focus == GTK_WIDGET(nb) ||
                          (gtk_widget_is_ancestor(focus, GTK_WIDGET(nb)) && !VTE_IS_TERMINAL(focus)))) {
        gint cur = gtk_notebook_get_current_page(nb);
        if (cur >= 0) {
            gtk_widget_grab_focus(gtk_notebook_get_nth_page(nb, cur));
        }
    }
    g_object_unref(nb);
    return G_SOURCE_REMOVE;
}

static void on_term_switch_page(GtkNotebook* nb, GtkWidget* page, guint num, gpointer data) {
    (void)num;
    AppData* app = (AppData*)data;
    if (term_root_box(app, GTK_WIDGET(nb)) == cur_box(app) && VTE_IS_TERMINAL(page)) {
        set_focus(app, page);
    }
    g_idle_add(term_focus_page_idle, g_object_ref(nb));
}

static void on_term_tab_dropped(GtkNotebook* target, GtkWidget* child, PaneZone zone,
                                gint index, gpointer data);
static void on_term_tab_drag_done(GtkNotebook* source, gpointer data);

/* Terminal notebooks take terminal tabs, from the editor notebooks too */
static gboolean term_accepts_tab(GtkNotebook* target, GtkNotebook* source, GtkWidget* child,
                                 gpointer data) {
    (void)target;
    (void)source;
    (void)data;
    return VTE_IS_TERMINAL(child);
}

static gboolean is_term_notebook(GtkNotebook* nb) {
    return g_strcmp0(gtk_notebook_get_group_name(nb), TERM_TABS_GROUP) == 0;
}

static GtkWidget* term_notebook_new(AppData* app) {
    GtkWidget* nb = gtk_notebook_new();
    gtk_widget_set_name(nb, "term-notebook");
    gtk_widget_set_hexpand(nb, TRUE);
    gtk_widget_set_vexpand(nb, TRUE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(nb), TERM_TABS_GROUP);
    g_signal_connect_after(nb, "switch-page", G_CALLBACK(on_term_switch_page), app);
    g_signal_connect(nb, "page-removed", G_CALLBACK(on_term_page_removed), app);
    tab_drop_attach(GTK_NOTEBOOK(nb), TRUE, on_term_tab_dropped, on_term_tab_drag_done, app);
    tab_drop_set_accept(GTK_NOTEBOOK(nb), term_accepts_tab);
    return nb;
}

/* A terminal tab dragged with the mouse was dropped on target (tab_drop.h) */
static void on_term_tab_dropped(GtkNotebook* target, GtkWidget* child, PaneZone zone,
                                gint index, gpointer data) {
    AppData* app = (AppData*)data;
    GtkNotebook* source = VTE_IS_TERMINAL(child) ? term_notebook_of(child) : NULL;
    if (source == NULL) {
        return;
    }
    if (zone == PANE_ZONE_CENTER) {
        if (source == target) {
            gtk_notebook_reorder_child(target, child, index);
            gtk_notebook_set_current_page(target, gtk_notebook_page_num(target, child));
        } else {
            term_move_page(source, target, child, index);
        }
    } else {
        if (source == target && gtk_notebook_get_n_pages(target) <= 1) {
            return;
        }
        GtkWidget* nb = term_notebook_new(app);
        term_split_beside(app, GTK_WIDGET(target), nb, zone);
        term_move_page(source, GTK_NOTEBOOK(nb), child, -1);
    }
    set_focus(app, child);
    gtk_widget_grab_focus(child);
}

static gboolean term_drag_done_idle(gpointer data) {
    GtkNotebook* source = GTK_NOTEBOOK(data);
    AppData* app = g_object_get_data(G_OBJECT(source), "app");
    GtkWidget* w = GTK_WIDGET(source);
    if (app != NULL && gtk_widget_get_parent(w) != NULL && gtk_notebook_get_n_pages(source) == 0) {
        term_remove_leaf(w);
        term_close_panel_if_empty(app);
    }
    g_object_unref(source);
    return G_SOURCE_REMOVE;
}

/* The drag of a tab out of source has ended: remove source if left empty */
static void on_term_tab_drag_done(GtkNotebook* source, gpointer data) {
    AppData* app = (AppData*)data;
    GtkWidget* w = GTK_WIDGET(source);
    if (gtk_widget_get_parent(w) == NULL || gtk_notebook_get_n_pages(source) > 0) {
        return;
    }
    g_object_set_data(G_OBJECT(source), "app", app);
    g_idle_add(term_drag_done_idle, g_object_ref(source));
}

/* Notebook new terminals go to: the one of the terminal focused last, else
 * the last one; NULL without terminals */
static GtkNotebook* term_target_notebook(AppData* app) {
    GPtrArray* panes = term_all_panes(app);
    GtkWidget* target = NULL;
    for (guint i = 0; i < panes->len && cur_focus(app) != NULL; i++) {
        if (g_ptr_array_index(panes, i) == cur_focus(app)) {
            target = cur_focus(app);
        }
    }
    if (target == NULL && panes->len > 0) {
        target = g_ptr_array_index(panes, panes->len - 1);
    }
    g_ptr_array_unref(panes);
    return target != NULL ? term_notebook_of(target) : NULL;
}

/* Ctrl+Shift+C / Ctrl+Shift+V: copy the selection, paste the clipboard */
static gboolean on_term_copy_paste_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                                       GdkModifierType state, gpointer data) {
    (void)keycode;
    (void)data;
    if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK)) != (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        return FALSE;
    }
    VteTerminal* term = VTE_TERMINAL(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl)));
    if (keyval == GDK_KEY_C || keyval == GDK_KEY_c) {
        vte_terminal_copy_clipboard_format(term, VTE_FORMAT_TEXT);
        return TRUE;
    }
    if (keyval == GDK_KEY_V || keyval == GDK_KEY_v) {
        vte_terminal_paste_clipboard(term);
        return TRUE;
    }
    return FALSE;
}

/* New terminal widget running a shell (not placed yet) */
static GtkWidget* term_new_pane(AppData* app, const gchar* cwd) {
    GtkWidget* vte = vte_terminal_new();
    gtk_widget_set_hexpand(vte, TRUE);
    gtk_widget_set_vexpand(vte, TRUE);
    apply_terminal_theme(VTE_TERMINAL(vte));
    if (all_terminals == NULL) {
        all_terminals = g_ptr_array_new();
    }
    g_ptr_array_add(all_terminals, vte);
    g_object_weak_ref(G_OBJECT(vte), on_terminal_finalized, app);
    g_signal_connect(vte, "child-exited",
                     G_CALLBACK(on_term_child_exited), app);
    g_signal_connect(vte, "realize", G_CALLBACK(on_vte_realize), app);
#if VTE_CHECK_VERSION(0, 78, 0)
    g_signal_connect(vte, "termprop-changed::" VTE_TERMPROP_XTERM_TITLE,
                     G_CALLBACK(on_term_title_changed), NULL);
#endif

    GtkEventController* focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_vte_focus_enter), app);
    gtk_widget_add_controller(vte, focus_ctrl);

    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_term_copy_paste_key), NULL);
    gtk_widget_add_controller(vte, keys);

    spawn_shell(app, VTE_TERMINAL(vte), cwd);
    return vte;
}

/* A new terminal: a tab of the target notebook, or (new_row) a notebook of
 * its own below it */
static void term_spawn_pane_in(AppData* app, gboolean new_row, const gchar* cwd) {
    GtkWidget* vte = term_new_pane(app, cwd);
    GtkNotebook* target = term_target_notebook(app);
    if (target == NULL || new_row) {
        GtkWidget* nb = term_notebook_new(app);
        if (target == NULL) {
            /* No terminal left: drop the empty notebooks a tab drag left */
            for (GtkWidget* c = gtk_widget_get_first_child(cur_box(app)); c != NULL;
                 c = gtk_widget_get_first_child(cur_box(app))) {
                gtk_box_remove(GTK_BOX(cur_box(app)), c);
            }
            gtk_box_append(GTK_BOX(cur_box(app)), nb);
        } else {
            term_split_beside(app, GTK_WIDGET(target), nb, PANE_ZONE_BOTTOM);
        }
        target = GTK_NOTEBOOK(nb);
    }
    term_notebook_add(target, vte, NULL, -1);
    set_focus(app, vte);
}

void term_spawn_pane(AppData* app, gboolean new_row) {
    term_spawn_pane_in(app, new_row, NULL);
}

GtkWidget* terminal_add_tab(AppData* app, GtkNotebook* nb) {
    GtkWidget* vte = term_new_pane(app, NULL);
    term_notebook_add(nb, vte, NULL, -1);
    return vte;
}

void terminal_move_tab(GtkWidget* vte, GtkNotebook* nb) {
    GtkNotebook* from = (vte != NULL) ? term_notebook_of(vte) : NULL;
    if (from != NULL && nb != NULL) {
        term_move_page(from, nb, vte, -1);
    }
}

gboolean terminal_step_tab(AppData* app, gint step) {
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    GtkWidget* vte = focus != NULL ? gtk_widget_get_ancestor(focus, VTE_TYPE_TERMINAL) : NULL;
    GtkNotebook* nb = vte != NULL ? term_notebook_of(vte) : NULL;
    /* In the editor area too: the next tab may be an editor tab */
    return nb != NULL && focus_route_step_tab(app, nb, step, TRUE);
}

void on_term_child_exited(VteTerminal* term, gint status, gpointer data) {
    (void)status;
    AppData* app = (AppData*)data;
    GtkWidget* term_w = GTK_WIDGET(term);
    terminal_forget(app, term_w);
    GtkNotebook* nb = term_notebook_of(term_w);
    if (nb == NULL) {
        return;
    }
    if (!is_term_notebook(nb)) {
        editor_remove_page(app, term_w); /* a terminal moved to the editor area */
        return;
    }
    GtkWidget* box = term_root_box(app, GTK_WIDGET(nb));
    gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, term_w));
    gboolean nb_left = gtk_notebook_get_n_pages(nb) > 0;
    if (!nb_left) {
        term_remove_leaf(GTK_WIDGET(nb));
    }
    if (box == NULL || box != cur_box(app)) {
        return;
    }
    GPtrArray* panes = term_visible_panes(app);
    if (panes->len == 0) {
        term_panel_closed(app);
    } else if (nb_left) {
        gtk_widget_grab_focus(gtk_notebook_get_nth_page(nb, gtk_notebook_get_current_page(nb)));
    } else {
        gtk_widget_grab_focus(GTK_WIDGET(g_ptr_array_index(panes, 0)));
    }
    g_ptr_array_unref(panes);
}

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
    return term_layout_capture_box(cur_box(app));
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
    for (GtkWidget* c = gtk_widget_get_first_child(cur_box(app)); c != NULL;
         c = gtk_widget_get_first_child(cur_box(app))) {
        gtk_box_remove(GTK_BOX(cur_box(app)), c);
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

void term_layout_apply_spare(AppData* app, const gchar* text, GPtrArray* spare) {
    TermLayoutNode* tree = term_layout_parse(text);
    if (tree == NULL || cur_box(app) == NULL) {
        term_layout_free(tree);
        return;
    }
    /* The running terminals in order, then those set aside before */
    GPtrArray* existing = take_all_out(app);
    while (spare != NULL && spare->len > 0) {
        g_ptr_array_add(existing, g_ptr_array_steal_index(spare, 0));
    }
    guint next = 0;
    gtk_box_append(GTK_BOX(cur_box(app)), build_panes(app, tree, existing, &next));
    g_ptr_array_remove_range(existing, 0, next);
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
    GtkWidget* root = (cur_box(app) != NULL) ? gtk_widget_get_first_child(cur_box(app)) : NULL;
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
    if (cur_box(app) == NULL) {
        return;
    }
    GPtrArray* taken = take_all_out(app);
    GtkWidget* root = (snap != NULL) ? rebuild_snapshot(app, snap, taken, spare) : NULL;
    if (root != NULL) {
        gtk_box_append(GTK_BOX(cur_box(app)), root);
    }
    set_aside(app, taken, spare);
    g_ptr_array_unref(taken);
}

/* ------------------------------------------------------------------------- */
/* Terminal tabs of the preview notebook, kept by their workspace            */
/* ------------------------------------------------------------------------- */

#define TAB_LABEL_KEY "term-tab-label"

void terminal_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app->notebook == NULL || ws->term == NULL) {
        return;
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (gint p = gtk_notebook_get_n_pages(nb) - 1; p >= 0; p--) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
        if (!VTE_IS_TERMINAL(page)) {
            continue;
        }
        if (ws->term->preview_tabs == NULL) {
            ws->term->preview_tabs = g_ptr_array_new_with_free_func(g_object_unref);
        }
        g_object_set_data_full(G_OBJECT(page), TAB_LABEL_KEY,
                               g_object_ref(gtk_notebook_get_tab_label(nb, page)), g_object_unref);
        g_ptr_array_insert(ws->term->preview_tabs, 0, g_object_ref(page));
        gtk_notebook_remove_page(nb, p);
    }
}

void terminal_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (ws->term == NULL) {
        return;
    }
    if (term_stack(app) != NULL && ws->term->box != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(term_stack(app)), ws->term->box);
    }
    if (ws->term->preview_tabs == NULL || app->notebook == NULL) {
        return;
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (guint i = 0; i < ws->term->preview_tabs->len; i++) {
        GtkWidget* vte = g_ptr_array_index(ws->term->preview_tabs, i);
        GtkWidget* label = g_object_get_data(G_OBJECT(vte), TAB_LABEL_KEY);
        gtk_notebook_append_page(nb, vte, label);
        gtk_notebook_set_tab_reorderable(nb, vte, TRUE);
        gtk_notebook_set_tab_detachable(nb, vte, TRUE);
        g_object_set_data(G_OBJECT(vte), TAB_LABEL_KEY, NULL);
    }
    g_clear_pointer(&ws->term->preview_tabs, g_ptr_array_unref);
}

static void term_workspace_free(VirtualLayout* ws) {
    if (ws->term == NULL) {
        return;
    }
    /* Their shells end with the widgets */
    g_clear_pointer(&ws->term->preview_tabs, g_ptr_array_unref);
    g_clear_pointer(&ws->term, g_free);
}

void terminal_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (ws->term == NULL) {
        return;
    }
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces &&
        ws == &app->workspaces[app->current_ws]) {
        terminal_workspace_hidden(app, ws);
    }
    if (term_stack(app) != NULL && ws->term->box != NULL) {
        gtk_stack_remove(GTK_STACK(term_stack(app)), ws->term->box);
    }
    term_workspace_free(ws);
}

void terminal_init(AppData* app, GtkWidget* panel) {
    app->terminal = g_new0(struct Terminal, 1);
    app->terminal->stack = gtk_stack_new();
    gtk_widget_set_name(app->terminal->stack, "term-box");
    gtk_widget_set_hexpand(app->terminal->stack, TRUE);
    gtk_widget_set_vexpand(app->terminal->stack, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(panel), app->terminal->stack);
}

void terminal_free(AppData* app) {
    for (gint i = 0; i < app->num_workspaces; i++) {
        term_workspace_free(&app->workspaces[i]);
    }
    g_clear_pointer(&app->terminal, g_free);
}

void terminal_workspace_init(AppData* app, VirtualLayout* ws) {
    ws->term = g_new0(struct TermWorkspace, 1);
    if (term_stack(app) != NULL) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(box, TRUE);
        gtk_widget_set_vexpand(box, TRUE);
        gtk_stack_add_child(GTK_STACK(term_stack(app)), box);
        ws->term->box = box;
    }
}

GtkWidget* terminal_workspace_box(const VirtualLayout* ws) {
    return ws->term != NULL ? ws->term->box : NULL;
}

guint terminal_workspace_preview_tab_count(const VirtualLayout* ws) {
    return (ws->term != NULL && ws->term->preview_tabs != NULL) ? ws->term->preview_tabs->len : 0;
}

GtkWidget* terminal_last_focused(AppData* app) {
    return cur_focus(app);
}

void terminal_forget(AppData* app, GtkWidget* vte) {
    for (gint i = 0; i < app->num_workspaces; i++) {
        struct TermWorkspace* t = app->workspaces[i].term;
        if (t != NULL && t->last_focused == vte) {
            t->last_focused = NULL;
        }
    }
}

void on_vte_realize(GtkWidget* term, gpointer data) {
    (void)data;
    gtk_widget_grab_focus(term);
}

#endif /* HAVE_VTE */


void terminal_open(AppData* app, gboolean new_row, const gchar* cwd) {
#ifdef HAVE_VTE
    /* Each pane gets its own shell */
    gboolean was_term_vis = gtk_widget_get_visible(app->term_scroll);
    if (!was_term_vis) {
        app_layout_begin(app);
    }
    if (app->right_paned != NULL) {
        gtk_widget_set_visible(app->right_paned, TRUE);
    }
    if (!was_term_vis) {
        gtk_widget_set_visible(app->term_scroll, TRUE);
    }
    term_spawn_pane_in(app, new_row, cwd);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    if (!was_term_vis) {
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
#else
    (void)new_row;
    (void)cwd;
    /* No VTE installed: the panel is a notice instead of a terminal */
    gboolean was_term_vis = gtk_widget_get_visible(app->term_scroll);
    app_layout_begin(app);
    gtk_widget_set_visible(app->term_scroll, !was_term_vis);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
#endif
}
