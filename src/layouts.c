/* Saved workspace layouts: capture, apply and the list dialog (see layouts.h) */
#include "layouts.h"
#include "app_events.h"
#include "dialog.h"
#include "editor.h"
#include "editor_layout.h"
#include "layout_store.h"
#include "terminal.h"
#include "workspace.h"

enum { RESPONSE_SAVE = 1, RESPONSE_DELETE = 2 };

/* ------------------------------------------------------------------------- */
/* Capture and apply                                                         */
/* ------------------------------------------------------------------------- */

SavedLayout* layouts_capture(AppData* app, const gchar* name) {
    app_emit(app, APP_EVENT_LAYOUT_CHANGED); /* store the live state in the workspace */
    const VirtualLayout* ws = &app->workspaces[app->current_ws];
    SavedLayout* l = saved_layout_new(name);
    l->files_vis = ws->files_vis;
    l->preview_vis = ws->preview_vis;
    l->term_vis = ws->term_vis;
    l->editor_dedicated = ws->editor_dedicated;
    l->stacked = (ws->right_orient == GTK_ORIENTATION_VERTICAL);
    l->main_frac = ws->main_frac;
    l->right_frac = ws->right_frac;
    l->editor_frac = ws->editor_frac;
#ifdef HAVE_VTE
    g_free(l->terminals);
    l->terminals = term_layout_capture(app);
#endif
    g_free(l->editor_splits);
    l->editor_splits = editor_layout_capture(app);
    return l;
}

static gboolean confirm(AppData* app, const gchar* title, const gchar* message, const gchar* ok_label) {
    GtkWidget* dlg = app_dialog_new(title, app->window, "_Cancel", APP_RESPONSE_CANCEL,
                                    ok_label, APP_RESPONSE_OK, NULL);
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 16);
    gtk_widget_set_margin_end(label, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), label);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    gint resp = app_dialog_run(APP_DIALOG(dlg),
                               app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    return resp == APP_RESPONSE_OK;
}

/* Record the panels of a layout in the current workspace (shown by the caller) */
static void set_panels(AppData* app, const SavedLayout* l) {
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    ws->files_vis = l->files_vis;
    ws->preview_vis = l->preview_vis;
    ws->term_vis = l->term_vis;
    ws->right_orient = l->stacked ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    ws->main_frac = l->main_frac;
    ws->right_frac = l->right_frac;
    ws->editor_frac = l->editor_frac;
    ws->editor_dedicated = app->editor_dedicated;
    if (!ws->files_vis && !ws->preview_vis && !ws->term_vis && !ws->editor_dedicated) {
        ws->preview_vis = TRUE; /* never leave the window empty */
    }
}

/* Running terminals (term_running in the terminal panel, editor_running in
 * the editor panel) that applying l closes: those beyond its terminals (a
 * layout without terminals keeps them) */
static guint closing_terminals(const SavedLayout* l, guint term_running, guint editor_running) {
    guint closing = 0;
#ifdef HAVE_VTE
    TermLayoutNode* tree = l->term_vis ? term_layout_parse(l->terminals) : NULL;
    guint wanted = term_layout_count(tree);
    term_layout_free(tree);
    closing += (wanted > 0 && term_running > wanted) ? term_running - wanted : 0;
    gboolean splits = l->editor_dedicated && l->editor_splits != NULL && l->editor_splits[0] != '\0';
    guint editor_wanted = splits ? editor_layout_wanted_terminals(l->editor_splits) : 0;
    closing += (editor_wanted > 0 && editor_running > editor_wanted) ? editor_running - editor_wanted : 0;
#else
    (void)l;
    (void)term_running;
    (void)editor_running;
#endif
    return closing;
}

static gboolean confirm_closing(AppData* app, const SavedLayout* l, guint closing) {
    if (closing == 0) {
        return TRUE;
    }
    gchar* msg = g_strdup_printf("The layout “%s” has fewer terminals than are running; %u "
                                 "running terminal%s will be closed.", l->name, closing,
                                 closing == 1 ? "" : "s");
    gboolean ok = confirm(app, "Close Terminals", msg, "_Apply Layout");
    g_free(msg);
    return ok;
}

/* Arrange the editor tabs, terminals and panels of l (shown by the caller).
 * With spares, nothing closes: extra terminals go there, missing ones come
 * from there first. */
static void arrange(AppData* app, const SavedLayout* l, GPtrArray* term_spare, GPtrArray* editor_spare) {
    /* The editor panel's shape, when the layout saved one */
    gboolean splits = l->editor_dedicated && l->editor_splits != NULL && l->editor_splits[0] != '\0';
    if (l->editor_dedicated != app->editor_dedicated) {
        editor_set_dedicated(app, l->editor_dedicated); /* moves the open tabs */
    }
    if (splits) {
        editor_layout_apply_spare(app, l->editor_splits, editor_spare);
    }
#ifdef HAVE_VTE
    TermLayoutNode* tree = l->term_vis ? term_layout_parse(l->terminals) : NULL;
    if (term_layout_count(tree) > 0) {
        term_layout_apply_spare(app, l->terminals, term_spare);
    }
    term_layout_free(tree);
#else
    (void)term_spare;
#endif
    set_panels(app, l);
}

static guint running_terminals(AppData* app) {
#ifdef HAVE_VTE
    return term_pane_count(app);
#else
    (void)app;
    return 0;
#endif
}

gboolean layouts_apply(AppData* app, const SavedLayout* l) {
    if (!confirm_closing(app, l, closing_terminals(l, running_terminals(app), editor_layout_terminal_count(app)))) {
        return FALSE;
    }
    arrange(app, l, NULL, NULL);
    workspace_switch(app, app->current_ws); /* re-applies the current workspace */
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Previews                                                                  */
/* ------------------------------------------------------------------------- */

struct LayoutsPreview {
    AppData* app;
    SavedLayout* panels;      /* the panels at the start */
    EditorSnapshot* editor;   /* the editor tabs at the start */
#ifdef HAVE_VTE
    TermSnapshot* terms;      /* the terminal panel at the start */
#endif
    GPtrArray* term_spare;    /* terminals out of the panels meanwhile (referenced) */
    GPtrArray* editor_spare;
    guint term_running;       /* terminals at the start */
    guint editor_running;
};

LayoutsPreview* layouts_preview_begin(AppData* app) {
    LayoutsPreview* p = g_new0(LayoutsPreview, 1);
    p->app = app;
    p->panels = layouts_capture(app, "");
    p->editor = editor_layout_snapshot(app);
#ifdef HAVE_VTE
    p->terms = term_snapshot_take(app);
#endif
    p->term_spare = g_ptr_array_new_with_free_func(g_object_unref);
    p->editor_spare = g_ptr_array_new_with_free_func(g_object_unref);
    p->term_running = running_terminals(app);
    p->editor_running = editor_layout_terminal_count(app);
    return p;
}

/* Everything back as at the start: terminals started meanwhile go to the spares */
static void restore_start(LayoutsPreview* p) {
    editor_layout_restore(p->app, p->editor, p->editor_spare);
#ifdef HAVE_VTE
    term_snapshot_restore(p->app, p->terms, p->term_spare);
#endif
    set_panels(p->app, p->panels);
}

void layouts_preview_show(LayoutsPreview* p, const SavedLayout* l) {
    restore_start(p);
    if (l != NULL) {
        arrange(p->app, l, p->term_spare, p->editor_spare);
    }
    workspace_show_panels(p->app);
}

gboolean layouts_preview_end(LayoutsPreview* p, const SavedLayout* l) {
    AppData* app = p->app;
    /* Asked while the layout is shown: the terminals it closes are those
     * running at the start */
    gboolean apply = (l != NULL) && confirm_closing(app, l, closing_terminals(l, p->term_running, p->editor_running));
    restore_start(p);
    /* The shells started for the previews end here */
    g_ptr_array_unref(p->term_spare);
    g_ptr_array_unref(p->editor_spare);
    if (apply) {
        arrange(app, l, NULL, NULL);
        workspace_switch(app, app->current_ws); /* re-applies the current workspace */
    } else {
        workspace_show_panels(app);
    }
    editor_snapshot_free(p->editor);
#ifdef HAVE_VTE
    term_snapshot_free(p->terms);
#endif
    saved_layout_free(p->panels);
    g_free(p);
    return apply;
}

/* ------------------------------------------------------------------------- */
/* Dialogs                                                                   */
/* ------------------------------------------------------------------------- */

static void notice(AppData* app, const gchar* title, const gchar* message) {
    GtkWidget* dlg = app_dialog_new(title, app->window, "_Close", APP_RESPONSE_CLOSE, NULL);
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 16);
    gtk_widget_set_margin_end(label, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), label);
    app_dialog_run(APP_DIALOG(dlg), NULL);
    app_dialog_destroy(APP_DIALOG(dlg));
}

/* Layouts shown in the dialog: the default layout first, then the named ones */
typedef struct {
    AppData* app;
    SavedLayout* def;    /* default layout of new workspaces */
    GPtrArray* layouts;  /* named layouts */
    GtkListBox* list;
    AppDialog* dialog;
} LayoutsDialog;

static void save_store(LayoutsDialog* d) {
    gchar* path = layout_store_default_path();
    GError* err = NULL;
    if (!layout_store_save(path, d->layouts, d->def, &err)) {
        gchar* msg = g_strdup_printf("Could not save %s:\n%s", path, err ? err->message : "");
        notice(d->app, "Layouts Not Saved", msg);
        g_free(msg);
        g_clear_error(&err);
    }
    g_free(path);
}

static const gchar* DEFAULT_NAME = "Default";

/* Ask a name and save the current layout under it ("Default" replaces the
 * default layout). Returns the list index saved to (0 = default), or -1. */
static gint save_current(LayoutsDialog* d, const gchar* suggestion) {
    AppData* app = d->app;
    GtkWidget* dlg = app_dialog_new("Save Layout", app->window, "_Cancel", APP_RESPONSE_CANCEL,
                                    "_Save", APP_RESPONSE_OK, NULL);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    GtkWidget* label = gtk_label_new("Save the panels of this workspace as "
                                     "(“Default” for new workspaces):");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), suggestion);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 380);

    gchar* name = NULL;
    while (app_dialog_run(APP_DIALOG(dlg), entry) == APP_RESPONSE_OK) {
        name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));
        gboolean is_default = (g_ascii_strcasecmp(name, DEFAULT_NAME) == 0);
        gchar* msg = NULL;
        if (name[0] == '\0') {
            g_clear_pointer(&name, g_free);
            continue;
        } else if (is_default) {
            msg = g_strdup("Make the panels of this workspace the default layout of new workspaces?");
        } else if (layout_store_find(d->layouts, name) != NULL) {
            msg = g_strdup_printf("A layout named “%s” already exists. Replace it?", name);
        }
        if (msg != NULL) {
            gboolean ok = confirm(app, is_default ? "Replace Default Layout" : "Replace Layout", msg,
                                  "_Replace");
            g_free(msg);
            if (!ok) {
                g_clear_pointer(&name, g_free);
                continue;
            }
        }
        break;
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    if (name == NULL) {
        return -1;
    }
    gint index = 0;
    if (g_ascii_strcasecmp(name, DEFAULT_NAME) == 0) {
        saved_layout_free(d->def);
        d->def = layouts_capture(app, DEFAULT_NAME);
    } else {
        layout_store_put(d->layouts, layouts_capture(app, name));
        for (guint i = 0; i < d->layouts->len; i++) {
            if (g_strcmp0(((SavedLayout*)g_ptr_array_index(d->layouts, i))->name, name) == 0) {
                index = (gint)i + 1;
            }
        }
    }
    g_free(name);
    save_store(d);
    return index;
}

GtkWidget* layouts_row_new(const gchar* title, const SavedLayout* l) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(box, "layout-row");
    GtkWidget* name = gtk_label_new(title);
    gtk_widget_add_css_class(name, "layout-name");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gchar* summary_text = saved_layout_summary(l);
    GtkWidget* summary = gtk_label_new(summary_text);
    g_free(summary_text);
    gtk_widget_add_css_class(summary, "dim-label");
    gtk_widget_add_css_class(summary, "layout-summary");
    gtk_label_set_xalign(GTK_LABEL(summary), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(summary), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), name);
    gtk_box_append(GTK_BOX(box), summary);
    return box;
}

gchar* layouts_default_title(const SavedLayout* def) {
    return g_strdup_printf("Default — new workspaces%s", layout_store_is_customized(def) ? "" : " (built-in)");
}

static void fill_list(LayoutsDialog* d, gint select_index) {
    /* Remove the rows only: gtk_list_box_remove_all() also drops the placeholder */
    for (GtkListBoxRow* row = gtk_list_box_get_row_at_index(d->list, 0); row != NULL;
         row = gtk_list_box_get_row_at_index(d->list, 0)) {
        gtk_list_box_remove(d->list, GTK_WIDGET(row));
    }
    gchar* title = layouts_default_title(d->def);
    gtk_list_box_append(d->list, layouts_row_new(title, d->def));
    g_free(title);
    for (guint i = 0; i < d->layouts->len; i++) {
        const SavedLayout* l = g_ptr_array_index(d->layouts, i);
        gtk_list_box_append(d->list, layouts_row_new(l->name, l));
    }
    gint n = (gint)d->layouts->len + 1;
    gtk_list_box_select_row(d->list, gtk_list_box_get_row_at_index(d->list, CLAMP(select_index, 0, n - 1)));
}

/* Index of the selected row (0 = default layout), -1 if none */
static gint selected_index(LayoutsDialog* d) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(d->list);
    return (row != NULL) ? gtk_list_box_row_get_index(row) : -1;
}

static const SavedLayout* layout_at(LayoutsDialog* d, gint index) {
    if (index == 0) {
        return d->def;
    }
    return (index > 0 && (guint)index <= d->layouts->len) ? g_ptr_array_index(d->layouts, (guint)index - 1)
                                                          : NULL;
}

/* Delete removes a named layout; on the default layout it becomes Reset */
static void update_buttons(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
    (void)list;
    LayoutsDialog* d = data;
    gint index = (row != NULL) ? gtk_list_box_row_get_index(row) : -1;
    GtkWidget* del = app_dialog_get_widget_for_response(d->dialog, RESPONSE_DELETE);
    gtk_button_set_label(GTK_BUTTON(del), index == 0 ? "_Reset Default" : "_Delete");
    gtk_widget_set_sensitive(del, index > 0 || (index == 0 && layout_store_is_customized(d->def)));
    gtk_widget_set_sensitive(app_dialog_get_widget_for_response(d->dialog, APP_RESPONSE_OK), index >= 0);
}

/* Double click or Enter on a row applies it; a single click only selects it */
static void on_row_activated(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
    (void)list;
    (void)row;
    app_dialog_response(APP_DIALOG(data), APP_RESPONSE_OK);
}

void layouts_show_dialog(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return;
    }
    gchar* path = layout_store_default_path();
    LayoutsDialog d = { app, layout_store_load_default(path), layout_store_load(path), NULL, NULL };
    g_free(path);

    GtkWidget* dlg = app_dialog_new("Workspace Layouts", app->window,
                                    "_Delete", RESPONSE_DELETE,
                                    "_Save Current…", RESPONSE_SAVE,
                                    "_Close", APP_RESPONSE_CLOSE,
                                    "_Apply", APP_RESPONSE_OK, NULL);
    d.dialog = APP_DIALOG(dlg);
    gtk_widget_add_css_class(app_dialog_get_widget_for_response(d.dialog, RESPONSE_DELETE),
                             "destructive-action");
    gtk_widget_add_css_class(app_dialog_get_widget_for_response(d.dialog, APP_RESPONSE_OK),
                             "suggested-action");
    app_dialog_set_default_response(d.dialog, APP_RESPONSE_OK);
    app_dialog_set_width(d.dialog, 480);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    GtkWidget* hint = gtk_label_new("Select a layout, then Apply it to this workspace (or "
                                    "double-click it). Save Current… replaces the selected "
                                    "layout or adds a new one; the default layout is the one "
                                    "new workspaces open with.");
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 58);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_append(GTK_BOX(box), hint);

    GtkWidget* list = gtk_list_box_new();
    d.list = GTK_LIST_BOX(list);
    gtk_widget_set_name(list, "layout-list");
    gtk_list_box_set_selection_mode(d.list, GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(d.list, FALSE);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 180);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 360);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(d.dialog)), box);

    g_signal_connect(list, "row-selected", G_CALLBACK(update_buttons), &d);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), dlg);
    fill_list(&d, 0);

    SavedLayout* to_apply = NULL;
    while (TRUE) {
        GtkListBoxRow* sel = gtk_list_box_get_selected_row(d.list);
        gint resp = app_dialog_run(d.dialog, sel != NULL ? GTK_WIDGET(sel) : list);
        gint index = selected_index(&d);
        const SavedLayout* l = layout_at(&d, index);
        if (resp == RESPONSE_SAVE) {
            gchar* suggestion = (index == 0) ? g_strdup(DEFAULT_NAME)
                              : (l != NULL)  ? g_strdup(l->name)
                                             : g_strdup_printf("Layout %u", d.layouts->len + 1);
            gint saved = save_current(&d, suggestion);
            g_free(suggestion);
            if (saved >= 0) {
                fill_list(&d, saved);
            }
        } else if (resp == RESPONSE_DELETE && index == 0) {
            if (confirm(app, "Reset Default Layout",
                        "Reset the default layout of new workspaces to the built-in one "
                        "(explorer and preview, no terminal)?", "_Reset")) {
                saved_layout_free(d.def);
                d.def = layout_store_builtin_default();
                save_store(&d);
                fill_list(&d, 0);
            }
        } else if (resp == RESPONSE_DELETE && l != NULL) {
            gchar* msg = g_strdup_printf("Delete the layout “%s”?", l->name);
            if (confirm(app, "Delete Layout", msg, "_Delete")) {
                layout_store_remove(d.layouts, l->name);
                save_store(&d);
                fill_list(&d, index - 1);
            }
            g_free(msg);
        } else if (resp == APP_RESPONSE_OK && l != NULL) {
            to_apply = saved_layout_copy(l);
            break;
        } else if (resp != APP_RESPONSE_OK) {
            break;
        }
    }
    g_signal_handlers_disconnect_by_func(list, update_buttons, &d);
    app_dialog_destroy(d.dialog);
    g_ptr_array_unref(d.layouts);
    saved_layout_free(d.def);
    if (to_apply != NULL) {
        layouts_apply(app, to_apply);
        saved_layout_free(to_apply);
    }
}
