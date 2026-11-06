#include "git_tab.h"
#include "text_tags.h"
#include "icons.h"
#include "panels.h"
#include "text_style.h"
#include "git_utils.h"
#include "focus_route.h"
#include <string.h>

/* One row of the commit list (item of app->git_store) */
#define GIT_TYPE_COMMIT (git_commit_get_type())
G_DECLARE_FINAL_TYPE(GitCommit, git_commit, GIT, COMMIT, GObject)

struct _GitCommit {
    GObject parent_instance;
    GitCommitInfo* info; /* owned */
};

G_DEFINE_FINAL_TYPE(GitCommit, git_commit, G_TYPE_OBJECT)

static void git_commit_finalize(GObject* object) {
    git_commit_info_free(GIT_COMMIT(object)->info);
    G_OBJECT_CLASS(git_commit_parent_class)->finalize(object);
}

static void git_commit_class_init(GitCommitClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = git_commit_finalize;
}

static void git_commit_init(GitCommit* self) {
    (void)self;
}

/* Takes ownership of info */
static GitCommit* git_commit_new(GitCommitInfo* info) {
    GitCommit* self = g_object_new(GIT_TYPE_COMMIT, NULL);
    self->info = info;
    return self;
}

/* Commit at position i of the list, NULL if out of range (borrowed) */
static GitCommitInfo* git_commit_at(AppData* app, guint i) {
    GitCommit* c = g_list_model_get_item(G_LIST_MODEL(app->git_store), i);
    if (c == NULL) return NULL;
    GitCommitInfo* info = c->info;
    g_object_unref(c); /* the store keeps it alive */
    return info;
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

    GPtrArray* commits = git_read_log(td->git_root, 100);

    if (g_cancellable_is_cancelled(cancellable)) {
        if (commits != NULL) g_ptr_array_unref(commits);
        g_task_return_error_if_cancelled(task);
        return;
    }

    td->commits = commits != NULL ? commits : g_ptr_array_new_with_free_func(git_commit_info_free);
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

/* The diff is laid out line by line on the UI thread: a huge commit (a large
 * file added) shows its first lines only */
#define GIT_SHOW_MAX_LINES 5000

/* text cut after GIT_SHOW_MAX_LINES lines, with a note of the rest (takes text) */
static gchar* cut_diff(gchar* text) {
    const gchar* p = text;
    for (guint n = 0; n < GIT_SHOW_MAX_LINES; n++) {
        p = strchr(p, '\n');
        if (p == NULL) {
            return text;
        }
        p++;
    }
    guint rest = 0;
    for (const gchar* q = p; *q != '\0'; q++) {
        rest += (*q == '\n');
    }
    if (rest == 0 && *p == '\0') {
        return text;
    }
    rest = MAX(rest, 1u); /* a last line without a line break */
    gchar* cut = g_strdup_printf("%.*s\n… %u more line%s not shown\n", (int)(p - text), text, rest,
                                 rest == 1 ? "" : "s");
    g_free(text);
    return cut;
}

static void git_show_thread_func(GTask* task, gpointer source_object,
                                 gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    GitShowTaskData* td = (GitShowTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) return;

    gchar* text = git_show_commit(td->git_root, td->commit_hash);

    if (g_cancellable_is_cancelled(cancellable)) {
        g_free(text);
        g_task_return_error_if_cancelled(task);
        return;
    }

    td->diff_output = text != NULL ? cut_diff(text) : g_strdup("(No diff available or git error)");
    g_task_return_boolean(task, TRUE);
}

static void format_git_diff_markup(GtkTextBuffer* buffer, const gchar* raw_diff) {
    gtk_text_buffer_set_text(buffer, "", -1);
    if (raw_diff == NULL || raw_diff[0] == '\0') {
        return;
    }

    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(tags, "diff_add")) {
        /* Same look as the diff preview (preview_doc_view.c) */
        text_tag_create(buffer, "diff_add", "foreground", "@diff_added_fg",
                                   "paragraph-background", "@diff_added_bg", NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_del")) {
        /* Same look as the diff preview (preview_doc_view.c) */
        text_tag_create(buffer, "diff_del", "foreground", "@diff_removed_fg",
                                   "paragraph-background", "@diff_removed_bg", NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_hunk")) {
        /* Same look as the diff preview (preview_doc_view.c) */
        text_tag_create(buffer, "diff_hunk", "foreground", "@diff_hunk_fg",
                                   "paragraph-background", "@diff_hunk_bg", NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_header")) {
        text_tag_create(buffer, "diff_header", "foreground", "@syn_function", "weight", PANGO_WEIGHT_BOLD, NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "diff_dim")) {
        text_tag_create(buffer, "diff_dim", "foreground", "@fg_subtle", NULL);
    }

    gchar** lines = g_strsplit(raw_diff, "\n", -1);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);

    gboolean in_diff = FALSE; /* +/- lines before the first diff are the message and stat */
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* line = lines[i];
        const gchar* tag_name = NULL;
        if (g_str_has_prefix(line, "diff --git")) {
            in_diff = TRUE;
        }

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
        } else if (in_diff && line[0] == '+') {
            tag_name = "diff_add";
        } else if (in_diff && line[0] == '-') {
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

static void on_git_selection_changed(GtkSingleSelection* selection, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    if (app == NULL || app->git_store == NULL || app->git_current_root == NULL) return;

    GitCommit* item = gtk_single_selection_get_selected_item(selection);
    if (item == NULL || item->info == NULL || item->info->hash == NULL) {
        return;
    }
    const gchar* hash = item->info->hash;
    const gchar* short_hash = item->info->short_hash;
    const gchar* subject = item->info->subject;
    const gchar* author = item->info->author;
    const gchar* date = item->info->date;

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
}

static void on_git_log_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GitLogTaskData* td = (GitLogTaskData*)g_task_get_task_data(task);
    if (td == NULL || td->app == NULL || td->app->git_store == NULL) return;

    AppData* app = td->app;

    GtkSingleSelection* sel = app->git_selection;
    g_signal_handlers_block_by_func(sel, (gpointer)on_git_selection_changed, app);

    /* Follow the newest commit unless an older one was selected on purpose */
    gboolean follow_head = TRUE;
    GitCommitInfo* old_first = git_commit_at(app, 0);
    if (app->git_selected_hash != NULL && old_first != NULL) {
        follow_head = (g_strcmp0(old_first->hash, app->git_selected_hash) == 0);
    }

    g_list_store_remove_all(app->git_store);
    if (td->commits != NULL) {
        /* The store takes the commits over */
        g_ptr_array_set_free_func(td->commits, NULL);
        for (guint i = 0; i < td->commits->len; i++) {
            GitCommit* item = git_commit_new(g_ptr_array_index(td->commits, i));
            g_list_store_append(app->git_store, item);
            g_object_unref(item);
        }
        g_ptr_array_set_size(td->commits, 0);
    }
    gtk_single_selection_set_selected(sel, GTK_INVALID_LIST_POSITION);

    g_signal_handlers_unblock_by_func(sel, (gpointer)on_git_selection_changed, app);

    /* Select the newest commit, or keep an older commit the user selected */
    guint n = g_list_model_get_n_items(G_LIST_MODEL(app->git_store));
    if (n > 0) {
        guint target = 0;
        if (!follow_head && app->git_selected_hash != NULL) {
            for (guint i = 0; i < n; i++) {
                if (g_strcmp0(git_commit_at(app, i)->hash, app->git_selected_hash) == 0) {
                    target = i;
                    break;
                }
            }
        }
        gtk_single_selection_set_selected(sel, target); /* shows its diff */
    } else {
        if (app->git_header_label != NULL) {
            gtk_label_set_text(GTK_LABEL(app->git_header_label), "No commits in this repository.");
        }
        if (app->git_detail_buf != NULL) {
            gtk_text_buffer_set_text(app->git_detail_buf, "", -1);
        }
    }
}

/* Re-read the history of the current repository even if the root is unchanged
 * (called when .git changed: commit, checkout, reset, pull...) */
void git_tab_reload(AppData* app) {
    if (app == NULL) {
        return;
    }
    g_free(app->git_current_root);
    app->git_current_root = NULL;
    git_tab_update(app, app->base_dir);
}

void git_tab_update(AppData* app, const gchar* dir) {
    if (app == NULL || app->git_tab_box == NULL) return;

    /* Hide git commits tab when in Bookmarks, Notes or Remote view */
    if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->sidebar_view == SIDEBAR_VIEW_NOTES ||
        app->sidebar_view == SIDEBAR_VIEW_REMOTE || app->show_bookmarks) {
        gtk_widget_set_visible(app->git_tab_box, FALSE);
        return;
    }

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

/* Column cells: a label showing one GitCommitInfo string field */
static void on_git_cell_setup(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)factory;
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, (const gchar*)data);
    if (g_strcmp0((const gchar*)data, "git-subject") == 0) {
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    }
    gtk_list_item_set_child(item, label);
}

static void on_git_cell_bind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)data;
    GitCommit* commit = gtk_list_item_get_item(item);
    gsize offset = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(factory), "field-offset"));
    const gchar* text = (commit != NULL && commit->info != NULL)
                            ? G_STRUCT_MEMBER(const gchar*, commit->info, offset) : NULL;
    gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)), text != NULL ? text : "");
}

static void git_add_column(GtkWidget* view, const gchar* title, gsize field_offset,
                           const gchar* css_class, gboolean expand) {
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_object_set_data(G_OBJECT(factory), "field-offset", GSIZE_TO_POINTER(field_offset));
    g_signal_connect(factory, "setup", G_CALLBACK(on_git_cell_setup), (gpointer)css_class);
    g_signal_connect(factory, "bind", G_CALLBACK(on_git_cell_bind), NULL);
    GtkColumnViewColumn* column = gtk_column_view_column_new(title, factory);
    gtk_column_view_column_set_resizable(column, TRUE);
    gtk_column_view_column_set_expand(column, expand);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), column);
    g_object_unref(column);
}

void git_tab_init(AppData* app) {
    if (app == NULL || app->notebook == NULL) return;

    /* Commit list: newest first, one GitCommit per row */
    app->git_store = g_list_store_new(GIT_TYPE_COMMIT);
    app->git_selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(app->git_store)));
    gtk_single_selection_set_autoselect(app->git_selection, FALSE);
    gtk_single_selection_set_selected(app->git_selection, GTK_INVALID_LIST_POSITION);
    g_signal_connect(app->git_selection, "notify::selected-item",
                     G_CALLBACK(on_git_selection_changed), app);

    GtkWidget* tree_view = gtk_column_view_new(GTK_SELECTION_MODEL(app->git_selection));
    app->git_list_view = tree_view;
    focus_route_attach_list(app, tree_view, app->git_selection);
    gtk_widget_set_name(tree_view, "git-tree");
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(tree_view), FALSE);
    git_add_column(tree_view, "Commit", G_STRUCT_OFFSET(GitCommitInfo, short_hash), "git-hash", FALSE);
    git_add_column(tree_view, "Message", G_STRUCT_OFFSET(GitCommitInfo, subject), "git-subject", TRUE);
    git_add_column(tree_view, "Author", G_STRUCT_OFFSET(GitCommitInfo, author), "git-author", FALSE);
    git_add_column(tree_view, "Date", G_STRUCT_OFFSET(GitCommitInfo, date), "git-date", FALSE);

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
    set_view_text_padding(app->git_detail_view);
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
    paned_use_thin_handle(paned);
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

    app->git_tab_label = icon_label_new("document-open-recent", "Git Commits");
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
