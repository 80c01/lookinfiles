/* The Changes view of the Git tab (see git_changes.h) */
#include "git/git_changes.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "theme/text_style.h"
#include "theme/theme.h"
#include "git/git_diff_text.h"
#include "git/git_worktree.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"

/* Files listed at most: a huge untracked folder is not read file by file */
#define GIT_CHANGES_MAX 2000
/* Data of the view's widget: the GitChanges, freed with it */
#define GIT_CHANGES_KEY "lookin-git-changes"

struct GitChanges {
    AppData* app;
    GtkWidget* widget;          /* the view (owns this struct) */
    GListStore* store;          /* GitChangeItem rows: a header, then the files of a group */
    GtkSingleSelection* selection;
    GtkWidget* summary_label;   /* how many files in each group, above the list */
    GtkWidget* header_label;    /* the file whose patch is shown */
    GtkTextBuffer* detail_buf;  /* its patch */
    gchar* root;                /* work tree shown, or NULL */
    gchar* selected_path;       /* file selected (kept across reads), or NULL */
    GitChangeGroup selected_group;
    GCancellable* cancellable;  /* the read of the changes */
    GCancellable* patch_cancellable; /* the patch of the selected file */
    gboolean stale;             /* read again when the view is shown */
    GitChangesCountFunc counted;
    gpointer counted_data;
};

/* One row of the list: a group header (change NULL) or a file */
#define GIT_TYPE_CHANGE_ITEM (git_change_item_get_type())
G_DECLARE_FINAL_TYPE(GitChangeItem, git_change_item, GIT, CHANGE_ITEM, GObject)

struct _GitChangeItem {
    GObject parent_instance;
    GitChange* change;    /* owned, NULL for a header */
    GitChangeGroup group;
    guint count;          /* a header: the files of its group */
};

G_DEFINE_FINAL_TYPE(GitChangeItem, git_change_item, G_TYPE_OBJECT)

static void git_change_item_finalize(GObject* object) {
    GitChangeItem* self = GIT_CHANGE_ITEM(object);
    git_change_free(self->change);
    G_OBJECT_CLASS(git_change_item_parent_class)->finalize(object);
}

static void git_change_item_class_init(GitChangeItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = git_change_item_finalize;
}

static void git_change_item_init(GitChangeItem* self) {
    (void)self;
}

static GitChangeItem* item_new(GitChange* change, GitChangeGroup group, guint count) {
    GitChangeItem* self = g_object_new(GIT_TYPE_CHANGE_ITEM, NULL);
    self->change = change;
    self->group = group;
    self->count = count;
    return self;
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css("#git-changes label.git-status { font-weight: bold; font-family: @font_code; }"
                  "#git-changes label.git-st-modified { color: @git_modified; }"
                  "#git-changes label.git-st-added { color: @git_added; }"
                  "#git-changes label.git-st-deleted { color: @git_deleted; }"
                  "#git-changes label.git-st-renamed { color: @git_renamed; }"
                  "#git-changes label.git-st-conflict { color: @git_conflict; }"
                  "#git-changes label.git-dir { color: @fg_subtle; }"
                  "#git-changes label.git-group { color: @fg_strong; font-weight: 600; }"
                  "#git-changes label.git-added { color: @git_added; font-feature-settings: \"tnum\"; }"
                  "#git-changes label.git-removed { color: @git_deleted; font-feature-settings: \"tnum\"; }"
                  "#git-changes > row { padding: 1px 10px; }");
}

/* --- Reading the changes */

typedef struct {
    gchar* root;
    GPtrArray* changes; /* GitChange*, NULL: the work tree cannot be read */
    gboolean more;      /* some were left out (GIT_CHANGES_MAX) */
} ReadTask;

static void read_task_free(gpointer data) {
    ReadTask* td = data;
    g_free(td->root);
    if (td->changes != NULL) g_ptr_array_unref(td->changes);
    g_free(td);
}

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    ReadTask* td = data;
    td->changes = git_read_changes(td->root, GIT_CHANGES_MAX, cancellable, &td->more);
    if (g_task_return_error_if_cancelled(task)) return;
    g_task_return_boolean(task, TRUE);
}

typedef struct {
    gchar* root;
    gchar* path;
    GitChangeGroup group;
    gchar* patch;
} PatchTask;

static void patch_task_free(gpointer data) {
    PatchTask* td = data;
    g_free(td->root);
    g_free(td->path);
    g_free(td->patch);
    g_free(td);
}

static void patch_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    PatchTask* td = data;
    gchar* text = git_change_patch(td->root, td->path, td->group);
    if (g_task_return_error_if_cancelled(task)) {
        g_free(text);
        return;
    }
    td->patch = text != NULL ? git_diff_text_cut(text, GIT_DIFF_MAX_LINES) : NULL;
    g_task_return_boolean(task, TRUE);
}

static void cancel_patch(GitChanges* c) {
    if (c->patch_cancellable != NULL) {
        g_cancellable_cancel(c->patch_cancellable);
        g_clear_object(&c->patch_cancellable);
    }
}

static void cancel_reads(GitChanges* c) {
    cancel_patch(c);
    if (c->cancellable != NULL) {
        g_cancellable_cancel(c->cancellable);
        g_clear_object(&c->cancellable);
    }
}

/* The tasks reach their GitChanges through their callback data: they are
 * cancelled before it is freed, and a cancelled task's callback returns
 * before touching it */
static void on_patch_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: another file, a new read or exit */
    GitChanges* c = data;
    PatchTask* td = g_task_get_task_data(task);
    if (td->patch == NULL) {
        gtk_text_buffer_set_text(c->detail_buf, _("The changes of this file cannot be read."), -1);
    } else if (td->patch[0] == '\0') {
        gtk_text_buffer_set_text(c->detail_buf, _("No change left in this file."), -1);
    } else {
        git_diff_text_show(c->detail_buf, td->patch);
    }
}

/* Above the patch: the file and its group, with its old name when renamed */
static void show_file_header(GitChanges* c, const GitChange* ch) {
    const gchar* group = git_change_group_title(ch->group);
    gchar* text = ch->old_path != NULL
                      /* TRANSLATORS: above the patch of a renamed file: old name, new name, group
                       * ("Staged") */
                      ? g_strdup_printf(_("%s → %s  •  %s"), ch->old_path, ch->path, group)
                      /* TRANSLATORS: above the patch of a file: its path, its group ("Staged") */
                      : g_strdup_printf(_("%s  •  %s"), ch->path, group);
    gtk_label_set_text(GTK_LABEL(c->header_label), text);
    g_free(text);
}

static void on_selection_changed(GtkSingleSelection* sel, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    GitChanges* c = data;
    GitChangeItem* item = gtk_single_selection_get_selected_item(sel);
    cancel_patch(c);
    if (item == NULL || c->root == NULL) return;
    if (item->change == NULL) {
        /* A header: what its group holds */
        gchar* text = g_strdup_printf(ngettext("%s: %u file", "%s: %u files", item->count),
                                      git_change_group_title(item->group), item->count);
        gtk_label_set_text(GTK_LABEL(c->header_label), text);
        g_free(text);
        gtk_text_buffer_set_text(c->detail_buf, "", -1);
        return;
    }
    g_free(c->selected_path);
    c->selected_path = g_strdup(item->change->path);
    c->selected_group = item->group;
    show_file_header(c, item->change);

    PatchTask* td = g_new0(PatchTask, 1);
    td->root = g_strdup(c->root);
    td->path = g_strdup(item->change->path);
    td->group = item->group;
    c->patch_cancellable = g_cancellable_new();
    GTask* task = g_task_new(NULL, c->patch_cancellable, on_patch_ready, c);
    g_task_set_task_data(task, td, patch_task_free);
    g_task_run_in_thread(task, patch_thread);
    g_object_unref(task);
}

/* Above the list: the files of each group ("2 staged, 3 changed, 1 untracked") */
static void show_summary(GitChanges* c, const guint counts[GIT_CHANGE_GROUPS], gboolean more) {
    GString* text = g_string_new(NULL);
    if (counts[GIT_CHANGE_STAGED] > 0) {
        g_string_append_printf(text, ngettext("%u staged", "%u staged", counts[GIT_CHANGE_STAGED]),
                               counts[GIT_CHANGE_STAGED]);
    }
    if (counts[GIT_CHANGE_UNSTAGED] > 0) {
        if (text->len > 0) g_string_append(text, ", ");
        g_string_append_printf(text, ngettext("%u changed", "%u changed", counts[GIT_CHANGE_UNSTAGED]),
                               counts[GIT_CHANGE_UNSTAGED]);
    }
    if (counts[GIT_CHANGE_UNTRACKED] > 0) {
        if (text->len > 0) g_string_append(text, ", ");
        g_string_append_printf(text, ngettext("%u untracked", "%u untracked", counts[GIT_CHANGE_UNTRACKED]),
                               counts[GIT_CHANGE_UNTRACKED]);
    }
    if (more) {
        g_string_append(text, " ");
        g_string_append_printf(text, _("(the first %u files)"), GIT_CHANGES_MAX);
    }
    if (text->len == 0) g_string_append(text, _("Nothing to commit: the work tree is clean"));
    gtk_label_set_text(GTK_LABEL(c->summary_label), text->str);
    g_string_free(text, TRUE);
}

/* The row to select after a read: the file selected before if still
 * there, else the first file */
static guint row_to_select(GitChanges* c) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->store));
    guint first = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; i < n; i++) {
        GitChangeItem* item = g_list_model_get_item(G_LIST_MODEL(c->store), i);
        gboolean file = item->change != NULL;
        gboolean same = file && item->group == c->selected_group && g_strcmp0(item->change->path, c->selected_path) == 0;
        g_object_unref(item);
        if (same) return i;
        if (file && first == GTK_INVALID_LIST_POSITION) first = i;
    }
    return first;
}

static void on_read_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: a newer read, or exit */
    GitChanges* c = data;
    ReadTask* td = g_task_get_task_data(task);

    guint counts[GIT_CHANGE_GROUPS] = { 0 };
    GPtrArray* items = g_ptr_array_new_with_free_func(g_object_unref);
    if (td->changes != NULL) {
        for (guint i = 0; i < td->changes->len; i++) {
            const GitChange* ch = g_ptr_array_index(td->changes, i);
            counts[ch->group]++;
        }
        g_ptr_array_set_free_func(td->changes, NULL); /* the items take them over */
        GitChangeGroup group = GIT_CHANGE_GROUPS;
        for (guint i = 0; i < td->changes->len; i++) {
            GitChange* ch = g_ptr_array_index(td->changes, i);
            if (ch->group != group) {
                group = ch->group;
                g_ptr_array_add(items, item_new(NULL, group, counts[group]));
            }
            g_ptr_array_add(items, item_new(ch, ch->group, 0));
        }
        g_ptr_array_set_size(td->changes, 0);
    }

    g_signal_handlers_block_by_func(c->selection, (gpointer)on_selection_changed, c);
    g_list_store_splice(c->store, 0, g_list_model_get_n_items(G_LIST_MODEL(c->store)), items->pdata, items->len);
    gtk_single_selection_set_selected(c->selection, GTK_INVALID_LIST_POSITION);
    g_signal_handlers_unblock_by_func(c->selection, (gpointer)on_selection_changed, c);
    g_ptr_array_unref(items);

    if (td->changes == NULL) {
        gtk_label_set_text(GTK_LABEL(c->summary_label), _("The changes of this work tree cannot be read."));
    } else {
        show_summary(c, counts, td->more);
    }
    guint files = counts[GIT_CHANGE_STAGED] + counts[GIT_CHANGE_UNSTAGED] + counts[GIT_CHANGE_UNTRACKED];
    if (c->counted != NULL) c->counted(files, c->counted_data);

    guint row = row_to_select(c);
    if (row != GTK_INVALID_LIST_POSITION) {
        gtk_single_selection_set_selected(c->selection, row); /* shows its patch */
    } else {
        g_clear_pointer(&c->selected_path, g_free);
        gtk_label_set_text(GTK_LABEL(c->header_label), td->changes != NULL ? _("No uncommitted changes.") : "");
        gtk_text_buffer_set_text(c->detail_buf, "", -1);
    }
}

static void start_read(GitChanges* c) {
    cancel_reads(c);
    c->stale = FALSE;
    c->cancellable = g_cancellable_new();
    ReadTask* td = g_new0(ReadTask, 1);
    td->root = g_strdup(c->root);
    if (g_list_model_get_n_items(G_LIST_MODEL(c->store)) == 0) {
        gtk_label_set_text(GTK_LABEL(c->summary_label), _("Reading the changes…"));
    }
    GTask* task = g_task_new(NULL, c->cancellable, on_read_ready, c);
    g_task_set_task_data(task, td, read_task_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
}

void git_changes_set_root(GitChanges* c, const gchar* root) {
    if (c == NULL) return;
    if (g_strcmp0(c->root, root) != 0) {
        cancel_reads(c);
        g_free(c->root);
        c->root = g_strdup(root);
        g_clear_pointer(&c->selected_path, g_free);
        g_list_store_remove_all(c->store);
        gtk_label_set_text(GTK_LABEL(c->header_label), "");
        gtk_text_buffer_set_text(c->detail_buf, "", -1);
    }
    if (c->root == NULL) return;
    c->stale = !gtk_widget_get_mapped(c->widget);
    if (!c->stale) {
        start_read(c);
    } else if (c->counted != NULL) {
        c->counted(G_MAXUINT, c->counted_data); /* the last count may be wrong now */
    }
}

/* The view comes on screen: the work tree may have changed anywhere below
 * the folder watched, read it again */
static void on_view_map(GtkWidget* widget, gpointer data) {
    (void)widget;
    GitChanges* c = data;
    if (c->root != NULL) start_read(c);
}

/* Enter or a double click on a file: preview it (the Preview tab in front) */
static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    GitChanges* c = data;
    GitChangeItem* item = g_list_model_get_item(G_LIST_MODEL(c->store), position);
    if (item == NULL) return;
    if (item->change != NULL && item->change->status != 'D' && c->root != NULL) {
        AppData* app = c->app;
        g_free(app->preview_request_path);
        app->preview_request_path = g_build_filename(c->root, item->change->path, NULL);
        app->preview_request_line = -1;
        app_emit(app, APP_EVENT_PREVIEW_PATH);
        g_clear_pointer(&app->preview_request_path, g_free); /* not taken: nothing subscribed */
    }
    g_object_unref(item);
}

/* --- Rows: status letter, file name, its folder, lines added / removed */

static GtkWidget* row_label(GtkWidget* box, const gchar* css_class) {
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    if (css_class != NULL) gtk_widget_add_css_class(label, css_class);
    gtk_box_append(GTK_BOX(box), label);
    return label;
}

static void on_row_setup(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    (void)data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* status = row_label(box, "git-status");
    gtk_label_set_width_chars(GTK_LABEL(status), 1);
    GtkWidget* name = row_label(box, NULL);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    GtkWidget* dir = row_label(box, "git-dir");
    gtk_label_set_ellipsize(GTK_LABEL(dir), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(dir, TRUE);
    row_label(box, "git-added");
    row_label(box, "git-removed");
    gtk_list_item_set_child(li, box);
}

static const gchar* const STATUS_CLASSES[] = { "git-st-modified", "git-st-added", "git-st-deleted",
                                                "git-st-renamed", "git-st-conflict" };

static const gchar* status_class(gchar status) {
    switch (status) {
    case 'A': case 'U': return "git-st-added";
    case 'D': return "git-st-deleted";
    case 'R': return "git-st-renamed";
    case '!': return "git-st-conflict";
    default: return "git-st-modified";
    }
}

static void on_row_bind(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    (void)data;
    GitChangeItem* item = gtk_list_item_get_item(li);
    GtkWidget* status = gtk_widget_get_first_child(gtk_list_item_get_child(li));
    GtkWidget* name = gtk_widget_get_next_sibling(status);
    GtkWidget* dir = gtk_widget_get_next_sibling(name);
    GtkWidget* added = gtk_widget_get_next_sibling(dir);
    GtkWidget* removed = gtk_widget_get_next_sibling(added);
    for (gsize i = 0; i < G_N_ELEMENTS(STATUS_CLASSES); i++) gtk_widget_remove_css_class(status, STATUS_CLASSES[i]);

    GitChange* ch = item->change;
    gboolean file = ch != NULL;
    gtk_widget_set_visible(status, file);
    gtk_widget_set_visible(added, file);
    gtk_widget_set_visible(removed, file);
    if (file) {
        gtk_widget_remove_css_class(name, "git-group");
    } else {
        gtk_widget_add_css_class(name, "git-group");
    }
    gtk_widget_set_tooltip_text(name, file ? ch->path : NULL);
    if (!file) {
        gchar* text = g_strdup_printf("%s (%u)", git_change_group_title(item->group), item->count);
        gtk_label_set_text(GTK_LABEL(name), text);
        g_free(text);
        gtk_label_set_text(GTK_LABEL(dir), "");
        return;
    }
    gchar letter[2] = { ch->status, '\0' };
    gtk_label_set_text(GTK_LABEL(status), letter);
    gtk_widget_add_css_class(status, status_class(ch->status));
    gchar* base = g_path_get_basename(ch->path);
    gtk_label_set_text(GTK_LABEL(name), base);
    g_free(base);
    gchar* folder = g_path_get_dirname(ch->path);
    gchar* where = ch->old_path != NULL
                       /* TRANSLATORS: after the name of a renamed file in the Changes view */
                       ? g_strdup_printf(_("renamed from %s"), ch->old_path)
                       : g_strdup(g_strcmp0(folder, ".") == 0 ? "" : folder);
    gtk_label_set_text(GTK_LABEL(dir), where);
    g_free(where);
    g_free(folder);
    if (ch->added < 0) {
        gtk_label_set_text(GTK_LABEL(added), _("binary"));
        gtk_widget_set_visible(removed, FALSE);
        return;
    }
    gchar* plus = g_strdup_printf("+%d", ch->added);
    gchar* minus = g_strdup_printf("−%d", ch->removed);
    gtk_label_set_text(GTK_LABEL(added), plus);
    gtk_label_set_text(GTK_LABEL(removed), minus);
    g_free(plus);
    g_free(minus);
}

void git_changes_cancel(GitChanges* c) {
    if (c != NULL) cancel_reads(c);
}

/* With the view's widget */
static void git_changes_free(gpointer data) {
    GitChanges* c = data;
    cancel_reads(c);
    g_clear_object(&c->store);
    g_clear_object(&c->detail_buf);
    g_free(c->root);
    g_free(c->selected_path);
    g_free(c);
}

GitChanges* git_changes_new(AppData* app, GitChangesCountFunc counted, gpointer data) {
    ensure_css();
    GitChanges* c = g_new0(GitChanges, 1);
    c->app = app;
    c->counted = counted;
    c->counted_data = data;

    c->store = g_list_store_new(GIT_TYPE_CHANGE_ITEM);
    c->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(c->store)));
    gtk_single_selection_set_autoselect(c->selection, FALSE);
    gtk_single_selection_set_can_unselect(c->selection, TRUE);
    g_signal_connect(c->selection, "notify::selected-item", G_CALLBACK(on_selection_changed), c);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_row_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_row_bind), NULL);
    GtkWidget* list = gtk_list_view_new(GTK_SELECTION_MODEL(c->selection), factory);
    gtk_widget_set_name(list, "git-changes");
    gtk_widget_set_tooltip_text(list, _("Enter: preview the file"));
    g_signal_connect(list, "activate", G_CALLBACK(on_activate), c);
    focus_route_attach_list(app, list, c->selection);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_widget_set_size_request(scroll, -1, 120);

    c->summary_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(c->summary_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(c->summary_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(c->summary_label, "cell-muted");
    gtk_widget_set_margin_start(c->summary_label, 12);
    gtk_widget_set_margin_top(c->summary_label, 6);
    gtk_widget_set_margin_bottom(c->summary_label, 6);

    GtkWidget* list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(list_box), c->summary_label);
    gtk_box_append(GTK_BOX(list_box), scroll);

    /* Below: the file and its patch */
    GtkWidget* header_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(header_bar, "git-header-bar");
    c->header_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(c->header_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(c->header_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(c->header_label, TRUE);
    gtk_box_append(GTK_BOX(header_bar), c->header_label);

    c->detail_buf = gtk_text_buffer_new(NULL);
    GtkWidget* detail_view = gtk_text_view_new_with_buffer(c->detail_buf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(detail_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(detail_view), FALSE);
    set_view_text_padding(detail_view);
    set_view_monospace(detail_view, TRUE);
    apply_viewer_background(detail_view);
    GtkWidget* detail_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(detail_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(detail_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(detail_scroll), detail_view);

    GtkWidget* detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(detail_box), header_bar);
    gtk_box_append(GTK_BOX(detail_box), detail_scroll);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    paned_use_thin_handle(paned);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), list_box);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(paned), detail_box);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 220);

    c->widget = paned;
    g_object_set_data_full(G_OBJECT(paned), GIT_CHANGES_KEY, c, git_changes_free);
    g_signal_connect(paned, "map", G_CALLBACK(on_view_map), c);
    return c;
}

GtkWidget* git_changes_widget(GitChanges* c) {
    return c != NULL ? c->widget : NULL;
}
