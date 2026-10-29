/* Saved workspace layouts: capture, apply and the list dialog (see layouts.h) */
#include "layouts.h"
#include "app_events.h"
#include "dialog.h"
#include "editor.h"
#include "layout_store.h"
#include "terminal.h"
#include "workspace.h"

enum { RESPONSE_SAVE = 1, RESPONSE_DELETE = 2 };

/* ------------------------------------------------------------------------- */
/* Capture and apply                                                         */
/* ------------------------------------------------------------------------- */

static SavedLayout* capture_layout(AppData* app, const gchar* name) {
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

/* Apply a layout to the current workspace. FALSE if the user cancelled. */
static gboolean apply_layout(AppData* app, const SavedLayout* l) {
#ifdef HAVE_VTE
    TermLayoutNode* tree = l->term_vis ? term_layout_parse(l->terminals) : NULL;
    guint wanted = term_layout_count(tree);
    term_layout_free(tree);
    guint running = term_pane_count(app);
    if (wanted > 0 && running > wanted) {
        gchar* msg = g_strdup_printf("The layout “%s” has %u terminal pane%s; %u running terminal%s "
                                     "will be closed.", l->name, wanted, wanted == 1 ? "" : "s",
                                     running - wanted, running - wanted == 1 ? "" : "s");
        gboolean ok = confirm(app, "Close Terminals", msg, "_Apply Layout");
        g_free(msg);
        if (!ok) {
            return FALSE;
        }
    }
#endif
    if (l->editor_dedicated != app->editor_dedicated) {
        editor_set_dedicated(app, l->editor_dedicated); /* moves the open tabs */
    }
#ifdef HAVE_VTE
    if (wanted > 0) {
        term_layout_apply(app, l->terminals);
    }
#endif
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    ws->files_vis = l->files_vis;
    ws->preview_vis = l->preview_vis;
    ws->term_vis = l->term_vis;
    ws->right_orient = l->stacked ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    ws->main_frac = l->main_frac;
    ws->right_frac = l->right_frac;
    ws->editor_frac = l->editor_frac;
    if (!ws->files_vis && !ws->preview_vis && !ws->term_vis && !ws->editor_dedicated) {
        ws->preview_vis = TRUE; /* never leave the window empty */
    }
    workspace_switch(app, app->current_ws); /* re-applies the current workspace */
    return TRUE;
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

static void save_store(AppData* app, GPtrArray* layouts) {
    gchar* path = layout_store_default_path();
    GError* err = NULL;
    if (!layout_store_save(path, layouts, &err)) {
        gchar* msg = g_strdup_printf("Could not save %s:\n%s", path, err ? err->message : "");
        notice(app, "Layouts Not Saved", msg);
        g_free(msg);
        g_clear_error(&err);
    }
    g_free(path);
}

/* Ask a name and save the current layout under it. Returns the name, or NULL. */
static gchar* save_current(AppData* app, GPtrArray* layouts, const gchar* suggestion) {
    GtkWidget* dlg = app_dialog_new("Save Layout", app->window, "_Cancel", APP_RESPONSE_CANCEL,
                                    "_Save", APP_RESPONSE_OK, NULL);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    GtkWidget* label = gtk_label_new("Save the panels of this workspace as:");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), suggestion);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 360);

    gchar* name = NULL;
    while (app_dialog_run(APP_DIALOG(dlg), entry) == APP_RESPONSE_OK) {
        name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));
        if (name[0] == '\0') {
            g_clear_pointer(&name, g_free);
            continue;
        }
        if (layout_store_find(layouts, name) != NULL) {
            gchar* msg = g_strdup_printf("A layout named “%s” already exists. Replace it?", name);
            gboolean ok = confirm(app, "Replace Layout", msg, "_Replace");
            g_free(msg);
            if (!ok) {
                g_clear_pointer(&name, g_free);
                continue;
            }
        }
        break;
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    if (name != NULL) {
        layout_store_put(layouts, capture_layout(app, name));
        save_store(app, layouts);
    }
    return name;
}

static GtkWidget* layout_row(const SavedLayout* l) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(box, "layout-row");
    GtkWidget* name = gtk_label_new(l->name);
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

static void fill_list(GtkListBox* list, GPtrArray* layouts, const gchar* select_name) {
    /* Remove the rows only: gtk_list_box_remove_all() also drops the placeholder */
    for (GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, 0); row != NULL;
         row = gtk_list_box_get_row_at_index(list, 0)) {
        gtk_list_box_remove(list, GTK_WIDGET(row));
    }
    for (guint i = 0; i < layouts->len; i++) {
        const SavedLayout* l = g_ptr_array_index(layouts, i);
        gtk_list_box_append(list, layout_row(l));
        if (g_strcmp0(l->name, select_name) == 0 || (select_name == NULL && i == 0)) {
            gtk_list_box_select_row(list, gtk_list_box_get_row_at_index(list, (gint)i));
        }
    }
}

static const SavedLayout* selected_layout(GtkListBox* list, GPtrArray* layouts) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(list);
    gint i = (row != NULL) ? gtk_list_box_row_get_index(row) : -1;
    return (i >= 0 && (guint)i < layouts->len) ? g_ptr_array_index(layouts, (guint)i) : NULL;
}

static void update_buttons(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
    (void)list;
    AppDialog* dlg = APP_DIALOG(data);
    gtk_widget_set_sensitive(app_dialog_get_widget_for_response(dlg, APP_RESPONSE_OK), row != NULL);
    gtk_widget_set_sensitive(app_dialog_get_widget_for_response(dlg, RESPONSE_DELETE), row != NULL);
}

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
    GPtrArray* layouts = layout_store_load(path);
    g_free(path);

    GtkWidget* dlg = app_dialog_new("Workspace Layouts", app->window,
                                    "_Delete", RESPONSE_DELETE,
                                    "_Save Current…", RESPONSE_SAVE,
                                    "_Close", APP_RESPONSE_CLOSE,
                                    "_Apply", APP_RESPONSE_OK, NULL);
    gtk_widget_add_css_class(app_dialog_get_widget_for_response(APP_DIALOG(dlg), RESPONSE_DELETE),
                             "destructive-action");
    gtk_widget_add_css_class(app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_OK),
                             "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 460);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    GtkWidget* hint = gtk_label_new("Apply a saved panel arrangement to this workspace. "
                                    "New workspaces still open with the default view.");
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 55);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_append(GTK_BOX(box), hint);

    GtkWidget* list = gtk_list_box_new();
    gtk_widget_set_name(list, "layout-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    GtkWidget* empty = gtk_label_new("No layouts yet — press Save Current… to add one");
    gtk_widget_add_css_class(empty, "dim-label");
    gtk_widget_set_margin_top(empty, 24);
    gtk_widget_set_margin_bottom(empty, 24);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list), empty);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 180);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 360);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);

    g_signal_connect(list, "row-selected", G_CALLBACK(update_buttons), dlg);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), dlg);
    fill_list(GTK_LIST_BOX(list), layouts, NULL);
    update_buttons(GTK_LIST_BOX(list), gtk_list_box_get_selected_row(GTK_LIST_BOX(list)), dlg);

    SavedLayout* to_apply = NULL;
    while (TRUE) {
        GtkListBoxRow* sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        gint resp = app_dialog_run(APP_DIALOG(dlg), sel != NULL ? GTK_WIDGET(sel) : list);
        const SavedLayout* l = selected_layout(GTK_LIST_BOX(list), layouts);
        if (resp == RESPONSE_SAVE) {
            gchar* suggestion = (l != NULL) ? g_strdup(l->name)
                                            : g_strdup_printf("Layout %u", layouts->len + 1);
            gchar* name = save_current(app, layouts, suggestion);
            g_free(suggestion);
            if (name != NULL) {
                fill_list(GTK_LIST_BOX(list), layouts, name);
                g_free(name);
            }
        } else if (resp == RESPONSE_DELETE && l != NULL) {
            gchar* msg = g_strdup_printf("Delete the layout “%s”?", l->name);
            if (confirm(app, "Delete Layout", msg, "_Delete")) {
                layout_store_remove(layouts, l->name);
                save_store(app, layouts);
                fill_list(GTK_LIST_BOX(list), layouts, NULL);
            }
            g_free(msg);
        } else if (resp == APP_RESPONSE_OK && l != NULL) {
            to_apply = saved_layout_copy(l);
            break;
        } else if (resp != APP_RESPONSE_OK) {
            break;
        }
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    g_ptr_array_unref(layouts);
    if (to_apply != NULL) {
        apply_layout(app, to_apply);
        saved_layout_free(to_apply);
    }
}
