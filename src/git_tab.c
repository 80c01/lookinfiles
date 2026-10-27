#include "git_tab.h"
#include "file_list.h"
#include "viewer.h"
#include <string.h>

enum {
    GIT_COL_HASH = 0,
    GIT_COL_SHORT_HASH,
    GIT_COL_SUBJECT,
    GIT_COL_AUTHOR,
    GIT_COL_DATE,
    GIT_COL_COUNT
};

typedef struct {
    gchar* hash;
    gchar* short_hash;
    gchar* subject;
    gchar* author;
    gchar* date;
} GitCommitInfo;

static void git_commit_info_free(gpointer data) {
    GitCommitInfo* c = (GitCommitInfo*)data;
    if (c == NULL) return;
    g_free(c->hash);
    g_free(c->short_hash);
    g_free(c->subject);
    g_free(c->author);
    g_free(c->date);
    g_free(c);
}

typedef struct {
    AppData* app;
    gchar* git_root;
    GPtrArray* commits; /* array of GitCommitInfo* */
} GitLogTaskData;

static void git_log_task_data_free(gpointer data) {
    GitLogTaskData* td = (GitLogTaskData*)data;
    if (td == NULL) return;
    g_free(td->git_root);
    if (td->commits != NULL) {
        g_ptr_array_unref(td->commits);
    }
    g_free(td);
}

static void git_log_thread_func(GTask* task, gpointer source_object,
                                gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    GitLogTaskData* td = (GitLogTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) return;

    gchar* argv[] = {
        "git",
        "-C",
        td->git_root,
        "log",
        "-n",
        "100",
        "--pretty=format:%H%x1f%h%x1f%s%x1f%an%x1f%cr",
        NULL
    };

    gchar* stdout_buf = NULL;
    gint exit_status = 0;
    GError* err = NULL;

    gboolean ok = g_spawn_sync(NULL, argv, NULL,
                               G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, &stdout_buf, NULL, &exit_status, &err);

    if (g_cancellable_is_cancelled(cancellable)) {
        g_free(stdout_buf);
        if (err != NULL) g_error_free(err);
        g_task_return_error_if_cancelled(task);
        return;
    }

    td->commits = g_ptr_array_new_with_free_func(git_commit_info_free);

    if (ok && exit_status == 0 && stdout_buf != NULL) {
        gchar** lines = g_strsplit(stdout_buf, "\n", -1);
        for (gint i = 0; lines[i] != NULL; i++) {
            if (lines[i][0] == '\0') continue;
            gchar** parts = g_strsplit(lines[i], "\x1f", 5);
            if (parts[0] != NULL && parts[1] != NULL && parts[2] != NULL) {
                GitCommitInfo* ci = g_new0(GitCommitInfo, 1);
                ci->hash = g_strdup(parts[0]);
                ci->short_hash = g_strdup(parts[1]);
                ci->subject = g_strdup(parts[2]);
                ci->author = (parts[3] != NULL) ? g_strdup(parts[3]) : g_strdup("");
                ci->date = (parts[4] != NULL) ? g_strdup(parts[4]) : g_strdup("");
                g_ptr_array_add(td->commits, ci);
            }
            g_strfreev(parts);
        }
        g_strfreev(lines);
    }

    g_free(stdout_buf);
    if (err != NULL) g_error_free(err);

    g_task_return_boolean(task, TRUE);
}

typedef struct {
    AppData* app;
    gchar* git_root;
    gchar* commit_hash;
    gchar* diff_output;
} GitShowTaskData;

static void git_show_task_data_free(gpointer data) {
    GitShowTaskData* td = (GitShowTaskData*)data;
    if (td == NULL) return;
    g_free(td->git_root);
    g_free(td->commit_hash);
    g_free(td->diff_output);
    g_free(td);
}

static void git_show_thread_func(GTask* task, gpointer source_object,
                                 gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    GitShowTaskData* td = (GitShowTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) return;

    gchar* argv[] = {
        "git",
        "-C",
        td->git_root,
        "show",
        "--stat",
        "--patch",
        td->commit_hash,
        NULL
    };

    gchar* stdout_buf = NULL;
    gint exit_status = 0;
    GError* err = NULL;

    gboolean ok = g_spawn_sync(NULL, argv, NULL,
                               G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, &stdout_buf, NULL, &exit_status, &err);

    if (g_cancellable_is_cancelled(cancellable)) {
        g_free(stdout_buf);
        if (err != NULL) g_error_free(err);
        g_task_return_error_if_cancelled(task);
        return;
    }

    if (ok && exit_status == 0 && stdout_buf != NULL) {
        td->diff_output = stdout_buf;
    } else {
        g_free(stdout_buf);
        td->diff_output = g_strdup("(No diff available or git error)");
    }

    if (err != NULL) g_error_free(err);
    g_task_return_boolean(task, TRUE);
}

static void format_git_diff_markup(GtkTextBuffer* buffer, const gchar* raw_diff) {
    gtk_text_buffer_set_text(buffer, "", -1);
    if (raw_diff == NULL || raw_diff[0] == '\0') {
        return;
    }

    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(tags, "diff_add")) {
        gtk_text_buffer_create_tag(buffer, "diff_add", "foreground", "#89d185", NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_del")) {
        gtk_text_buffer_create_tag(buffer, "diff_del", "foreground", "#f44747", NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_hunk")) {
        gtk_text_buffer_create_tag(buffer, "diff_hunk", "foreground", "#4fc1ff", NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_header")) {
        gtk_text_buffer_create_tag(buffer, "diff_header", "foreground", "#dcdcaa", "weight", PANGO_WEIGHT_BOLD, NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_dim")) {
        gtk_text_buffer_create_tag(buffer, "diff_dim", "foreground", "#808080", NULL);
    }

    gchar** lines = g_strsplit(raw_diff, "\n", -1);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);

    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* line = lines[i];
        const gchar* tag_name = NULL;

        if (g_str_has_prefix(line, "commit ") ||
            g_str_has_prefix(line, "Author: ") ||
            g_str_has_prefix(line, "Date: ")) {
            tag_name = "diff_header";
        } else if (g_str_has_prefix(line, "diff --git") ||
                   g_str_has_prefix(line, "index ") ||
                   g_str_has_prefix(line, "--- ") ||
                   g_str_has_prefix(line, "+++ ")) {
            tag_name = "diff_dim";
        } else if (g_str_has_prefix(line, "@@")) {
            tag_name = "diff_hunk";
        } else if (line[0] == '+') {
            tag_name = "diff_add";
        } else if (line[0] == '-') {
            tag_name = "diff_del";
        }

        gchar* with_nl = g_strconcat(line, "\n", NULL);
        if (tag_name != NULL) {
            gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, with_nl, -1, tag_name, NULL);
        } else {
            gtk_text_buffer_insert(buffer, &iter, with_nl, -1);
        }
        g_free(with_nl);
    }
    g_strfreev(lines);
}

static void on_git_show_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GitShowTaskData* td = (GitShowTaskData*)g_task_get_task_data(task);
    if (td == NULL || td->app == NULL) return;

    AppData* app = td->app;
    if (td->commit_hash != NULL && g_strcmp0(td->commit_hash, app->git_selected_hash) == 0) {
        format_git_diff_markup(app->git_detail_buf, td->diff_output);
    }
}

static void on_git_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    AppData* app = (AppData*)user_data;
    if (app == NULL || app->git_store == NULL || app->git_current_root == NULL) return;

    GtkTreeModel* model = NULL;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar* hash = NULL;
    gchar* short_hash = NULL;
    gchar* subject = NULL;
    gchar* author = NULL;
    gchar* date = NULL;

    gtk_tree_model_get(model, &iter,
                       GIT_COL_HASH, &hash,
                       GIT_COL_SHORT_HASH, &short_hash,
                       GIT_COL_SUBJECT, &subject,
                       GIT_COL_AUTHOR, &author,
                       GIT_COL_DATE, &date, -1);

    if (hash == NULL) {
        g_free(short_hash);
        g_free(subject);
        g_free(author);
        g_free(date);
        return;
    }

    g_free(app->git_selected_hash);
    app->git_selected_hash = g_strdup(hash);

    gchar* header_text = g_strdup_printf("[%s] %s  •  %s  (%s)",
                                        short_hash ? short_hash : "",
                                        subject ? subject : "",
                                        author ? author : "",
                                        date ? date : "");
    if (app->git_header_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app->git_header_label), header_text);
    }
    g_free(header_text);

    GitShowTaskData* td = g_new0(GitShowTaskData, 1);
    td->app = app;
    td->git_root = g_strdup(app->git_current_root);
    td->commit_hash = g_strdup(hash);

    GTask* task = g_task_new(NULL, app->git_cancellable, on_git_show_ready, NULL);
    g_task_set_task_data(task, td, git_show_task_data_free);
    g_task_run_in_thread(task, git_show_thread_func);
    g_object_unref(task);

    g_free(hash);
    g_free(short_hash);
    g_free(subject);
    g_free(author);
    g_free(date);
}

static void on_git_log_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GitLogTaskData* td = (GitLogTaskData*)g_task_get_task_data(task);
    if (td == NULL || td->app == NULL || td->app->git_store == NULL) return;

    AppData* app = td->app;

    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->git_tree_view));
    if (sel != NULL) {
        g_signal_handlers_block_by_func(sel, (gpointer)on_git_selection_changed, app);
    }

    gtk_list_store_clear(app->git_store);

    if (td->commits != NULL) {
        for (guint i = 0; i < td->commits->len; i++) {
            GitCommitInfo* ci = (GitCommitInfo*)g_ptr_array_index(td->commits, i);
            GtkTreeIter iter;
            gtk_list_store_append(app->git_store, &iter);
            gtk_list_store_set(app->git_store, &iter,
                               GIT_COL_HASH, ci->hash,
                               GIT_COL_SHORT_HASH, ci->short_hash,
                               GIT_COL_SUBJECT, ci->subject,
                               GIT_COL_AUTHOR, ci->author,
                               GIT_COL_DATE, ci->date, -1);
        }
    }

    if (sel != NULL) {
        g_signal_handlers_unblock_by_func(sel, (gpointer)on_git_selection_changed, app);
    }

    /* Select first commit if available */
    GtkTreeIter first_iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app->git_store), &first_iter)) {
        if (sel != NULL) {
            gtk_tree_selection_select_iter(sel, &first_iter);
            on_git_selection_changed(sel, app);
        }
    } else {
        if (app->git_header_label != NULL) {
            gtk_label_set_text(GTK_LABEL(app->git_header_label), "No commits in this repository.");
        }
        if (app->git_detail_buf != NULL) {
            gtk_text_buffer_set_text(app->git_detail_buf, "", -1);
        }
    }
}

void git_tab_update(AppData* app, const gchar* dir) {
    if (app == NULL || app->git_tab_box == NULL) return;

    gchar* root = find_git_root(dir);
    if (root == NULL) {
        gtk_widget_set_visible(app->git_tab_box, FALSE);
        g_free(app->git_current_root);
        app->git_current_root = NULL;
        return;
    }

    gtk_widget_set_visible(app->git_tab_box, TRUE);

    if (g_strcmp0(app->git_current_root, root) == 0) {
        g_free(root);
        return;
    }

    g_free(app->git_current_root);
    app->git_current_root = root;

    if (app->git_cancellable != NULL) {
        g_cancellable_cancel(app->git_cancellable);
        g_clear_object(&app->git_cancellable);
    }
    app->git_cancellable = g_cancellable_new();

    GitLogTaskData* td = g_new0(GitLogTaskData, 1);
    td->app = app;
    td->git_root = g_strdup(app->git_current_root);

    GTask* task = g_task_new(NULL, app->git_cancellable, on_git_log_ready, NULL);
    g_task_set_task_data(task, td, git_log_task_data_free);
    g_task_run_in_thread(task, git_log_thread_func);
    g_object_unref(task);
}

void git_tab_init(AppData* app) {
    if (app == NULL || app->notebook == NULL) return;

    /* Create commit list model */
    app->git_store = gtk_list_store_new(GIT_COL_COUNT,
                                        G_TYPE_STRING, /* HASH */
                                        G_TYPE_STRING, /* SHORT_HASH */
                                        G_TYPE_STRING, /* SUBJECT */
                                        G_TYPE_STRING, /* AUTHOR */
                                        G_TYPE_STRING  /* DATE */
    );

    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->git_store));
    app->git_tree_view = tree_view;
    gtk_widget_set_name(tree_view, "git-tree");
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree_view), FALSE);

    /* Column 1: Commit hash (short) */
    GtkCellRenderer* r_hash = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_padding(r_hash, 6, 2);
    g_object_set(r_hash, "foreground", "#4fc1ff", "family", "monospace", NULL);
    GtkTreeViewColumn* col_hash = gtk_tree_view_column_new_with_attributes(
        "Commit", r_hash, "text", GIT_COL_SHORT_HASH, NULL);
    gtk_tree_view_column_set_resizable(col_hash, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_hash);

    /* Column 2: Subject / message */
    GtkCellRenderer* r_msg = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_padding(r_msg, 6, 2);
    g_object_set(r_msg, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    GtkTreeViewColumn* col_msg = gtk_tree_view_column_new_with_attributes(
        "Message", r_msg, "text", GIT_COL_SUBJECT, NULL);
    gtk_tree_view_column_set_expand(col_msg, TRUE);
    gtk_tree_view_column_set_resizable(col_msg, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_msg);

    /* Column 3: Author */
    GtkCellRenderer* r_author = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_padding(r_author, 6, 2);
    g_object_set(r_author, "foreground", "#cccccc", NULL);
    GtkTreeViewColumn* col_author = gtk_tree_view_column_new_with_attributes(
        "Author", r_author, "text", GIT_COL_AUTHOR, NULL);
    gtk_tree_view_column_set_resizable(col_author, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_author);

    /* Column 4: Date */
    GtkCellRenderer* r_date = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_padding(r_date, 6, 2);
    g_object_set(r_date, "foreground", "#9d9d9d", NULL);
    GtkTreeViewColumn* col_date = gtk_tree_view_column_new_with_attributes(
        "Date", r_date, "text", GIT_COL_DATE, NULL);
    gtk_tree_view_column_set_resizable(col_date, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_date);

    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    g_signal_connect(sel, "changed", G_CALLBACK(on_git_selection_changed), app);

    GtkWidget* tree_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(tree_scroll, TRUE);
    gtk_widget_set_vexpand(tree_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tree_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tree_scroll), tree_view);
    gtk_widget_set_size_request(tree_scroll, -1, 120);

    /* Bottom: Commit header & diff viewer */
    GtkWidget* detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(detail_box, TRUE);
    gtk_widget_set_vexpand(detail_box, TRUE);

    GtkWidget* header_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(header_bar, "git-header-bar");
    GtkWidget* header_lbl = gtk_label_new("Select a commit to view diff");
    gtk_label_set_xalign(GTK_LABEL(header_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(header_lbl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(header_lbl, TRUE);
    gtk_box_append(GTK_BOX(header_bar), header_lbl);
    app->git_header_label = header_lbl;

    app->git_detail_buf = gtk_text_buffer_new(NULL);
    app->git_detail_view = gtk_text_view_new_with_buffer(app->git_detail_buf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->git_detail_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->git_detail_view), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->git_detail_view), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->git_detail_view), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->git_detail_view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->git_detail_view), 8);
    set_view_monospace(app->git_detail_view, TRUE);
    apply_viewer_background(app->git_detail_view);

    GtkWidget* detail_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(detail_scroll, TRUE);
    gtk_widget_set_vexpand(detail_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(detail_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(detail_scroll), app->git_detail_view);

    gtk_box_append(GTK_BOX(detail_box), header_bar);
    gtk_box_append(GTK_BOX(detail_box), detail_scroll);

    /* Paned dividing commits table and commit diff */
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), tree_scroll);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(paned), detail_box);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 220);
    app->git_paned = paned;

    /* Container box for the whole Git tab page */
    GtkWidget* tab_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(tab_box, TRUE);
    gtk_widget_set_vexpand(tab_box, TRUE);
    gtk_box_append(GTK_BOX(tab_box), paned);
    app->git_tab_box = tab_box;

    app->git_tab_label = gtk_label_new("📦 Git Commits");
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab_box, app->git_tab_label);

    git_tab_update(app, app->base_dir);
}

void git_tab_cleanup(AppData* app) {
    if (app == NULL) return;
    if (app->git_cancellable != NULL) {
        g_cancellable_cancel(app->git_cancellable);
        g_clear_object(&app->git_cancellable);
    }
    g_free(app->git_current_root);
    app->git_current_root = NULL;
    g_free(app->git_selected_hash);
    app->git_selected_hash = NULL;
}
