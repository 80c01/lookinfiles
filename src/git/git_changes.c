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
    GListStore* groups;         /* GitChangeItem of each group holding files, their files below */
    GtkTreeListModel* tree;     /* the groups and, unfolded, their files */
    GtkSingleSelection* selection; /* over tree (GtkTreeListRow items) */
    gboolean unfolded[GIT_CHANGE_GROUPS]; /* each group's state, kept across reads */
    GtkWidget* summary_label;   /* above the list when it has something to say: reading, clean,
                                 * unreadable, cut */
    GtkWidget* header_label;    /* the file whose patch is shown */
    GtkTextBuffer* detail_buf;  /* its patch */
    gchar* root;                /* work tree shown, or NULL */
    gchar* selected_path;       /* file selected (kept across reads), or NULL */
    GitChangeGroup selected_group;
    GCancellable* cancellable;  /* the read of the changes */
    GCancellable* patch_cancellable; /* the patch of the selected file */
    GCancellable* count_cancellable; /* the count of the changes while hidden */
    gboolean stale;             /* read again when the view is shown */
    GitChangesCountFunc counted;
    gpointer counted_data;
};

/* One row of the list: a group (change NULL, its files in children) or a
 * changed file */
#define GIT_TYPE_CHANGE_ITEM (git_change_item_get_type())
G_DECLARE_FINAL_TYPE(GitChangeItem, git_change_item, GIT, CHANGE_ITEM, GObject)

struct _GitChangeItem {
    GObject parent_instance;
    GitChange* change;    /* owned, NULL for a group */
    GitChangeGroup group;
    GListStore* children; /* a group: its files (GitChangeItem), owned */
};

G_DEFINE_FINAL_TYPE(GitChangeItem, git_change_item, G_TYPE_OBJECT)

static void git_change_item_finalize(GObject* object) {
    GitChangeItem* self = GIT_CHANGE_ITEM(object);
    git_change_free(self->change);
    g_clear_object(&self->children);
    G_OBJECT_CLASS(git_change_item_parent_class)->finalize(object);
}

static void git_change_item_class_init(GitChangeItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = git_change_item_finalize;
}

static void git_change_item_init(GitChangeItem* self) {
    (void)self;
}

static GitChangeItem* item_new(GitChange* change) {
    GitChangeItem* self = g_object_new(GIT_TYPE_CHANGE_ITEM, NULL);
    self->change = change;
    self->group = change->group;
    return self;
}

static GitChangeItem* group_new(GitChangeGroup group) {
    GitChangeItem* self = g_object_new(GIT_TYPE_CHANGE_ITEM, NULL);
    self->group = group;
    self->children = g_list_store_new(GIT_TYPE_CHANGE_ITEM);
    return self;
}

/* GtkTreeListModel: the files of a group, none below a file */
static GListModel* children_of(gpointer item, gpointer data) {
    (void)data;
    GitChangeItem* it = item;
    return it->children != NULL ? G_LIST_MODEL(g_object_ref(it->children)) : NULL;
}

/* The row at position (new reference), NULL out of range */
static GtkTreeListRow* row_at(GitChanges* c, guint position) {
    return g_list_model_get_item(G_LIST_MODEL(c->selection), position);
}

/* The item of row (new reference) */
static GitChangeItem* item_of(GtkTreeListRow* row) {
    return row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
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
                  "#git-changes > row { padding: 1px 10px 1px 4px; }"
                  /* The look of the History list (columnview.data-table, theme.c) */
                  "#git-changes { background-color: @bg_view; color: @fg; }"
                  "#git-changes > row:hover { background-color: @bg_hover; }"
                  "#git-changes > row:selected { background-color: @bg_selected; color: @fg_bright; }"
                  "#git-changes > row:selected:backdrop { background-color: @bg_selected_inactive; }"
                  "#git-changes > row:selected label { color: @fg_bright; }");
}

/* --- Reading the changes */

typedef struct {
    gchar* root;
    GPtrArray* changes; /* GitChange*, NULL: the work tree cannot be read */
    gboolean more;      /* some were left out (GIT_CHANGES_MAX) */
    gint total;         /* how many changes in all (git_count_changes()), -1: unknown */
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
    /* The list is cut: the button still says how many there are */
    td->total = td->changes == NULL ? -1 : td->more ? git_count_changes(td->root, cancellable) : (gint)td->changes->len;
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

static void cancel_count(GitChanges* c) {
    if (c->count_cancellable != NULL) {
        g_cancellable_cancel(c->count_cancellable);
        g_clear_object(&c->count_cancellable);
    }
}

static void cancel_reads(GitChanges* c) {
    cancel_patch(c);
    cancel_count(c);
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
    GitChangeItem* item = item_of(gtk_single_selection_get_selected_item(sel));
    cancel_patch(c);
    if (item == NULL || c->root == NULL) {
        g_clear_object(&item);
        return;
    }
    if (item->change == NULL) {
        /* A group: its title, and what Enter does on it */
        gtk_label_set_text(GTK_LABEL(c->header_label), git_change_group_title(item->group));
        gtk_text_buffer_set_text(c->detail_buf, _("Enter or a click folds or unfolds the group."), -1);
        g_object_unref(item);
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
    g_object_unref(item);
}

/* Above the list, only when the list does not say it (its group rows count
 * the files): nothing to commit, the files left out; text NULL hides
 * the line */
static void show_summary(GitChanges* c, const gchar* text) {
    gtk_label_set_text(GTK_LABEL(c->summary_label), text != NULL ? text : "");
    gtk_widget_set_visible(c->summary_label, text != NULL);
}

/* The row to select after a read: the file selected before if still shown,
 * else the first file shown, else the first group */
static guint row_to_select(GitChanges* c) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->selection));
    guint first_file = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; i < n; i++) {
        GtkTreeListRow* row = row_at(c, i);
        GitChangeItem* item = item_of(row);
        gboolean file = item->change != NULL;
        gboolean same = file && item->group == c->selected_group && g_strcmp0(item->change->path, c->selected_path) == 0;
        g_object_unref(item);
        g_object_unref(row);
        if (same) return i;
        if (file && first_file == GTK_INVALID_LIST_POSITION) first_file = i;
    }
    return first_file != GTK_INVALID_LIST_POSITION ? first_file : n > 0 ? 0 : GTK_INVALID_LIST_POSITION;
}

/* Unfold or fold each group as it was */
static void restore_folding(GitChanges* c) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->groups));
    for (guint i = 0; i < n; i++) {
        GtkTreeListRow* row = gtk_tree_list_model_get_child_row(c->tree, i);
        GitChangeItem* item = item_of(row);
        if (row != NULL && item != NULL) gtk_tree_list_row_set_expanded(row, c->unfolded[item->group]);
        g_clear_object(&item);
        g_clear_object(&row);
    }
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
        GitChangeItem* group = NULL;
        for (guint i = 0; i < td->changes->len; i++) {
            GitChange* ch = g_ptr_array_index(td->changes, i);
            if (group == NULL || group->group != ch->group) { /* the changes come group by group */
                group = group_new(ch->group);
                g_ptr_array_add(items, group);
            }
            GitChangeItem* file = item_new(ch);
            g_list_store_append(group->children, file);
            g_object_unref(file);
        }
        g_ptr_array_set_size(td->changes, 0);
    }

    g_signal_handlers_block_by_func(c->selection, (gpointer)on_selection_changed, c);
    g_list_store_splice(c->groups, 0, g_list_model_get_n_items(G_LIST_MODEL(c->groups)), items->pdata, items->len);
    restore_folding(c);
    gtk_single_selection_set_selected(c->selection, GTK_INVALID_LIST_POSITION);
    g_signal_handlers_unblock_by_func(c->selection, (gpointer)on_selection_changed, c);
    g_ptr_array_unref(items);

    guint files = counts[GIT_CHANGE_STAGED] + counts[GIT_CHANGE_UNSTAGED] + counts[GIT_CHANGE_UNTRACKED];
    gchar* cut = td->more ? g_strdup_printf(_("Only the first %u files are listed."), GIT_CHANGES_MAX) : NULL;
    show_summary(c, td->changes == NULL ? _("The changes of this work tree cannot be read.")
                    : files == 0        ? _("Nothing to commit: the work tree is clean")
                                        : cut);
    g_free(cut);
    if (c->counted != NULL) c->counted(td->total >= 0 ? (guint)td->total : files, c->counted_data);

    guint row = row_to_select(c);
    if (row != GTK_INVALID_LIST_POSITION) {
        gtk_single_selection_set_selected(c->selection, row); /* shows its patch */
    } else {
        g_clear_pointer(&c->selected_path, g_free);
        gtk_label_set_text(GTK_LABEL(c->header_label), "");
        gtk_text_buffer_set_text(c->detail_buf, "", -1);
    }
}

/* --- The count alone, while the view is hidden (for its button) */

typedef struct {
    gchar* root;
    gint count;
} CountTask;

static void count_task_free(gpointer data) {
    CountTask* td = data;
    g_free(td->root);
    g_free(td);
}

static void count_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    CountTask* td = data;
    td->count = git_count_changes(td->root, cancellable);
    if (g_task_return_error_if_cancelled(task)) return;
    g_task_return_boolean(task, TRUE);
}

static void on_count_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: a newer count or read, or exit */
    GitChanges* c = data;
    CountTask* td = g_task_get_task_data(task);
    if (c->counted != NULL) c->counted(td->count >= 0 ? (guint)td->count : G_MAXUINT, c->counted_data);
}

static void start_count(GitChanges* c) {
    cancel_count(c);
    c->count_cancellable = g_cancellable_new();
    CountTask* td = g_new0(CountTask, 1);
    td->root = g_strdup(c->root);
    GTask* task = g_task_new(NULL, c->count_cancellable, on_count_ready, c);
    g_task_set_task_data(task, td, count_task_free);
    g_task_run_in_thread(task, count_thread);
    g_object_unref(task);
}

static void start_read(GitChanges* c) {
    cancel_reads(c);
    c->stale = FALSE;
    c->cancellable = g_cancellable_new();
    ReadTask* td = g_new0(ReadTask, 1);
    td->root = g_strdup(c->root);
    if (g_list_model_get_n_items(G_LIST_MODEL(c->groups)) == 0) {
        show_summary(c, _("Reading the changes…"));
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
        g_list_store_remove_all(c->groups);
        gtk_label_set_text(GTK_LABEL(c->header_label), "");
        gtk_text_buffer_set_text(c->detail_buf, "", -1);
    }
    if (c->root == NULL) return;
    c->stale = !gtk_widget_get_mapped(c->widget);
    if (!c->stale) {
        start_read(c);
    } else {
        start_count(c); /* hidden: the count only, for the button */
    }
}

/* The view comes on screen: the work tree may have changed anywhere below
 * the folder watched, read it again */
static void on_view_map(GtkWidget* widget, gpointer data) {
    (void)widget;
    GitChanges* c = data;
    if (c->root != NULL) start_read(c);
}

/* Fold or unfold the group of row (unfold: TRUE, FALSE; -1: the other way) */
static void fold(GtkTreeListRow* row, gint unfold) {
    if (row == NULL || !gtk_tree_list_row_is_expandable(row)) return;
    gtk_tree_list_row_set_expanded(row, unfold < 0 ? !gtk_tree_list_row_get_expanded(row) : unfold);
}

/* Enter or a double click: on a file, preview it (the Preview tab in front);
 * on a group, fold or unfold it */
static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    GitChanges* c = data;
    GtkTreeListRow* row = row_at(c, position);
    GitChangeItem* item = item_of(row);
    if (item == NULL) {
        g_clear_object(&row);
        return;
    }
    if (item->change == NULL) {
        fold(row, -1);
    } else if (item->change->status != 'D' && c->root != NULL) {
        AppData* app = c->app;
        g_free(app->preview_request_path);
        app->preview_request_path = g_build_filename(c->root, item->change->path, NULL);
        app->preview_request_line = -1;
        app_emit(app, APP_EVENT_PREVIEW_PATH);
        g_clear_pointer(&app->preview_request_path, g_free); /* not taken: nothing subscribed */
    }
    g_object_unref(item);
    g_object_unref(row);
}

/* Left folds the group (on a file: goes to its group), Right unfolds it;
 * h / l in Vim mode, C-b / C-f in Emacs mode. After the navigation keys
 * (focus_route_attach_list()), which leave these to the list. */
static gboolean on_list_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)ctrl;
    (void)keycode;
    GitChanges* c = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    gint dir = 0; /* -1: fold, 1: unfold */
    if (mods == 0 && (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)) dir = -1;
    if (mods == 0 && (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)) dir = 1;
    switch (app_nav_style(c->app)) {
    case NAV_STYLE_VIM:
        if (mods == 0 && keyval == GDK_KEY_h) dir = -1;
        if (mods == 0 && keyval == GDK_KEY_l) dir = 1;
        break;
    case NAV_STYLE_EMACS:
        if (mods == GDK_CONTROL_MASK && gdk_keyval_to_lower(keyval) == GDK_KEY_b) dir = -1;
        if (mods == GDK_CONTROL_MASK && gdk_keyval_to_lower(keyval) == GDK_KEY_f) dir = 1;
        break;
    default: break;
    }
    if (dir == 0) return FALSE;
    GtkTreeListRow* row = gtk_single_selection_get_selected_item(c->selection);
    if (row == NULL) return TRUE;
    if (gtk_tree_list_row_is_expandable(row)) {
        fold(row, dir > 0);
    } else if (dir < 0) {
        GtkTreeListRow* parent = gtk_tree_list_row_get_parent(row); /* the file's group */
        if (parent != NULL) {
            gtk_single_selection_set_selected(c->selection, gtk_tree_list_row_get_position(parent));
            g_object_unref(parent);
        }
    }
    return TRUE;
}

/* A click on a group row folds or unfolds it (the press is the row's: a
 * double click does not fold it back) */
static void on_group_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)x;
    (void)y;
    (void)data;
    GtkWidget* box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkListItem* li = g_object_get_data(G_OBJECT(box), "list-item");
    GtkTreeListRow* row = li != NULL ? gtk_list_item_get_item(li) : NULL;
    if (row == NULL || !gtk_tree_list_row_is_expandable(row)) return; /* a file: the list selects it */
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (n_press == 1) fold(row, -1);
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
    gtk_widget_set_hexpand(box, TRUE); /* the whole row takes the click, even a group's (no folder label) */
    g_object_set_data(G_OBJECT(box), "list-item", li);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    /* Before the row's own click, which would only select the group */
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_group_pressed), NULL);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));
    GtkWidget* status = row_label(box, "git-status");
    gtk_label_set_width_chars(GTK_LABEL(status), 1);
    GtkWidget* name = row_label(box, NULL);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    GtkWidget* dir = row_label(box, "git-dir");
    gtk_label_set_ellipsize(GTK_LABEL(dir), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(dir, TRUE);
    row_label(box, "git-added");
    row_label(box, "git-removed");
    GtkWidget* expander = gtk_tree_expander_new();
    gtk_widget_set_hexpand(expander, TRUE);
    gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(expander), TRUE);
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    gtk_list_item_set_child(li, expander);
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

/* A group folded or unfolded (arrow, click, keys): kept for the next reads */
static void on_row_expanded(GtkTreeListRow* row, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    GitChanges* c = data;
    GitChangeItem* item = item_of(row);
    if (item != NULL && item->change == NULL) c->unfolded[item->group] = gtk_tree_list_row_get_expanded(row);
    g_clear_object(&item);
}

static void on_row_bind(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    GtkTreeListRow* row = gtk_list_item_get_item(li);
    GtkWidget* expander = gtk_list_item_get_child(li);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);
    GitChangeItem* item = item_of(row);
    GtkWidget* status = gtk_widget_get_first_child(gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander)));
    GtkWidget* name = gtk_widget_get_next_sibling(status);
    GtkWidget* dir = gtk_widget_get_next_sibling(name);
    GtkWidget* added = gtk_widget_get_next_sibling(dir);
    GtkWidget* removed = gtk_widget_get_next_sibling(added);
    for (gsize i = 0; i < G_N_ELEMENTS(STATUS_CLASSES); i++) gtk_widget_remove_css_class(status, STATUS_CLASSES[i]);
    g_object_unref(item); /* the row keeps it */

    GitChange* ch = item->change;
    gboolean file = ch != NULL;
    gtk_widget_set_visible(status, file);
    gtk_widget_set_visible(added, file);
    gtk_widget_set_visible(removed, file);
    gtk_widget_set_visible(dir, file);
    if (!file) {
        /* A group: its title and how many files, folded or unfolded */
        gtk_widget_add_css_class(name, "git-group");
        gtk_widget_set_tooltip_text(name, _("Click or Enter: fold or unfold"));
        gchar* text = g_strdup_printf("%s (%u)", git_change_group_title(item->group),
                                      g_list_model_get_n_items(G_LIST_MODEL(item->children)));
        gtk_label_set_text(GTK_LABEL(name), text);
        g_free(text);
        g_signal_connect(row, "notify::expanded", G_CALLBACK(on_row_expanded), data);
        return;
    }
    gtk_widget_remove_css_class(name, "git-group");
    gtk_widget_set_tooltip_text(name, ch->path);
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

static void on_row_unbind(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    GtkTreeListRow* row = gtk_list_item_get_item(li);
    if (row != NULL) g_signal_handlers_disconnect_by_func(row, (gpointer)on_row_expanded, data);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(gtk_list_item_get_child(li)), NULL);
}

void git_changes_cancel(GitChanges* c) {
    if (c != NULL) cancel_reads(c);
}

/* With the view's widget */
static void git_changes_free(gpointer data) {
    GitChanges* c = data;
    cancel_reads(c);
    g_clear_object(&c->groups);
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

    for (gint g = 0; g < GIT_CHANGE_GROUPS; g++) c->unfolded[g] = TRUE;
    c->groups = g_list_store_new(GIT_TYPE_CHANGE_ITEM);
    c->tree = gtk_tree_list_model_new(G_LIST_MODEL(g_object_ref(c->groups)), FALSE, FALSE, children_of, NULL, NULL);
    c->selection = gtk_single_selection_new(G_LIST_MODEL(c->tree));
    gtk_single_selection_set_autoselect(c->selection, FALSE);
    gtk_single_selection_set_can_unselect(c->selection, TRUE);
    g_signal_connect(c->selection, "notify::selected-item", G_CALLBACK(on_selection_changed), c);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_row_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_row_bind), c);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_row_unbind), c);
    GtkWidget* list = gtk_list_view_new(GTK_SELECTION_MODEL(c->selection), factory);
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_list_key), c);
    gtk_widget_add_controller(list, keys);
    gtk_widget_set_name(list, "git-changes");
    gtk_widget_set_tooltip_text(list, _("Enter: preview the file, fold or unfold the group"));
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
    gtk_widget_set_visible(c->summary_label, FALSE);

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
