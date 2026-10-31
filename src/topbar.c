/* Top bar: clickable path breadcrumbs and the layout/sort/hidden/help toggle pills. */
#include "topbar.h"
#include "icons.h"
#include "app_events.h"
#include "file_list.h"
#include "editor.h"
#include "git_tab.h"
#include "help.h"
#include "minimap.h"
#include "remote.h"
#include "terminal.h"

static void on_breadcrumb_clicked(GtkButton* btn, gpointer data) {
    AppData* app = (AppData*)data;
    const gchar* target = (const gchar*)g_object_get_data(G_OBJECT(btn), "target-path");
    if (target != NULL) {
        if (app->show_bookmarks || g_strcmp0(target, app->base_dir) != 0) {
            app->show_bookmarks = FALSE;
            if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS) {
                app->sidebar_view = SIDEBAR_VIEW_FILES;
            }
            reload_directory(app, target);
        }
    }
}

void on_files_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->left_vbox == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    if (cur && !preview_vis && !term_vis && !editor_vis) {
        return; /* keep at least one panel visible */
    }
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->left_vbox, !cur);
    update_path_label(app);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    if (!cur) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    } else if (preview_vis) {
        gtk_widget_grab_focus(app->text_view);
    } else if (editor_vis) {
        EditorTab* tab = editor_current_tab(app);
        if (tab != NULL && tab->view != NULL) {
            gtk_widget_grab_focus(tab->view);
        }
    }
}

void on_preview_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->viewer_box == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->viewer_box);
    gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);
    if (cur && !files_vis && !term_vis && !editor_vis) {
        return; /* keep at least one panel visible */
    }
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->viewer_box, !cur);
    update_path_label(app);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    if (!cur) {
        minimap_update_visibility(app);
        gtk_widget_grab_focus(app->text_view);
    } else if (files_vis) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

void on_split_btn_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    editor_toggle_dedicated((AppData*)data);
}

void on_term_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->term_scroll == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->term_scroll);
    gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    if (cur) {
        if (!files_vis && !preview_vis && !editor_vis) {
            return; /* keep at least one panel visible */
        }
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
        app->applying_layout = TRUE;
        gtk_widget_set_visible(app->term_scroll, FALSE);
        update_path_label(app);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        if (files_vis) {
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        } else if (preview_vis) {
            gtk_widget_grab_focus(app->text_view);
        }
    } else {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
        app->applying_layout = TRUE;
        if (app->right_paned != NULL) {
            gtk_widget_set_visible(app->right_paned, TRUE);
        }
        gtk_widget_set_visible(app->term_scroll, TRUE);
#ifdef HAVE_VTE
        GPtrArray* panes = term_visible_panes(app);
        if (panes->len == 0) {
            term_spawn_pane(app, FALSE);
        } else {
            GtkWidget* target = (app->last_focused_vte != NULL)
                                    ? app->last_focused_vte
                                    : GTK_WIDGET(g_ptr_array_index(panes, 0));
            gtk_widget_grab_focus(target);
        }
        g_ptr_array_unref(panes);
#endif
        update_path_label(app);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
}

void on_sort_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    cycle_sort_mode((AppData*)data);
}

void on_hidden_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_hidden_files((AppData*)data);
}

void on_help_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_help_panel((AppData*)data);
}

typedef struct {
    gchar* label;
    gchar* full_path;
} BreadcrumbSeg;

/* Top bar: clickable path breadcrumbs + right-aligned sort/hidden/help pills */
void update_path_label(AppData* app) {
    static const gchar* sort_names[SORT_COUNT] = {"name", "date", "size", "ext"};
    static const gchar* sort_pill_labels[SORT_COUNT] = {
        "⇅ Sort: Name", "⇅ Sort: Date", "⇅ Sort: Size", "⇅ Sort: Ext"
    };

    if (app->path_label != NULL) {
        gchar* text = g_strdup_printf("Directory: %s   [sort: %s] [hidden: %s] [view: %s]",
                                       app->base_dir, sort_names[app->sort_mode],
                                       app->show_hidden ? "on" : "off",
                                       app->show_bookmarks ? "bookmarks" : "files");
        gtk_label_set_text(GTK_LABEL(app->path_label), text);
        g_free(text);
    }

    if (app->breadcrumb_box != NULL && app->base_dir != NULL) {
        const gchar* built_dir = (const gchar*)g_object_get_data(G_OBJECT(app->breadcrumb_box), "built-dir");
        if (g_strcmp0(built_dir, app->base_dir) != 0) {
            GtkWidget* child;
            while ((child = gtk_widget_get_first_child(app->breadcrumb_box)) != NULL) {
                gtk_box_remove(GTK_BOX(app->breadcrumb_box), child);
            }

            GArray* segs = g_array_new(FALSE, FALSE, sizeof(BreadcrumbSeg));
            const gchar* home = g_get_home_dir();
            gsize home_len = (home != NULL) ? strlen(home) : 0;
            gboolean under_home = (home_len > 1 &&
                                   g_str_has_prefix(app->base_dir, home) &&
                                   (app->base_dir[home_len] == '\0' ||
                                    app->base_dir[home_len] == '/'));

            RemoteMachine* act_rm = remote_get_active_machine(app);
            gboolean is_remote_mount = (act_rm != NULL && act_rm->mount_point != NULL &&
                                        g_str_has_prefix(app->base_dir, act_rm->mount_point));

            gchar* accum = NULL;
            const gchar* rest = NULL;
            if (is_remote_mount) {
                gsize mount_len = strlen(act_rm->mount_point);
                gchar* root_title = g_strdup_printf("🌐 %s", act_rm->name ? act_rm->name : act_rm->host);
                BreadcrumbSeg root_seg = { root_title, g_strdup(act_rm->mount_point) };
                g_array_append_val(segs, root_seg);
                accum = g_strdup(act_rm->mount_point);
                rest = app->base_dir + mount_len;
            } else if (under_home) {
                BreadcrumbSeg root_seg = { g_strdup("🏠 ~"), g_strdup(home) };
                g_array_append_val(segs, root_seg);
                accum = g_strdup(home);
                rest = app->base_dir + home_len;
            } else {
                BreadcrumbSeg root_seg = { g_strdup("📁 /"), g_strdup("/") };
                g_array_append_val(segs, root_seg);
                accum = g_strdup("/");
                rest = app->base_dir;
            }

            gchar** parts = g_strsplit(rest, "/", -1);
            for (gint i = 0; parts != NULL && parts[i] != NULL; i++) {
                if (parts[i][0] == '\0') {
                    continue;
                }
                gchar* next_accum = g_build_filename(accum, parts[i], NULL);
                g_free(accum);
                accum = next_accum;

                BreadcrumbSeg seg = { g_strdup(parts[i]), g_strdup(accum) };
                g_array_append_val(segs, seg);
            }
            g_strfreev(parts);
            g_free(accum);

            guint total = segs->len;
            for (guint i = 0; i < total; i++) {
                /* Collapse middle segments if path is very deep (> 6 segments) */
                if (total > 6 && i == 1) {
                    GtkWidget* sep = gtk_label_new("›");
                    gtk_widget_add_css_class(sep, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(app->breadcrumb_box), sep);

                    GtkWidget* dots = gtk_label_new("…");
                    gtk_widget_add_css_class(dots, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(app->breadcrumb_box), dots);

                    i = total - 4;
                }

                if (i > 0) {
                    GtkWidget* sep = gtk_label_new("›");
                    gtk_widget_add_css_class(sep, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(app->breadcrumb_box), sep);
                }

                BreadcrumbSeg* s = &g_array_index(segs, BreadcrumbSeg, i);
                gboolean is_last = (i == total - 1);
                gchar* btn_text = (is_last && i > 0)
                                      ? g_strdup_printf("📂 %s", s->label)
                                      : g_strdup(s->label);
                GtkWidget* btn = gtk_button_new_with_label(btn_text);
                g_free(btn_text);
                gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
                gtk_widget_set_focusable(btn, FALSE);
                gtk_widget_add_css_class(btn, is_last ? "breadcrumb-current"
                                                      : "breadcrumb-item");
                gtk_widget_set_tooltip_text(btn, s->full_path);
                g_object_set_data_full(G_OBJECT(btn), "target-path",
                                       g_strdup(s->full_path), g_free);
                g_signal_connect(btn, "clicked",
                                 G_CALLBACK(on_breadcrumb_clicked), app);
                gtk_box_append(GTK_BOX(app->breadcrumb_box), btn);
            }

            for (guint i = 0; i < segs->len; i++) {
                BreadcrumbSeg* s = &g_array_index(segs, BreadcrumbSeg, i);
                g_free(s->label);
                g_free(s->full_path);
            }
            g_array_unref(segs);

            g_object_set_data_full(G_OBJECT(app->breadcrumb_box), "built-dir",
                                   g_strdup(app->base_dir), g_free);
        }
    }

    if (app->sort_btn != NULL) {
        gtk_button_set_label(GTK_BUTTON(app->sort_btn),
                             sort_pill_labels[app->sort_mode]);
    }

    if (app->hidden_btn != NULL) {
        gtk_button_set_label(GTK_BUTTON(app->hidden_btn),
                             app->show_hidden ? "👁 Hidden: On"
                                              : "👁 Hidden: Off");
        if (app->show_hidden) {
            gtk_widget_add_css_class(app->hidden_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->hidden_btn, "pill-active");
        }
    }

    gboolean files_vis = (app->left_vbox == NULL) || gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box == NULL) || gtk_widget_get_visible(app->viewer_box);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);

    if (app->editor_paned != NULL) {
        gtk_widget_set_visible(app->editor_paned, preview_vis || editor_vis);
    }
    if (app->right_paned != NULL) {
        gtk_widget_set_visible(app->right_paned, preview_vis || editor_vis || term_vis);
    }

    if (app->files_btn != NULL) {
        if (files_vis) {
            gtk_widget_add_css_class(app->files_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->files_btn, "pill-active");
        }
    }
    if (app->preview_btn != NULL) {
        if (preview_vis) {
            gtk_widget_add_css_class(app->preview_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->preview_btn, "pill-active");
        }
    }
    if (app->split_btn != NULL) {
        /* Nothing to move or dock without an editor tab */
        gtk_widget_set_visible(app->split_btn, app->editors != NULL);
        if (app->editor_dedicated) {
            gtk_widget_add_css_class(app->split_btn, "tab-action-active");
            icon_button_set_content(app->split_btn, "view-paged", NULL);
            gtk_widget_set_tooltip_text(app->split_btn, "Dock Editor Tabs into Preview Panel");
        } else {
            gtk_widget_remove_css_class(app->split_btn, "tab-action-active");
            icon_button_set_content(app->split_btn, "view-dual", NULL);
            gtk_widget_set_tooltip_text(app->split_btn, "Move Editor Tabs to a Dedicated Panel");
        }
    }
    if (app->term_btn != NULL) {
        RemoteMachine* act_rm = remote_get_active_machine(app);
        if (act_rm != NULL) {
            gchar* term_label = g_strdup_printf("SSH: %s", act_rm->name ? act_rm->name : act_rm->host);
            icon_button_set_content(app->term_btn, "network-server", term_label);
            g_free(term_label);
            gchar* tip = g_strdup_printf("Connected to remote machine: %s (%s). Click to toggle SSH terminal (Ctrl+T)",
                                         act_rm->name ? act_rm->name : act_rm->host, act_rm->host);
            gtk_widget_set_tooltip_text(app->term_btn, tip);
            g_free(tip);
        } else {
            icon_button_set_content(app->term_btn, "utilities-terminal", "Terminal");
            gtk_widget_set_tooltip_text(app->term_btn, "Show / Hide Terminal Panel (Ctrl+T)");
        }
        if (term_vis) {
            gtk_widget_add_css_class(app->term_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->term_btn, "pill-active");
        }
    }
    if (app->help_btn != NULL) {
        gboolean help_vis = (app->help_window != NULL) && gtk_widget_get_visible(app->help_window);
        if (help_vis) {
            gtk_widget_add_css_class(app->help_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->help_btn, "pill-active");
        }
    }

    /* Explorer panel header (VS Code style): view title + current folder section */
    if (app->sidebar_title != NULL) {
        const gchar* title = "EXPLORER";
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            title = "NOTES";
        } else if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks) {
            title = "BOOKMARKS";
        } else if (app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
            title = "REMOTE";
        }
        gtk_label_set_text(GTK_LABEL(app->sidebar_title), title);
        git_tab_update(app, app->base_dir);
    }
    if (app->sidebar_files_btn != NULL) {
        if (app->sidebar_view == SIDEBAR_VIEW_FILES && !app->show_bookmarks) {
            gtk_widget_add_css_class(app->sidebar_files_btn, "sidebar-icon-active");
        } else {
            gtk_widget_remove_css_class(app->sidebar_files_btn, "sidebar-icon-active");
        }
    }
    if (app->sidebar_bm_btn != NULL) {
        if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks) {
            gtk_widget_add_css_class(app->sidebar_bm_btn, "sidebar-icon-active");
        } else {
            gtk_widget_remove_css_class(app->sidebar_bm_btn, "sidebar-icon-active");
        }
    }
    if (app->sidebar_notes_btn != NULL) {
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            gtk_widget_add_css_class(app->sidebar_notes_btn, "sidebar-icon-active");
        } else {
            gtk_widget_remove_css_class(app->sidebar_notes_btn, "sidebar-icon-active");
        }
    }
    if (app->sidebar_remote_btn != NULL) {
        if (app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
            gtk_widget_add_css_class(app->sidebar_remote_btn, "sidebar-icon-active");
        } else {
            gtk_widget_remove_css_class(app->sidebar_remote_btn, "sidebar-icon-active");
        }
    }
    if (app->sidebar_files_toolbar != NULL) {
        gtk_widget_set_visible(app->sidebar_files_toolbar,
                               app->sidebar_view == SIDEBAR_VIEW_FILES && !app->show_bookmarks);
    }
    if (app->sidebar_bm_toolbar != NULL) {
        gtk_widget_set_visible(app->sidebar_bm_toolbar,
                               app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks);
    }
    if (app->sidebar_notes_actions != NULL) {
        gtk_widget_set_visible(app->sidebar_notes_actions,
                               app->sidebar_view == SIDEBAR_VIEW_NOTES);
    }
    if (app->sidebar_remote_toolbar != NULL) {
        gtk_widget_set_visible(app->sidebar_remote_toolbar,
                               app->sidebar_view == SIDEBAR_VIEW_REMOTE);
    }
    if (app->sidebar_files_hidden_btn != NULL) {
        if (app->show_hidden) {
            gtk_widget_add_css_class(app->sidebar_files_hidden_btn, "sidebar-icon-active");
        } else {
            gtk_widget_remove_css_class(app->sidebar_files_hidden_btn, "sidebar-icon-active");
        }
    }
    if (app->sidebar_section != NULL && app->base_dir != NULL) {
        gchar* base = g_path_get_basename(app->base_dir);
        gchar* upper = g_utf8_strup(base, -1);
        gchar* section = g_strdup_printf("▾ %s", upper);
        gtk_label_set_text(GTK_LABEL(app->sidebar_section), section);
        gtk_widget_set_tooltip_text(app->sidebar_section, app->base_dir);
        g_free(section);
        g_free(upper);
        g_free(base);
    }

    app_emit(app, APP_EVENT_STATUS_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
}
