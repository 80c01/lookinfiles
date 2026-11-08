#include "editor.h"
#include "editor_private.h"
#include "editor_modes.h"
#include "core/file_types.h"
#include "widgets/pane_geometry.h"
#include "preview_state.h"
#include "core/i18n.h"
#include "widgets/dialog.h"
#include "core/app_events.h"
#include "core/text_encoding.h"
#include "file_save.h"
#include "widgets/minimap.h"
#include "widgets/diff_gutter.h"
#include "focus_route.h"
#include "nav_history.h"
#include "editor_view.h"
#include "theme/icons.h"


/* Show or hide the line numbers of tab (NULL: the current editor tab) */
void editor_toggle_line_numbers(AppData* app, EditorTab* tab) {
    if (tab == NULL) {
        tab = editor_current_tab(app);
    }
    if (tab == NULL || tab->view == NULL) {
        return;
    }
    gboolean shown = diff_gutter_get_line_numbers(tab->view);
    diff_gutter_set_line_numbers(tab->view, shown ? -1 : 0);
}

EditorTab* editor_current_tab(AppData* app) {
    if (app == NULL || app->editors == NULL) {
        return NULL;
    }
    if (app->last_focused_editor != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            if (l->data == app->last_focused_editor) {
                return (EditorTab*)l->data;
            }
        }
        app->last_focused_editor = NULL;
    }

    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->view != NULL && gtk_widget_has_focus(t->view)) {
            app->last_focused_editor = t;
            return t;
        }
    }

    if (app->editor_box != NULL && gtk_widget_get_visible(app->editor_box)) {
        GPtrArray* nbs = editor_get_all_notebooks(app);
        for (guint i = 0; i < nbs->len; i++) {
            GtkNotebook* nb = GTK_NOTEBOOK(g_ptr_array_index(nbs, i));
            gint p = gtk_notebook_get_current_page(nb);
            if (p >= 0) {
                GtkWidget* child = gtk_notebook_get_nth_page(nb, p);
                for (GList* l = app->editors; l != NULL; l = l->next) {
                    EditorTab* t = (EditorTab*)l->data;
                    if (t != NULL && t->container == child) {
                        g_ptr_array_unref(nbs);
                        return t;
                    }
                }
            }
        }
        g_ptr_array_unref(nbs);
    }

    if (app->notebook != NULL) {
        /* Pages other than editor tabs (Preview, Git, References) match no tab */
        gint p = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
        if (p >= 0) {
            GtkWidget* child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), p);
            for (GList* l = app->editors; l != NULL; l = l->next) {
                EditorTab* t = (EditorTab*)l->data;
                if (t != NULL && t->container == child) {
                    return t;
                }
            }
        }
    }

    return (EditorTab*)app->editors->data;
}


void on_editor_modified_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->is_buffer && gtk_text_buffer_get_modified(tab->buffer)) {
        if (tab->path != NULL) {
            /* Automatically back buffer to ~/.config file so user doesn't lose changes */
            editor_tab_write(tab, tab->path, NULL);
        }
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
    }
    editor_tab_label_update(tab);
    /* Mode line flags (Emacs -U:**-, Vim [+]) follow saves right away */
    editor_modes_update_status(tab);
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


/* GTK selects the neighbour page when the current one is removed: when that
 * lands on the Git tab of the preview notebook (no editor tab left beside
 * it), show the preview instead */



EditorTab* editor_open_dedicated(AppData* app, const gchar* name, const gchar* full,
                                 GtkWidget* keep_focus) {
    if (app == NULL || full == NULL) return NULL;
    editor_open_file(app, name, full);
    EditorTab* tab = editor_find_tab(app, full);
    if (tab == NULL) return NULL; /* media, too large, unreadable: previewed only */
    if (editor_tab_notebook(app, tab) == GTK_NOTEBOOK(app->notebook)) {
        editor_move_tab_dedicated(app, tab);
    }
    if (keep_focus != NULL) {
        gtk_widget_grab_focus(keep_focus);
    } else if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
        app->last_focused_editor = tab;
    }
    return tab;
}

/* Give tab (its view built) a page with a title in target_nb, make it current
 * and focus it */
static void editor_tab_add_page(AppData* app, EditorTab* tab, GtkNotebook* target_nb) {
    tab->tab_label = file_type_label_new(tab->name, "");
    editor_tab_label_update(tab);

    editor_tab_label_connect(tab);

    gint page = gtk_notebook_append_page(target_nb, tab->container, tab->tab_label);
    editor_page_set_draggable(target_nb, tab->container);
    g_object_ref(tab->container);
    g_object_ref(tab->tab_label);
    gtk_notebook_set_current_page(target_nb, page);
    gtk_widget_grab_focus(tab->view);

    minimap_view_update_visibility(tab->minimap);

    g_signal_connect(tab->buffer, "modified-changed",
                     G_CALLBACK(on_editor_modified_changed), tab);

    app->editors = g_list_append(app->editors, tab);
    app->last_focused_editor = tab;
    if (app->editor_dedicated && app->editor_box != NULL) {
        gtk_widget_set_visible(app->editor_box, TRUE);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].editors = app->editors;
        app->workspaces[app->current_ws].notebook_page = page;
    }
    minimap_update_visibility(app);
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the split button shows with a tab */
}

void editor_open_file(AppData* app, const gchar* name, const gchar* full) {
    FileKind kind = (g_strcmp0(full, preview_state_path(app)) == 0) ? preview_state_kind(app)
                                                             : file_kind_of_path(full, name);
    if (file_kind_is_media(kind)) {
        /* Images, PDFs, Videos, and Audio are previewed in the main viewer tab */
        if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
            app_layout_begin(app);
            gtk_widget_set_visible(app->viewer_box, TRUE);
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
        if (app->notebook != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
        return;
    }

    GtkNotebook* target_nb = editor_target_notebook(app);
    if (target_nb == NULL) {
        return;
    }
    nav_history_file_opened(app, full);

    /* Already open: switch to its existing tab */
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (g_strcmp0(t->path, full) == 0) {
            t = editor_view_active(t); /* split: the view in use */
            GtkNotebook* t_nb = editor_tab_notebook(app, t);
            if (GTK_WIDGET(t_nb) != app->notebook) {
                if (app->editor_box != NULL && !gtk_widget_get_visible(app->editor_box)) {
                    app_layout_begin(app);
                    gtk_widget_set_visible(app->editor_box, TRUE);
                    app_emit(app, APP_EVENT_CHROME_CHANGED);
                    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
                }
            } else {
                if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
                    app_layout_begin(app);
                    gtk_widget_set_visible(app->viewer_box, TRUE);
                    app_emit(app, APP_EVENT_CHROME_CHANGED);
                    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
                }
            }
            gint target_page = gtk_notebook_page_num(t_nb, t->container);
            if (target_page >= 0) {
                gtk_notebook_set_current_page(t_nb, target_page);
            }
            if (t->view != NULL) {
                gtk_widget_grab_focus(t->view);
            }
            app->last_focused_editor = t;
            return;
        }
    }

    GStatBuf st;
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

    /* A text buffer takes valid UTF-8 only: binary files are not edited (an
     * empty buffer saved would erase them), other encodings are converted */
    if (!g_utf8_validate(contents, (gssize)len, NULL)) {
        if (!file_content_is_text((const guchar*)contents, MIN(len, FILE_SNIFF_BYTES))) {
            g_free(contents);
            gchar* msg = g_strdup_printf("%s is a binary file: previewed, not edited", name);
            app_notice(app, msg, FALSE);
            g_free(msg);
            return;
        }
        const gchar* encoding = NULL;
        gchar* utf8 = text_encoding_to_utf8(contents, len, &encoding);
        g_free(contents);
        contents = utf8;
        len = strlen(contents);
        gchar* msg = g_strdup_printf("%s read as %s: saving writes UTF-8", name,
                                     encoding != NULL ? encoding : "another encoding");
        app_notice(app, msg, FALSE);
        g_free(msg);
    }

    if (app->editor_dedicated) {
        if (app->editor_box != NULL && !gtk_widget_get_visible(app->editor_box)) {
            app_layout_begin(app);
            gtk_widget_set_visible(app->editor_box, TRUE);
            app->editor_dedicated = TRUE;
            if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
                app->workspaces[app->current_ws].editor_dedicated = TRUE;
            }
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
    } else {
        if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
            app_layout_begin(app);
            gtk_widget_set_visible(app->viewer_box, TRUE);
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
    }

    EditorTab* tab = g_new0(EditorTab, 1);
    tab->path = g_strdup(full);
    tab->name = g_strdup(name);
    tab->app = app;
    tab->nb = GTK_WIDGET(target_nb);
    tab->line_ending = text_line_ending_detect(contents, len);
    /* The buffer holds the lines without the final break: a text view would
     * show it as one more, empty, line */
    tab->final_break = text_final_break(contents, len);
    contents[len - strlen(tab->final_break)] = '\0';
    const gchar* cfg = g_get_user_config_dir();
    tab->is_buffer = (cfg != NULL && g_str_has_prefix(full, cfg));
    if (is_org_file(name)) {
        tab->mode = EDITOR_MODE_ORGMODE;
    } else {
        tab->mode = app->default_editor_mode;
    }
#ifdef HAVE_GSV
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
#else
    GtkTextBuffer* buffer = gtk_text_buffer_new(NULL);
#endif
    gtk_text_buffer_set_text(buffer, contents, -1);
    /* set_text leaves the cursor at the end: start editing at the top */
    GtkTextIter start_iter;
    gtk_text_buffer_get_start_iter(buffer, &start_iter);
    gtk_text_buffer_place_cursor(buffer, &start_iter);
    gchar* detect_name = g_path_get_basename(full);
    if (detect_name == NULL || detect_name[0] == '\0') {
        g_free(detect_name);
        detect_name = g_strdup(name);
    }
    editor_apply_language(buffer, detect_name);
    g_free(detect_name);
    editor_view_build(app, tab, buffer);
    gtk_text_buffer_set_modified(tab->buffer, FALSE);
    editor_tab_add_page(app, tab, target_nb);
    g_free(contents);
}

EditorTab* editor_open_buffer(AppData* app, const gchar* name, EditorMode mode) {
    if (app == NULL || name == NULL) return NULL;
    GtkNotebook* target_nb = editor_target_notebook(app);
    if (target_nb == NULL) return NULL;

    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->is_buffer && t->path == NULL && g_strcmp0(t->name, name) == 0) {
            t = editor_view_active(t);
            GtkNotebook* t_nb = editor_tab_notebook(app, t);
            if (GTK_WIDGET(t_nb) != app->notebook) {
                if (app->editor_box != NULL && !gtk_widget_get_visible(app->editor_box)) {
                    app_layout_begin(app);
                    gtk_widget_set_visible(app->editor_box, TRUE);
                    app_emit(app, APP_EVENT_CHROME_CHANGED);
                    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
                }
            } else {
                if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
                    app_layout_begin(app);
                    gtk_widget_set_visible(app->viewer_box, TRUE);
                    app_emit(app, APP_EVENT_CHROME_CHANGED);
                    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
                }
            }
            gint target_page = gtk_notebook_page_num(t_nb, t->container);
            if (target_page >= 0) {
                gtk_notebook_set_current_page(t_nb, target_page);
            }
            if (t->view != NULL) {
                gtk_widget_grab_focus(t->view);
            }
            app->last_focused_editor = t;
            return t;
        }
    }

    if (app->editor_dedicated) {
        if (app->editor_box != NULL && !gtk_widget_get_visible(app->editor_box)) {
            app_layout_begin(app);
            gtk_widget_set_visible(app->editor_box, TRUE);
            app->editor_dedicated = TRUE;
            if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
                app->workspaces[app->current_ws].editor_dedicated = TRUE;
            }
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
    } else {
        if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
            app_layout_begin(app);
            gtk_widget_set_visible(app->viewer_box, TRUE);
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
    }

    EditorTab* tab = g_new0(EditorTab, 1);
    tab->path = NULL;
    tab->name = g_strdup(name);
    tab->is_buffer = TRUE;
    tab->app = app;
    tab->nb = GTK_WIDGET(target_nb);
    tab->line_ending = "Unix (LF)";
    tab->final_break = "\n";
    tab->mode = mode;
#ifdef HAVE_GSV
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
#else
    GtkTextBuffer* buffer = gtk_text_buffer_new(NULL);
#endif
    editor_view_build(app, tab, buffer);
    gtk_text_buffer_set_modified(tab->buffer, FALSE);
    editor_tab_add_page(app, tab, target_nb);
    return tab;
}

void editor_split_tab(AppData* app, EditorTab* tab, EditorMoveDirection dir) {
    if (app == NULL) return;
    if (tab == NULL) {
        tab = editor_current_tab(app);
    }
    if (tab == NULL || tab->buffer == NULL || tab->view == NULL) return;
    /* The preview notebook does not split: the dedicated panel does */
    if (editor_tab_notebook(app, tab) == GTK_NOTEBOOK(app->notebook)) {
        editor_move_tab_dedicated(app, tab);
    }
    GtkNotebook* nb = editor_tab_notebook(app, tab);
    if (nb == NULL || GTK_WIDGET(nb) == app->notebook) return;
    PaneZone side = dir == EDITOR_MOVE_LEFT ? PANE_ZONE_LEFT
                  : dir == EDITOR_MOVE_UP   ? PANE_ZONE_TOP
                  : dir == EDITOR_MOVE_DOWN ? PANE_ZONE_BOTTOM
                                            : PANE_ZONE_RIGHT;
    GtkWidget* new_nb = editor_split_notebook(app, nb, side);
    if (new_nb != NULL) {
        editor_add_view(app, tab, GTK_NOTEBOOK(new_nb));
    }
}

EditorTab* editor_add_view(AppData* app, EditorTab* tab, GtkNotebook* nb) {
    EditorTab* twin = g_new0(EditorTab, 1);
    twin->path = g_strdup(tab->path);
    twin->name = g_strdup(tab->name);
    twin->app = app;
    twin->nb = GTK_WIDGET(nb);
    twin->is_buffer = tab->is_buffer;
    twin->line_ending = tab->line_ending;
    twin->final_break = tab->final_break;
    twin->mode = tab->mode;
    twin->text_mode = tab->text_mode;
    editor_view_build(app, twin, tab->buffer);
    /* The view looks like the one it was split from */
    diff_gutter_set_line_numbers(twin->view, diff_gutter_get_line_numbers(tab->view) ? 0 : -1);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(twin->view),
                                gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(tab->view)));
    editor_tab_add_page(app, twin, nb);
    editor_update_panel_visibilities(app);
    /* Both views share the cursor: show it */
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(twin->view), gtk_text_buffer_get_insert(twin->buffer),
                                 0.0, TRUE, 0.0, 0.3);
    editor_modes_update_status(twin);
    return twin;
}

gint editor_close_other_views(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL) return 0;
    gint closed = 0;
    for (EditorTab* t = editor_view_twin(app, tab); t != NULL; t = editor_view_twin(app, tab)) {
        editor_close_tab(app, t); /* no question: tab keeps the changes */
        closed++;
    }
    if (closed > 0 && tab->view != NULL) {
        app->last_focused_editor = tab;
        gtk_widget_grab_focus(tab->view);
    }
    return closed;
}

EditorTab* editor_find_tab(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL) return NULL;
    gchar* want = g_canonicalize_filename(path, NULL);
    EditorTab* found = NULL;
    for (GList* l = app->editors; l != NULL && found == NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t->path == NULL) continue;
        gchar* have = g_canonicalize_filename(t->path, NULL);
        if (g_strcmp0(have, want) == 0) found = editor_view_active(t); /* split: the view in use */
        g_free(have);
    }
    g_free(want);
    return found;
}

/* Centre again once the current event is handled: a later
 * scroll_mark_onscreen() (the Vim mode after a command) would otherwise
 * replace the pending scroll and leave the line at the bottom. */
static gboolean center_cursor_idle(gpointer data) {
    GtkTextView* view = GTK_TEXT_VIEW(data);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    if (buffer != NULL) gtk_text_view_scroll_to_mark(view, gtk_text_buffer_get_insert(buffer), 0.0, TRUE, 0.0, 0.5);
    return G_SOURCE_REMOVE;
}

void editor_center_cursor(EditorTab* tab) {
    if (tab == NULL || tab->view == NULL || tab->buffer == NULL) return;
    /* scroll_to_mark waits for line validation: works for a tab just opened */
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(tab->view), gtk_text_buffer_get_insert(tab->buffer), 0.0, TRUE,
                                 0.0, 0.5);
    /* Before the next layout (GDK_PRIORITY_REDRAW) */
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, center_cursor_idle, g_object_ref(tab->view), g_object_unref);
}

EditorTab* editor_open_at(AppData* app, const gchar* path, gint line, gint index) {
    if (app == NULL || path == NULL) return NULL;
    EditorTab* tab = editor_find_tab(app, path);
    if (tab != NULL) {
        editor_open_file(app, tab->name, tab->path);
    } else {
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) return NULL;
        gchar* name = g_path_get_basename(path);
        editor_open_file(app, name, path);
        g_free(name);
        tab = editor_find_tab(app, path);
    }
    if (tab == NULL || tab->buffer == NULL) return NULL;

    gint lines = gtk_text_buffer_get_line_count(tab->buffer);
    line = CLAMP(line, 0, lines - 1);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(tab->buffer, &it, line);
    /* Clamp to the line content, newline excluded */
    GtkTextIter end = it;
    if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
    gtk_text_iter_set_line_index(&it, CLAMP(index, 0, gtk_text_iter_get_line_index(&end)));
    gtk_text_buffer_place_cursor(tab->buffer, &it);
    if (tab->view != NULL) gtk_widget_grab_focus(tab->view);
    app->last_focused_editor = tab;
    editor_modes_update_status(tab);
    editor_center_cursor(tab);
    return tab;
}

static guint status_idle_id = 0; /* pending status bar refresh after a page switch */

static gboolean emit_status_changed_idle(gpointer data) {
    AppData* app = (AppData*)data;
    status_idle_id = 0;
    app_emit(app, APP_EVENT_STATUS_CHANGED);
    return G_SOURCE_REMOVE;
}

void on_editor_switch_page_changed(GtkNotebook* notebook,
                                   GtkWidget* page, guint page_num,
                                   gpointer data) {
    (void)page;
    AppData* app = (AppData*)data;
    if (app == NULL) return;

    if (!app->switching_ws &&
        app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].notebook_page = (gint)page_num;
    }

    if (GTK_WIDGET(notebook) == app->notebook && page_num == 0 && preview_state_path(app) != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && g_strcmp0(t->path, preview_state_path(app)) == 0) {
                if (t->buffer != NULL && (t->is_buffer || gtk_text_buffer_get_modified(t->buffer))) {
                    editor_tab_write(t, t->path, NULL);
                    gtk_text_buffer_set_modified(t->buffer, FALSE);
                    editor_tab_label_update(t);
                }
                app->file_dirty = FALSE;
                app_emit(app, APP_EVENT_PREVIEW_STALE);
                break;
            }
        }
    }

    GtkWidget* cur_child = gtk_notebook_get_nth_page(notebook, page_num);
    if (cur_child != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->container == cur_child) {
                app->last_focused_editor = t;
                minimap_view_update_visibility(t->minimap);
                editor_modes_update_status(t);
                break;
            }
        }
    }
    /* Also on the preview page: the bar then shows the default editor mode.
     * "switch-page" runs before the notebook changes its current page and
     * moves the focus, so refresh once the switch is done. */
    if (status_idle_id == 0) {
        status_idle_id = g_idle_add(emit_status_changed_idle, app);
    }
    minimap_update_visibility(app);
}

gboolean editor_confirm_discard(AppData* app, EditorTab* tab) {
    if (tab == NULL || tab->is_buffer || !gtk_text_buffer_get_modified(tab->buffer)) {
        return TRUE;
    }
    if (editor_view_twin(app, tab) != NULL) {
        return TRUE; /* another view of the file keeps the changes */
    }
    GtkWidget* dlg = app_dialog_new(_("Unsaved Changes"), app ? app->window : NULL,
                                    _("_Cancel"), APP_RESPONSE_CANCEL,
                                    _("Close _Without Saving"), APP_RESPONSE_OK,
                                    NULL);
    gchar* message = g_strdup_printf(_("%s has unsaved changes. Close anyway?"), tab->name);
    GtkWidget* label = gtk_label_new(message);
    g_free(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 16);
    gtk_widget_set_margin_end(label, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), label);
    /* Destructive choice: Enter keeps the changes */
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);

    gint response = app_dialog_run(APP_DIALOG(dlg),
                                   app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    return response == APP_RESPONSE_OK;
}

void editor_close_tab(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL) return;
    if (!editor_confirm_discard(app, tab)) return;

    if (tab->buffer != NULL && tab->is_buffer && tab->path != NULL) {
        editor_tab_write(tab, tab->path, NULL);
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
    }

    gchar* closed_path = g_strdup(tab->path);
    gchar* closed_name = g_strdup(tab->name);

    editor_view_release(tab);

    /* The focus goes back to the pane used before this tab */
    GtkWidget* focus_back = focus_route_return_target(app, tab->container);

    GtkWidget* nb_widget = tab->nb ? tab->nb : GTK_WIDGET(editor_target_notebook(app));
    GtkNotebook* nb = (nb_widget != NULL) ? GTK_NOTEBOOK(nb_widget) : NULL;
    if (nb != NULL) {
        gint page_idx = gtk_notebook_page_num(nb, tab->container);
        if (page_idx >= 0) {
            gboolean was_current = (gtk_notebook_get_current_page(nb) == page_idx);
            gtk_notebook_remove_page(nb, page_idx);
            editor_after_current_removed(app, nb, was_current);
        }
    }

    app->editors = g_list_remove(app->editors, tab);
    if (app->last_focused_editor == tab) {
        app->last_focused_editor = NULL;
    }

    if (nb_widget != NULL && nb_widget != app->notebook && nb != NULL && gtk_notebook_get_n_pages(nb) == 0) {
        editor_cleanup_notebook(app, nb_widget);
    }

    editor_update_panel_visibilities(app);
    focus_route_return(app, focus_back, tab->container);

    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].editors = app->editors;
        gint cur = (app->editor_notebook && gtk_widget_get_visible(app->editor_box))
            ? gtk_notebook_get_current_page(GTK_NOTEBOOK(app->editor_notebook))
            : (app->notebook ? gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook)) : 0);
        app->workspaces[app->current_ws].notebook_page = (cur >= 0 ? cur : 0);
    }
    g_object_unref(tab->container);
    g_object_unref(tab->tab_label);
    editor_modes_free_tab(tab);
    g_free(tab->path);
    g_free(tab->name);
    g_free(tab);
    minimap_update_visibility(app);

    if (closed_path != NULL) {
        gboolean is_viewed = (preview_state_path(app) != NULL && g_strcmp0(preview_state_path(app), closed_path) == 0);
        gboolean is_selected = FALSE;
        gchar* sel_path = NULL;
        gchar* sel_name = NULL;

        ExplorerRow* sel_row = (app->tree != NULL) ? explorer_tree_get_selected(app->tree) : NULL;
        if (sel_row != NULL) {
            sel_path = g_strdup(sel_row->path);
            sel_name = g_strdup(sel_row->name);
            is_selected = (g_strcmp0(sel_path, closed_path) == 0);
        }

        if (is_viewed || is_selected) {
            if (!is_viewed) {
                gchar* closed_name = g_path_get_basename(closed_path);
                preview_state_set_file(app, closed_path, closed_name);
                g_free(closed_name);
            }
            app->file_dirty = FALSE;
            app_emit(app, APP_EVENT_PREVIEW_STALE);
        }
        g_free(sel_path);
        g_free(sel_name);
        g_free(closed_path);
        g_free(closed_name);
    }
}

void editor_close_page(AppData* app, gint page) {
    if (app == NULL || page < 0) return;
    GtkNotebook* nb = editor_target_notebook(app);
    if (nb == NULL) return;
    GtkWidget* child = gtk_notebook_get_nth_page(nb, page);
    if (child == NULL) return;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->container == child) {
            editor_close_tab(app, t);
            return;
        }
    }
}

void editor_close_current(AppData* app) {
    if (app == NULL) return;
    EditorTab* cur = editor_current_tab(app);
    if (cur != NULL) {
        editor_close_tab(app, cur);
    }
}

GtkWidget* editor_detach_tabs(AppData* app) {
    if (app == NULL) return NULL;
    if (app->notebook != NULL) {
        /* Only the editor tabs leave: Preview, Git and References stay */
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        for (gint p = gtk_notebook_get_n_pages(nb) - 1; p >= 0; p--) {
            GtkWidget* child = gtk_notebook_get_nth_page(nb, p);
            for (GList* l = app->editors; l != NULL; l = l->next) {
                EditorTab* t = (EditorTab*)l->data;
                if (t != NULL && t->container == child) {
                    gtk_notebook_remove_page(nb, p);
                    break;
                }
            }
        }
    }
    /* The dedicated panel keeps its notebooks, splits and tabs: the whole
     * content leaves the panel and belongs to the workspace until it returns */
    GtkWidget* panel = NULL;
    if (app->editor_box != NULL) {
        GtkWidget* child = gtk_widget_get_first_child(app->editor_box);
        if (child != NULL) {
            GtkRoot* root = gtk_widget_get_root(child);
            GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
            if (focus != NULL && gtk_widget_is_ancestor(focus, child)) {
                gtk_root_set_focus(root, NULL);
            }
            panel = g_object_ref(child);
            gtk_box_remove(GTK_BOX(app->editor_box), child);
        }
    }
    app->editor_notebook = NULL;
    app->editors = NULL;
    return panel;
}

void editor_attach_tabs(AppData* app, GList* editors, GtkWidget* panel) {
    if (app == NULL) {
        if (panel != NULL) g_object_unref(panel);
        return;
    }
    if (panel != NULL && app->editor_box != NULL) {
        gtk_box_append(GTK_BOX(app->editor_box), panel);
    }
    if (panel != NULL) {
        g_object_unref(panel);
        /* A page closed while the workspace was hidden (a canvas) may have
         * left a notebook empty */
        GPtrArray* nbs = editor_get_all_notebooks(app);
        for (guint i = 0; i < nbs->len; i++) {
            GtkWidget* nb = g_ptr_array_index(nbs, i);
            if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb)) == 0) editor_cleanup_notebook(app, nb);
        }
        g_ptr_array_unref(nbs);
    }
    GPtrArray* arr = editor_get_all_notebooks(app);
    app->editor_notebook = (arr->len > 0) ? GTK_WIDGET(g_ptr_array_index(arr, 0)) : NULL;
    g_ptr_array_unref(arr);

    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab != NULL && tab->container != NULL && tab->tab_label != NULL &&
            tab->nb == app->notebook && app->notebook != NULL) {
            GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
            gtk_notebook_append_page(nb, tab->container, tab->tab_label);
            editor_page_set_draggable(nb, tab->container);
        }
        if (tab != NULL) {
            minimap_view_update_visibility(tab->minimap);
        }
    }
    app->editors = editors;
    editor_update_panel_visibilities(app);
}

gboolean editor_confirm_discard_workspace(AppData* app, GList* editors) {
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        gboolean seen = FALSE; /* the file of a split view: asked once */
        for (GList* p = editors; p != l && !seen; p = p->next) {
            seen = tab != NULL && ((EditorTab*)p->data)->buffer == tab->buffer;
        }
        if (tab != NULL && !seen && tab->buffer != NULL &&
            gtk_text_buffer_get_modified(tab->buffer)) {
            if (!editor_confirm_discard(app, tab)) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

void editor_free_tabs(AppData* app, GList* editors, gboolean detach_first) {
    if (detach_first && app != NULL) {
        GtkWidget* panel = editor_detach_tabs(app);
        if (panel != NULL) g_object_unref(panel);
    }
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab == NULL) {
            continue;
        }
        /* No pointer to a freed tab may stay behind */
        if (app != NULL && app->last_focused_editor == tab) {
            app->last_focused_editor = NULL;
        }
        editor_view_release(tab);
        if (tab->container != NULL) {
            g_object_unref(tab->container);
            tab->container = NULL;
        }
        if (tab->tab_label != NULL) {
            g_object_unref(tab->tab_label);
            tab->tab_label = NULL;
        }
        editor_modes_free_tab(tab);
        g_free(tab->path);
        g_free(tab->name);
        g_free(tab);
    }
    g_list_free(editors);
}

void editor_set_tab_mode(AppData* app, EditorTab* tab, EditorMode mode) {
    if (tab == NULL) return;
    editor_modes_apply_mode(app, tab, mode);
}

void editor_set_dedicated(AppData* app, gboolean dedicated) {
    if (app == NULL) return;
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    /* Each move makes the moved tab current: keep the current one instead */
    EditorTab* keep = editor_current_tab(app);

    if (dedicated) {
        GList* to_move = NULL;
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->nb == app->notebook) {
                to_move = g_list_append(to_move, t);
            }
        }
        for (GList* l = to_move; l != NULL; l = l->next) {
            editor_move_tab_dedicated(app, (EditorTab*)l->data);
        }
        g_list_free(to_move);
    } else {
        GList* to_move = NULL;
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->nb != app->notebook) {
                to_move = g_list_append(to_move, t);
            }
        }
        for (GList* l = to_move; l != NULL; l = l->next) {
            editor_move_tab_dedicated(app, (EditorTab*)l->data);
        }
        g_list_free(to_move);
    }

    if (keep != NULL && keep->nb != NULL) {
        GtkNotebook* nb = GTK_NOTEBOOK(keep->nb);
        gint page = gtk_notebook_page_num(nb, keep->container);
        if (page >= 0) gtk_notebook_set_current_page(nb, page);
        app->last_focused_editor = keep;
    }

    editor_update_panel_visibilities(app);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    minimap_update_visibility(app);

    EditorTab* cur = editor_current_tab(app);
    if (cur != NULL && cur->view != NULL) {
        gtk_widget_grab_focus(cur->view);
        minimap_view_update_visibility(cur->minimap);
    }
}

void editor_toggle_dedicated(AppData* app) {
    if (app == NULL) return;
    editor_set_dedicated(app, !app->editor_dedicated);
}

void editor_save_all_buffers(AppData* app) {
    if (app == NULL) {
        return;
    }
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab != NULL && tab->is_buffer && tab->path != NULL && tab->buffer != NULL) {
            editor_tab_write(tab, tab->path, NULL);
            gtk_text_buffer_set_modified(tab->buffer, FALSE);
        }
    }
}

gboolean editor_save_current(AppData* app) {
    EditorTab* tab = editor_current_tab(app);
    if (tab == NULL || tab->path == NULL) {
        return FALSE;
    }
    GError* error = NULL;
    if (!editor_tab_write(tab, tab->path, &error)) {
        gchar* msg = g_strdup_printf(_("Cannot save %s: %s"), tab->name, error->message);
        app_notice(app, msg, FALSE);
        g_free(msg);
        g_error_free(error);
    } else {
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
        editor_tab_label_update(tab);
        app_emit(app, APP_EVENT_FILES_CHANGED);
        if (preview_state_path(app) != NULL && g_strcmp0(preview_state_path(app), tab->path) == 0) {
            app->file_dirty = FALSE;
            app_emit(app, APP_EVENT_PREVIEW_STALE);
        }
        if (tab->view != NULL) {
            gtk_widget_grab_focus(tab->view);
        }
    }
    return TRUE;
}

gboolean editor_tab_write(EditorTab* tab, const gchar* path, GError** error) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
    gchar* lines = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
    gchar* text = g_strconcat(lines, tab->final_break, NULL);
    g_free(lines);
    gboolean ok = file_save_text(path, text, -1, error);
    g_free(text);
    return ok;
}
