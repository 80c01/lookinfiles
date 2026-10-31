#include "terminal.h"
#include "panels.h"
#include "app_events.h"
#include "remote_store.h"
#include "layout_store.h"
#include "palette.h"
#include "fonts.h"

#ifdef HAVE_VTE

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
    (void)data;
    if (all_terminals != NULL) {
        g_ptr_array_remove_fast(all_terminals, where);
    }
}

void terminal_refresh_theme(void) {
    for (guint i = 0; all_terminals != NULL && i < all_terminals->len; i++) {
        apply_terminal_theme(VTE_TERMINAL(g_ptr_array_index(all_terminals, i)));
    }
}

/* Async spawn callback: report failures, then hand child PID to terminal */
static void on_spawn_result(VteTerminal* term, GPid pid, GError* error,
                            gpointer data) {
    (void)data;
    if (error != NULL) {
        g_printerr("[ Cannot start terminal shell: %s ]\n", error->message);
        g_error_free(error);
        return;
    }
    vte_terminal_watch_child(term, pid);
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

    const gchar* shell = g_getenv("SHELL");
    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/bash";
    }
    gchar* argv[2] = { (gchar*)shell, NULL };
    vte_terminal_spawn_async(term, VTE_PTY_DEFAULT, cwd != NULL ? cwd : app->base_dir, argv, NULL,
                             G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                             on_spawn_result, app);
}

static void collect_vte_panes(GtkWidget* widget, GPtrArray* panes) {
    if (widget == NULL || !gtk_widget_get_visible(widget)) {
        return;
    }
    if (VTE_IS_TERMINAL(widget)) {
        g_ptr_array_add(panes, widget);
        return;
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        collect_vte_panes(child, panes);
    }
}

GPtrArray* term_visible_panes(AppData* app) {
    GPtrArray* panes = g_ptr_array_new();
    collect_vte_panes(app->term_box, panes);
    return panes;
}

static void on_vte_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    AppData* app = (AppData*)data;
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    if (widget != NULL) {
        app->last_focused_vte = widget;
        update_active_panel_highlight(app);
    }
}

static GtkWidget* get_target_pane(AppData* app) {
    GPtrArray* panes = term_visible_panes(app);
    GtkWidget* target = NULL;
    if (panes->len > 0) {
        if (app->last_focused_vte != NULL) {
            for (guint i = 0; i < panes->len; i++) {
                if (g_ptr_array_index(panes, i) == app->last_focused_vte) {
                    target = app->last_focused_vte;
                    break;
                }
            }
        }
        if (target == NULL) {
            target = GTK_WIDGET(g_ptr_array_index(panes, panes->len - 1));
        }
    }
    g_ptr_array_unref(panes);
    return target;
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
    g_object_weak_ref(G_OBJECT(vte), on_terminal_finalized, NULL);
    g_signal_connect(vte, "child-exited",
                     G_CALLBACK(on_term_child_exited), app);
    g_signal_connect(vte, "realize", G_CALLBACK(on_vte_realize), app);

    GtkEventController* focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_vte_focus_enter), app);
    gtk_widget_add_controller(vte, focus_ctrl);

    spawn_shell(app, VTE_TERMINAL(vte), cwd);
    return vte;
}

static void term_spawn_pane_in(AppData* app, gboolean new_row, const gchar* cwd) {
    GtkWidget* vte = term_new_pane(app, cwd);
    GtkWidget* target = get_target_pane(app);
    if (target == NULL) {
        /* First terminal: placed directly in term_box */
        gtk_box_append(GTK_BOX(app->term_box), vte);
    } else {
        GtkWidget* parent = gtk_widget_get_parent(target);
        GtkOrientation orient = new_row ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
        GtkWidget* new_paned = gtk_paned_new(orient);
        gtk_widget_set_hexpand(new_paned, TRUE);
        gtk_widget_set_vexpand(new_paned, TRUE);
        wire_paned_controllers(new_paned, app);

        g_object_ref(target);
        if (parent == app->term_box) {
            gtk_box_remove(GTK_BOX(app->term_box), target);
            gtk_paned_set_start_child(GTK_PANED(new_paned), target);
            gtk_paned_set_resize_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_end_child(GTK_PANED(new_paned), vte);
            gtk_paned_set_resize_end_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_end_child(GTK_PANED(new_paned), TRUE);
            gtk_box_append(GTK_BOX(app->term_box), new_paned);
        } else if (GTK_IS_PANED(parent)) {
            gboolean is_start = (gtk_paned_get_start_child(GTK_PANED(parent)) == target);
            if (is_start) {
                gtk_paned_set_start_child(GTK_PANED(parent), NULL);
            } else {
                gtk_paned_set_end_child(GTK_PANED(parent), NULL);
            }
            gtk_paned_set_start_child(GTK_PANED(new_paned), target);
            gtk_paned_set_resize_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_start_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_end_child(GTK_PANED(new_paned), vte);
            gtk_paned_set_resize_end_child(GTK_PANED(new_paned), TRUE);
            gtk_paned_set_shrink_end_child(GTK_PANED(new_paned), TRUE);
            if (is_start) {
                gtk_paned_set_start_child(GTK_PANED(parent), new_paned);
            } else {
                gtk_paned_set_end_child(GTK_PANED(parent), new_paned);
            }
        }
        g_object_unref(target);
    }

    app->last_focused_vte = vte;
}

void term_spawn_pane(AppData* app, gboolean new_row) {
    term_spawn_pane_in(app, new_row, NULL);
}

void on_term_child_exited(VteTerminal* term, gint status, gpointer data) {
    (void)status;
    AppData* app = (AppData*)data;
    GtkWidget* term_w = GTK_WIDGET(term);

    if (app->last_focused_vte == term_w) {
        app->last_focused_vte = NULL;
    }

    GtkWidget* parent = gtk_widget_get_parent(term_w);
    if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), term_w);
        if (parent == app->term_box) {
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->term_scroll, FALSE);
            gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
            gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
            if (!files_vis && !preview_vis && app->viewer_box != NULL) {
                gtk_widget_set_visible(app->viewer_box, TRUE);
                preview_vis = TRUE;
            }
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
            if (files_vis) {
                gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
            } else if (preview_vis) {
                gtk_widget_grab_focus(app->text_view);
            }
        }
        return;
    }

    if (GTK_IS_PANED(parent)) {
        GtkPaned* parent_paned = GTK_PANED(parent);
        GtkWidget* sibling = (gtk_paned_get_start_child(parent_paned) == term_w) ?
                             gtk_paned_get_end_child(parent_paned) :
                             gtk_paned_get_start_child(parent_paned);
        GtkWidget* grandparent = gtk_widget_get_parent(parent);

        g_object_ref(sibling);
        gtk_paned_set_start_child(parent_paned, NULL);
        gtk_paned_set_end_child(parent_paned, NULL);

        if (GTK_IS_BOX(grandparent)) {
            gtk_box_remove(GTK_BOX(grandparent), parent);
            gtk_box_append(GTK_BOX(grandparent), sibling);
        } else if (GTK_IS_PANED(grandparent)) {
            gboolean is_start = (gtk_paned_get_start_child(GTK_PANED(grandparent)) == parent);
            if (is_start) {
                gtk_paned_set_start_child(GTK_PANED(grandparent), sibling);
            } else {
                gtk_paned_set_end_child(GTK_PANED(grandparent), sibling);
            }
        }
        g_object_unref(sibling);

        GPtrArray* panes = term_visible_panes(app);
        if (panes->len > 0) {
            gtk_widget_grab_focus(GTK_WIDGET(g_ptr_array_index(panes, 0)));
        } else {
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->term_scroll, FALSE);
            gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
            gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
            if (!files_vis && !preview_vis && app->viewer_box != NULL) {
                gtk_widget_set_visible(app->viewer_box, TRUE);
                preview_vis = TRUE;
            }
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
            if (files_vis) {
                gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
            } else if (preview_vis) {
                gtk_widget_grab_focus(app->text_view);
            }
        }
        g_ptr_array_unref(panes);
    }
}

/* ------------------------------------------------------------------------- */
/* Pane tree of saved layouts (layout_store.h)                               */
/* ------------------------------------------------------------------------- */

static const gchar* PANED_FRAC_KEY = "term-paned-frac"; /* also set by panels.c */

static void describe_pane(GtkWidget* w, GString* out) {
    if (VTE_IS_TERMINAL(w)) {
        g_string_append_c(out, 'T');
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
    GString* out = g_string_new(NULL);
    GtkWidget* root = (app->term_box != NULL) ? gtk_widget_get_first_child(app->term_box) : NULL;
    if (root != NULL) {
        describe_pane(root, out);
    }
    return g_string_free(out, FALSE);
}

guint term_pane_count(AppData* app) {
    GPtrArray* panes = term_visible_panes(app);
    guint n = panes->len;
    g_ptr_array_unref(panes);
    return n;
}

static GtkWidget* build_panes(AppData* app, const TermLayoutNode* node, GPtrArray* existing,
                              guint* next) {
    if (node->is_terminal) {
        return (*next < existing->len) ? g_ptr_array_index(existing, (*next)++) : term_new_pane(app, NULL);
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

void term_layout_apply(AppData* app, const gchar* text) {
    TermLayoutNode* tree = term_layout_parse(text);
    if (tree == NULL || app->term_box == NULL) {
        term_layout_free(tree);
        return;
    }
    /* Take the running terminals out of the old tree, in order */
    GPtrArray* existing = term_visible_panes(app);
    for (guint i = 0; i < existing->len; i++) {
        GtkWidget* vte = g_ptr_array_index(existing, i);
        GtkWidget* parent = gtk_widget_get_parent(vte);
        g_object_ref(vte);
        if (GTK_IS_PANED(parent)) {
            if (gtk_paned_get_start_child(GTK_PANED(parent)) == vte) {
                gtk_paned_set_start_child(GTK_PANED(parent), NULL);
            } else {
                gtk_paned_set_end_child(GTK_PANED(parent), NULL);
            }
        } else if (GTK_IS_BOX(parent)) {
            gtk_box_remove(GTK_BOX(parent), vte);
        }
    }
    for (GtkWidget* c = gtk_widget_get_first_child(app->term_box); c != NULL;
         c = gtk_widget_get_first_child(app->term_box)) {
        gtk_box_remove(GTK_BOX(app->term_box), c);
    }

    guint next = 0;
    gtk_box_append(GTK_BOX(app->term_box), build_panes(app, tree, existing, &next));
    /* Terminals left over (the caller confirmed it) close with their widget */
    for (guint i = 0; i < existing->len; i++) {
        GtkWidget* vte = g_ptr_array_index(existing, i);
        if (i >= next && app->last_focused_vte == vte) {
            app->last_focused_vte = NULL;
        }
        g_object_unref(vte);
    }
    g_ptr_array_unref(existing);
    term_layout_free(tree);
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
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
        app->applying_layout = TRUE;
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
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->term_scroll, !was_term_vis);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
#endif
}
