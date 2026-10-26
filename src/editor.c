#include "editor.h"
#include "viewer.h"
#include "file_list.h"

EditorTab* editor_current_tab(AppData* app) {
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (page < 1) {
        return NULL; /* page 0 is read-only preview */
    }
    gint wanted = page - 1;
    gint i = 0;
    for (GList* l = app->editors; l != NULL; l = l->next, i++) {
        if (i == wanted) {
            return (EditorTab*)l->data;
        }
    }
    return NULL;
}

void editor_tab_label_update(EditorTab* tab) {
    gchar* title =
        gtk_text_buffer_get_modified(tab->buffer)
            ? g_strdup_printf("\xe2\x80\xa2 %s", tab->name)
            : g_strdup(tab->name);
    gtk_label_set_text(GTK_LABEL(tab->tab_label), title);
    g_free(title);
}

void on_editor_modified_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    editor_tab_label_update((EditorTab*)data);
}

void editor_apply_language(GtkTextBuffer* buffer, const gchar* name) {
#ifdef HAVE_GSV
    GtkSourceLanguageManager* mgr = gtk_source_language_manager_get_default();
    gchar* ctype = g_content_type_guess(name, NULL, 0, NULL);
    GtkSourceLanguage* lang =
        gtk_source_language_manager_guess_language(mgr, name, ctype);
    g_free(ctype);
    gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(buffer), lang);
    gtk_source_buffer_set_highlight_syntax(GTK_SOURCE_BUFFER(buffer),
                                           lang != NULL);
#else
    (void)buffer;
    (void)name;
#endif
}

void editor_open_file(AppData* app, const gchar* name, const gchar* full) {
    if (is_image_file(name) || is_pdf_file(name)) {
        /* Images and PDFs are previewed in the main viewer tab */
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        return;
    }

    /* Already open: switch to its existing tab */
    gint index = 0;
    for (GList* l = app->editors; l != NULL; l = l->next, index++) {
        EditorTab* t = (EditorTab*)l->data;
        if (g_strcmp0(t->path, full) == 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook),
                                          index + 1);
            gtk_widget_grab_focus(t->view);
            return;
        }
    }

    struct stat st;
    if (g_stat(full, &st) != 0) {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }
    if (st.st_size > EDIT_MAX_BYTES) {
        gchar* msg = g_strdup_printf(
            "[ %s is too large to edit (%" G_GINT64_FORMAT
            " bytes, limit %d) ]", name, (gint64)st.st_size, EDIT_MAX_BYTES);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }

    gchar* contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(full, &contents, &len, NULL)) {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }

    EditorTab* tab = g_new0(EditorTab, 1);
    tab->path = g_strdup(full);
    tab->name = g_strdup(name);
#ifdef HAVE_GSV
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
#else
    GtkTextBuffer* buffer = gtk_text_buffer_new(NULL);
#endif
    gtk_text_buffer_set_text(buffer, contents, -1);
#ifdef HAVE_GSV
    GtkWidget* view =
        gtk_source_view_new_with_buffer(GTK_SOURCE_BUFFER(buffer));
#else
    GtkWidget* view = gtk_text_view_new_with_buffer(buffer);
#endif
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X);
#if GTK_CHECK_VERSION(3, 18, 0)
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y);
#endif
    editor_apply_language(buffer, name);
    set_view_monospace(view, is_code_file(name));
#ifdef HAVE_GSV
    apply_vscode_scheme(buffer, view);
#endif

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);

    tab->buffer = buffer;
    tab->scroll = scroll;
    tab->view = view;
    tab->tab_label = gtk_label_new(tab->name);

    gint page = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook),
                                         scroll, tab->tab_label);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
    gtk_widget_grab_focus(view);

    gtk_text_buffer_set_modified(tab->buffer, FALSE);
    g_signal_connect(buffer, "modified-changed",
                     G_CALLBACK(on_editor_modified_changed), tab);

    app->editors = g_list_append(app->editors, tab);
    g_free(contents);
}

void on_editor_switch_page_changed(GtkNotebook* notebook,
                                   GtkWidget* page, guint page_num,
                                   gpointer data) {
    (void)notebook;
    (void)page;
    gtk_widget_set_visible(GTK_WIDGET(((AppData*)data)->minimap),
                           page_num == 0);
}

typedef struct {
    GMainLoop* loop;
    gint response;
} DiscardDialogData;

static void on_discard_dialog_response(GtkDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    DiscardDialogData* d = (DiscardDialogData*)user_data;
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

gboolean editor_confirm_discard(AppData* app, EditorTab* tab) {
    if (!gtk_text_buffer_get_modified(tab->buffer)) {
        return TRUE;
    }
    GtkWidget* dlg = gtk_message_dialog_new(app ? app->window : NULL,
                                            GTK_DIALOG_MODAL,
                                            GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_NONE,
                                            "%s has unsaved changes. "
                                            "Close anyway?", tab->name);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Close without saving",
                          GTK_RESPONSE_OK);

    DiscardDialogData d = { g_main_loop_new(NULL, FALSE), GTK_RESPONSE_CANCEL };
    g_signal_connect(dlg, "response", G_CALLBACK(on_discard_dialog_response), &d);
    gtk_window_present(GTK_WINDOW(dlg));
    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);
    gtk_window_destroy(GTK_WINDOW(dlg));
    return d.response == GTK_RESPONSE_OK;
}

void editor_close_page(AppData* app, gint page) {
    if (page < 1) {
        return;
    }
    gint wanted = page - 1;
    gint i = 0;
    EditorTab* tab = NULL;
    for (GList* l = app->editors; l != NULL; l = l->next, i++) {
        if (i == wanted) {
            tab = (EditorTab*)l->data;
            break;
        }
    }
    if (tab == NULL || !editor_confirm_discard(app, tab)) {
        return;
    }
    g_signal_handlers_disconnect_by_func(tab->buffer,
                                         (gpointer)on_editor_modified_changed,
                                         tab);
    gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
    app->editors = g_list_remove(app->editors, tab);
    g_free(tab->path);
    g_free(tab->name);
    g_free(tab);
}

void editor_close_current(AppData* app) {
    editor_close_page(app, gtk_notebook_get_current_page(
                          GTK_NOTEBOOK(app->notebook)));
}

gboolean editor_save_current(AppData* app) {
    EditorTab* tab = editor_current_tab(app);
    if (tab == NULL) {
        return FALSE;
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, FALSE);
    if (!g_file_set_contents(tab->path, text, -1, NULL)) {
        g_printerr("[ Cannot save: %s ]\n", tab->name);
    } else {
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
    }
    g_free(text);
    return TRUE;
}
