/* The Stashes & Tags view of the Git tab (see git_stash_view.h) */
#include "git/git_stash_view.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "theme/text_style.h"
#include "theme/theme.h"
#include "git/git_diff_text.h"
#include "git/git_stash_tags.h"
#include "git/git_utils.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"

/* Data of the view's widget: the GitStashView, freed with it */
#define GIT_STASH_VIEW_KEY "lookin-git-stash-view"
/* Data of a row's box and of a menu: the GtkListItem / the GitStashItem */
#define GIT_STASH_ITEM_KEY "lookin-git-stash-item"

/* The groups of the list, in their order */
typedef enum { GROUP_STASHES, GROUP_TAGS, GROUP_COUNT } StashGroup;

struct GitStashView {
    AppData* app;
    GtkWidget* widget;          /* the view (owns this struct) */
    GListStore* groups;         /* GitStashItem of each group holding entries, their entries below */
    GtkTreeListModel* tree;     /* the groups and, unfolded, their entries */
    GtkSingleSelection* selection; /* over tree (GtkTreeListRow items) */
    gboolean unfolded[GROUP_COUNT]; /* each group's state, kept across reads */
    GtkWidget* summary_label;   /* above the list when it has something to say: reading, none, unreadable */
    GtkWidget* header_label;    /* the entry whose content is shown */
    GtkTextBuffer* detail_buf;  /* its content */
    gchar* root;                /* repository shown, or NULL */
    gchar* selected_name;       /* entry selected (kept across reads), or NULL */
    StashGroup selected_group;
    GCancellable* cancellable;  /* the read of the lists */
    GCancellable* show_cancellable; /* the content of the selected entry */
};

/* One row of the list: a group (its entries in children), a stash or a tag */
#define GIT_TYPE_STASH_ITEM (git_stash_item_get_type())
G_DECLARE_FINAL_TYPE(GitStashItem, git_stash_item, GIT, STASH_ITEM, GObject)

struct _GitStashItem {
    GObject parent_instance;
    StashGroup group;
    GListStore* children; /* a group: its entries (GitStashItem), owned */
    GitStashInfo* stash;  /* owned, or NULL */
    GitTagInfo* tag;      /* owned, or NULL */
};

G_DEFINE_FINAL_TYPE(GitStashItem, git_stash_item, G_TYPE_OBJECT)

static void git_stash_item_finalize(GObject* object) {
    GitStashItem* self = GIT_STASH_ITEM(object);
    g_clear_object(&self->children);
    git_stash_info_free(self->stash);
    git_tag_info_free(self->tag);
    G_OBJECT_CLASS(git_stash_item_parent_class)->finalize(object);
}

static void git_stash_item_class_init(GitStashItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = git_stash_item_finalize;
}

static void git_stash_item_init(GitStashItem* self) {
    (void)self;
}

static GitStashItem* group_new(StashGroup group) {
    GitStashItem* self = g_object_new(GIT_TYPE_STASH_ITEM, NULL);
    self->group = group;
    self->children = g_list_store_new(GIT_TYPE_STASH_ITEM);
    return self;
}

static gboolean is_group(const GitStashItem* item) {
    return item->children != NULL;
}

/* The name of an entry ("stash@{0}", "v1.0") */
static const gchar* item_name(const GitStashItem* item) {
    return item->stash != NULL ? item->stash->name : item->tag != NULL ? item->tag->name : NULL;
}

static const gchar* group_title(StashGroup group) {
    /* TRANSLATORS: the groups of the Stashes & Tags view of the Git tab */
    return group == GROUP_STASHES ? _("Stashes") : _("Tags");
}

/* GtkTreeListModel: the entries of a group, none below an entry */
static GListModel* children_of(gpointer item, gpointer data) {
    (void)data;
    GitStashItem* it = item;
    return it->children != NULL ? G_LIST_MODEL(g_object_ref(it->children)) : NULL;
}

/* The row at position (new reference), NULL out of range */
static GtkTreeListRow* row_at(GitStashView* v, guint position) {
    return g_list_model_get_item(G_LIST_MODEL(v->selection), position);
}

/* The item of row (new reference) */
static GitStashItem* item_of(GtkTreeListRow* row) {
    return row != NULL ? gtk_tree_list_row_get_item(row) : NULL;
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css("#git-stash-tags label.git-name { color: @fg_strong; font-family: @font_code; }"
                  "#git-stash-tags label.git-dim { color: @fg_subtle; }"
                  "#git-stash-tags label.git-hash { color: @fg_subtle; font-family: @font_code; }"
                  "#git-stash-tags label.git-group { color: @fg_strong; font-weight: 600; }"
                  "#git-stash-tags > row { padding: 1px 10px 1px 4px; }"
                  /* The look of the History list (columnview.data-table, theme.c) */
                  "#git-stash-tags { background-color: @bg_view; color: @fg; }"
                  "#git-stash-tags > row:hover { background-color: @bg_hover; }"
                  "#git-stash-tags > row:selected { background-color: @bg_selected; color: @fg_bright; }"
                  "#git-stash-tags > row:selected:backdrop { background-color: @bg_selected_inactive; }"
                  "#git-stash-tags > row:selected label { color: @fg_bright; }");
}

/* Age of time, as in the History ("3 days ago") */
static gchar* relative_date(gint64 time) {
    return time > 0 ? git_relative_date(g_get_real_time() / G_USEC_PER_SEC - time) : g_strdup("");
}

/* time in full, in the local time zone ("2026-11-21 08:19") */
static gchar* exact_date(gint64 time) {
    GDateTime* t = time > 0 ? g_date_time_new_from_unix_local(time) : NULL;
    if (t == NULL) return g_strdup("");
    gchar* text = g_date_time_format(t, "%Y-%m-%d %H:%M");
    g_date_time_unref(t);
    return text != NULL ? text : g_strdup("");
}

/* --- Reading the lists */

typedef struct {
    gchar* root;
    GPtrArray* stashes; /* GitStashInfo*, NULL: not readable */
    GPtrArray* tags;    /* GitTagInfo*, NULL: not readable */
} ReadTask;

static void read_task_free(gpointer data) {
    ReadTask* td = data;
    g_free(td->root);
    if (td->stashes != NULL) g_ptr_array_unref(td->stashes);
    if (td->tags != NULL) g_ptr_array_unref(td->tags);
    g_free(td);
}

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    ReadTask* td = data;
    td->stashes = git_read_stashes(td->root);
    td->tags = git_read_tags(td->root);
    if (g_task_return_error_if_cancelled(task)) return;
    g_task_return_boolean(task, TRUE);
}

typedef struct {
    gchar* root;
    gchar* hash;           /* a stash: its commit and untracked files */
    gchar* untracked_hash;
    gchar* tag;            /* a tag: its name */
    gchar* text;
} ShowTask;

static void show_task_free(gpointer data) {
    ShowTask* td = data;
    g_free(td->root);
    g_free(td->hash);
    g_free(td->untracked_hash);
    g_free(td->tag);
    g_free(td->text);
    g_free(td);
}

static void show_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    ShowTask* td = data;
    gchar* text = td->tag != NULL ? git_show_tag(td->root, td->tag) : git_show_stash(td->root, td->hash, td->untracked_hash);
    if (g_task_return_error_if_cancelled(task)) {
        g_free(text);
        return;
    }
    td->text = text != NULL ? git_diff_text_cut(text, GIT_DIFF_MAX_LINES) : NULL;
    g_task_return_boolean(task, TRUE);
}

static void cancel_show(GitStashView* v) {
    if (v->show_cancellable != NULL) {
        g_cancellable_cancel(v->show_cancellable);
        g_clear_object(&v->show_cancellable);
    }
}

static void cancel_reads(GitStashView* v) {
    cancel_show(v);
    if (v->cancellable != NULL) {
        g_cancellable_cancel(v->cancellable);
        g_clear_object(&v->cancellable);
    }
}

/* The tasks reach their GitStashView through their callback data: they are
 * cancelled before it is freed, and a cancelled task's callback returns
 * before touching it */
static void on_show_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: another entry, a new read or exit */
    GitStashView* v = data;
    ShowTask* td = g_task_get_task_data(task);
    if (td->text == NULL) {
        gtk_text_buffer_set_text(v->detail_buf, _("This entry cannot be read."), -1);
    } else {
        git_diff_text_show(v->detail_buf, td->text);
    }
}

/* Above the content: the name, the message and the dates of item */
static void show_header(GitStashView* v, const GitStashItem* item) {
    const gchar* message = item->stash != NULL ? item->stash->message : item->tag->subject;
    gint64 time = item->stash != NULL ? item->stash->time : item->tag->time;
    gchar* ago = relative_date(time);
    gchar* when = exact_date(time);
    /* TRANSLATORS: above the content of a stash or a tag: its name, its
     * message, the relative date ("2 days ago") and the full date
     * ("2026-11-21 08:19") */
    gchar* text = g_strdup_printf(_("%s  •  %s  (%s, %s)"), item_name(item), message, ago, when);
    gtk_label_set_text(GTK_LABEL(v->header_label), text);
    g_free(text);
    g_free(when);
    g_free(ago);
}

static void on_selection_changed(GtkSingleSelection* sel, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    GitStashView* v = data;
    GitStashItem* item = item_of(gtk_single_selection_get_selected_item(sel));
    cancel_show(v);
    if (item == NULL || v->root == NULL) {
        g_clear_object(&item);
        return;
    }
    if (is_group(item)) {
        /* A group: its title, and what Enter does on it */
        gtk_label_set_text(GTK_LABEL(v->header_label), group_title(item->group));
        gtk_text_buffer_set_text(v->detail_buf, _("Enter or a click folds or unfolds the group."), -1);
        g_object_unref(item);
        return;
    }
    g_free(v->selected_name);
    v->selected_name = g_strdup(item_name(item));
    v->selected_group = item->group;
    show_header(v, item);

    ShowTask* td = g_new0(ShowTask, 1);
    td->root = g_strdup(v->root);
    if (item->stash != NULL) {
        td->hash = g_strdup(item->stash->hash);
        td->untracked_hash = g_strdup(item->stash->untracked_hash);
    } else {
        td->tag = g_strdup(item->tag->name);
    }
    v->show_cancellable = g_cancellable_new();
    GTask* task = g_task_new(NULL, v->show_cancellable, on_show_ready, v);
    g_task_set_task_data(task, td, show_task_free);
    g_task_run_in_thread(task, show_thread);
    g_object_unref(task);
    g_object_unref(item);
}

/* Above the list, only when the list does not say it; text NULL hides the
 * line */
static void show_summary(GitStashView* v, const gchar* text) {
    gtk_label_set_text(GTK_LABEL(v->summary_label), text != NULL ? text : "");
    gtk_widget_set_visible(v->summary_label, text != NULL);
}

/* The row to select after a read: the entry selected before if still shown,
 * else the first entry shown, else the first group */
static guint row_to_select(GitStashView* v) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(v->selection));
    guint first_entry = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; i < n; i++) {
        GtkTreeListRow* row = row_at(v, i);
        GitStashItem* item = item_of(row);
        gboolean entry = !is_group(item);
        gboolean same = entry && item->group == v->selected_group && g_strcmp0(item_name(item), v->selected_name) == 0;
        g_object_unref(item);
        g_object_unref(row);
        if (same) return i;
        if (entry && first_entry == GTK_INVALID_LIST_POSITION) first_entry = i;
    }
    return first_entry != GTK_INVALID_LIST_POSITION ? first_entry : n > 0 ? 0 : GTK_INVALID_LIST_POSITION;
}

/* Unfold or fold each group as it was */
static void restore_folding(GitStashView* v) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(v->groups));
    for (guint i = 0; i < n; i++) {
        GtkTreeListRow* row = gtk_tree_list_model_get_child_row(v->tree, i);
        GitStashItem* item = item_of(row);
        if (row != NULL && item != NULL) gtk_tree_list_row_set_expanded(row, v->unfolded[item->group]);
        g_clear_object(&item);
        g_clear_object(&row);
    }
}

/* A group holding the entries of list (taken over), NULL when list is empty */
static GitStashItem* group_of(StashGroup group, GPtrArray* list) {
    if (list == NULL || list->len == 0) return NULL;
    GitStashItem* g = group_new(group);
    g_ptr_array_set_free_func(list, NULL); /* the items take them over */
    for (guint i = 0; i < list->len; i++) {
        GitStashItem* entry = g_object_new(GIT_TYPE_STASH_ITEM, NULL);
        entry->group = group;
        if (group == GROUP_STASHES) {
            entry->stash = g_ptr_array_index(list, i);
        } else {
            entry->tag = g_ptr_array_index(list, i);
        }
        g_list_store_append(g->children, entry);
        g_object_unref(entry);
    }
    g_ptr_array_set_size(list, 0);
    return g;
}

static void on_read_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: a newer read, or exit */
    GitStashView* v = data;
    ReadTask* td = g_task_get_task_data(task);

    gboolean readable = td->stashes != NULL && td->tags != NULL;
    gboolean none = readable && td->stashes->len == 0 && td->tags->len == 0;
    GPtrArray* items = g_ptr_array_new_with_free_func(g_object_unref);
    GitStashItem* stashes = group_of(GROUP_STASHES, td->stashes);
    GitStashItem* tags = group_of(GROUP_TAGS, td->tags);
    if (stashes != NULL) g_ptr_array_add(items, stashes);
    if (tags != NULL) g_ptr_array_add(items, tags);

    g_signal_handlers_block_by_func(v->selection, (gpointer)on_selection_changed, v);
    g_list_store_splice(v->groups, 0, g_list_model_get_n_items(G_LIST_MODEL(v->groups)), items->pdata, items->len);
    restore_folding(v);
    gtk_single_selection_set_selected(v->selection, GTK_INVALID_LIST_POSITION);
    g_signal_handlers_unblock_by_func(v->selection, (gpointer)on_selection_changed, v);
    g_ptr_array_unref(items);

    show_summary(v, !readable ? _("The stashes and tags of this repository cannot be read.")
                    : none    ? _("No stash and no tag in this repository")
                              : NULL);
    guint row = row_to_select(v);
    if (row != GTK_INVALID_LIST_POSITION) {
        gtk_single_selection_set_selected(v->selection, row); /* shows its content */
    } else {
        g_clear_pointer(&v->selected_name, g_free);
        gtk_label_set_text(GTK_LABEL(v->header_label), "");
        gtk_text_buffer_set_text(v->detail_buf, "", -1);
    }
}

static void start_read(GitStashView* v) {
    cancel_reads(v);
    v->cancellable = g_cancellable_new();
    ReadTask* td = g_new0(ReadTask, 1);
    td->root = g_strdup(v->root);
    if (g_list_model_get_n_items(G_LIST_MODEL(v->groups)) == 0) show_summary(v, _("Reading the stashes and tags…"));
    GTask* task = g_task_new(NULL, v->cancellable, on_read_ready, v);
    g_task_set_task_data(task, td, read_task_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
}

void git_stash_view_set_root(GitStashView* v, const gchar* root) {
    if (v == NULL) return;
    if (g_strcmp0(v->root, root) != 0) {
        cancel_reads(v);
        g_free(v->root);
        v->root = g_strdup(root);
        g_clear_pointer(&v->selected_name, g_free);
        g_list_store_remove_all(v->groups);
        gtk_label_set_text(GTK_LABEL(v->header_label), "");
        gtk_text_buffer_set_text(v->detail_buf, "", -1);
    }
    /* Hidden: read when it comes on screen */
    if (v->root != NULL && gtk_widget_get_mapped(v->widget)) start_read(v);
}

/* The view comes on screen: a stash or a tag may have been made in a
 * terminal, read again */
static void on_view_map(GtkWidget* widget, gpointer data) {
    (void)widget;
    GitStashView* v = data;
    if (v->root != NULL) start_read(v);
}

/* --- Folding */

/* Fold or unfold the group of row (unfold: TRUE, FALSE; -1: the other way) */
static void fold(GtkTreeListRow* row, gint unfold) {
    if (row == NULL || !gtk_tree_list_row_is_expandable(row)) return;
    gtk_tree_list_row_set_expanded(row, unfold < 0 ? !gtk_tree_list_row_get_expanded(row) : unfold);
}

/* Enter or a double click on a group: fold or unfold it */
static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    GtkTreeListRow* row = row_at(data, position);
    fold(row, -1);
    g_clear_object(&row);
}

/* A group folded or unfolded (arrow, click, keys): kept for the next reads */
static void on_row_expanded(GtkTreeListRow* row, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    GitStashView* v = data;
    GitStashItem* item = item_of(row);
    if (item != NULL && is_group(item)) v->unfolded[item->group] = gtk_tree_list_row_get_expanded(row);
    g_clear_object(&item);
}

/* A click on a group row folds or unfolds it (the press is the row's: a
 * double click does not fold it back) */
static void on_group_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)x;
    (void)y;
    (void)data;
    GtkWidget* box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkListItem* li = g_object_get_data(G_OBJECT(box), GIT_STASH_ITEM_KEY);
    GtkTreeListRow* row = li != NULL ? gtk_list_item_get_item(li) : NULL;
    if (row == NULL || !gtk_tree_list_row_is_expandable(row)) return; /* an entry: the list selects it */
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (n_press == 1) fold(row, -1);
}

/* --- Copying */

typedef enum { COPY_NAME, COPY_HASH, COPY_MESSAGE } CopyKind;

/* Copy a field of item to the clipboard, with a notice */
static void copy_item(AppData* app, const GitStashItem* item, CopyKind kind) {
    if (item == NULL || is_group(item)) return;
    const gchar* hash = item->stash != NULL ? item->stash->hash : item->tag->hash;
    const gchar* message = item->stash != NULL ? item->stash->message : item->tag->message;
    const gchar* text = kind == COPY_NAME ? item_name(item) : kind == COPY_HASH ? hash : message;
    if (text == NULL || text[0] == '\0') return;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(app->window)), text);
    gchar* msg = kind == COPY_MESSAGE
                     /* TRANSLATORS: %s is the name of a stash or a tag */
                     ? g_strdup_printf(_("Copied the message of %s"), item_name(item))
                     : g_strdup_printf(_("Copied “%s”"), text);
    app_notice(app, msg, FALSE);
    g_free(msg);
}

/* The key that copies the name in the current mode (shown in the menu) */
static const gchar* copy_key(AppData* app) {
    switch (app_nav_style(app)) {
    case NAV_STYLE_VIM: return "y";
    case NAV_STYLE_EMACS: return "M-w";
    default: return "Ctrl+C";
    }
}

/* The selected entry (new reference), NULL for none or a group */
static GitStashItem* selected_entry(GitStashView* v) {
    GitStashItem* item = item_of(gtk_single_selection_get_selected_item(v->selection));
    if (item != NULL && is_group(item)) g_clear_object(&item);
    return item;
}

/* In the list, after the navigation keys (focus_route_attach_list()), which
 * leave these to it: Left folds the group (on an entry: goes to its group),
 * Right unfolds it (h / l in Vim mode, C-b / C-f in Emacs mode); Ctrl+C
 * (y, M-w) copies the name of the entry */
static gboolean on_list_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)ctrl;
    (void)keycode;
    GitStashView* v = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    guint key = gdk_keyval_to_lower(keyval);
    gint dir = 0; /* -1: fold, 1: unfold */
    gboolean copy = FALSE;
    if (mods == 0 && (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)) dir = -1;
    if (mods == 0 && (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)) dir = 1;
    switch (app_nav_style(v->app)) {
    case NAV_STYLE_VIM:
        if (mods == 0 && keyval == GDK_KEY_h) dir = -1;
        if (mods == 0 && keyval == GDK_KEY_l) dir = 1;
        copy = mods == 0 && keyval == GDK_KEY_y;
        break;
    case NAV_STYLE_EMACS:
        if (mods == GDK_CONTROL_MASK && key == GDK_KEY_b) dir = -1;
        if (mods == GDK_CONTROL_MASK && key == GDK_KEY_f) dir = 1;
        copy = mods == GDK_ALT_MASK && key == GDK_KEY_w;
        break;
    default: copy = mods == GDK_CONTROL_MASK && key == GDK_KEY_c; break;
    }
    if (copy) {
        GitStashItem* item = selected_entry(v);
        copy_item(v->app, item, COPY_NAME);
        g_clear_object(&item);
        return TRUE;
    }
    if (dir == 0) return FALSE;
    GtkTreeListRow* row = gtk_single_selection_get_selected_item(v->selection);
    if (row == NULL) return TRUE;
    if (gtk_tree_list_row_is_expandable(row)) {
        fold(row, dir > 0);
    } else if (dir < 0) {
        GtkTreeListRow* parent = gtk_tree_list_row_get_parent(row); /* the entry's group */
        if (parent != NULL) {
            gtk_single_selection_set_selected(v->selection, gtk_tree_list_row_get_position(parent));
            g_object_unref(parent);
        }
    }
    return TRUE;
}

static void on_copy_clicked(GtkButton* button, gpointer data) {
    GtkWidget* popover = data;
    AppData* app = g_object_get_data(G_OBJECT(popover), "app");
    GitStashItem* item = g_object_get_data(G_OBJECT(popover), GIT_STASH_ITEM_KEY);
    copy_item(app, item, (CopyKind)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "copy-kind")));
    gtk_popover_popdown(GTK_POPOVER(popover));
}

static void menu_item(GtkWidget* box, GtkWidget* popover, const gchar* text, const gchar* hint, CopyKind kind) {
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    if (hint != NULL) {
        GtkWidget* key = gtk_label_new(hint);
        gtk_widget_add_css_class(key, "dim-label");
        gtk_box_append(GTK_BOX(row), key);
    }
    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_object_set_data(G_OBJECT(btn), "copy-kind", GINT_TO_POINTER(kind));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_copy_clicked), popover);
    gtk_box_append(GTK_BOX(box), btn);
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

/* A right click on an entry: select it, then its menu */
static void on_row_menu(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    GitStashView* v = data;
    GtkWidget* box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkListItem* li = g_object_get_data(G_OBJECT(box), GIT_STASH_ITEM_KEY);
    GtkTreeListRow* row = li != NULL ? gtk_list_item_get_item(li) : NULL;
    GtkWidget* list = gtk_widget_get_ancestor(box, GTK_TYPE_LIST_VIEW);
    GitStashItem* item = item_of(row);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (item == NULL || is_group(item) || list == NULL) {
        g_clear_object(&item);
        return;
    }
    gtk_single_selection_set_selected(v->selection, gtk_list_item_get_position(li));
    gtk_widget_grab_focus(list);

    graphene_point_t at;
    if (!gtk_widget_compute_point(box, list, &GRAPHENE_POINT_INIT((float)x, (float)y), &at)) {
        at = GRAPHENE_POINT_INIT(0.0f, 0.0f);
    }
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, list);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = { (gint)at.x, (gint)at.y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(popover), "app", v->app);
    g_object_set_data_full(G_OBJECT(popover), GIT_STASH_ITEM_KEY, item, g_object_unref);

    GtkWidget* menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    menu_item(menu, popover, _("Copy Name"), copy_key(v->app), COPY_NAME);
    menu_item(menu, popover, _("Copy Hash"), NULL, COPY_HASH);
    menu_item(menu, popover, _("Copy Message"), NULL, COPY_MESSAGE);
    gtk_popover_set_child(GTK_POPOVER(popover), menu);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

/* --- Rows: name, message, short hash (a tag), date */

static GtkWidget* row_label(GtkWidget* box, const gchar* css_class) {
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    if (css_class != NULL) gtk_widget_add_css_class(label, css_class);
    gtk_box_append(GTK_BOX(box), label);
    return label;
}

static void on_row_setup(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_hexpand(box, TRUE); /* the whole row takes the click, even a group's */
    g_object_set_data(G_OBJECT(box), GIT_STASH_ITEM_KEY, li);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    /* Before the row's own click, which would only select the group */
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_group_pressed), NULL);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));
    GtkGesture* menu = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(menu), GDK_BUTTON_SECONDARY);
    g_signal_connect(menu, "pressed", G_CALLBACK(on_row_menu), data);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(menu));
    row_label(box, "git-name");
    GtkWidget* message = row_label(box, NULL);
    gtk_label_set_ellipsize(GTK_LABEL(message), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(message, TRUE);
    row_label(box, "git-hash");
    row_label(box, "git-dim");
    GtkWidget* expander = gtk_tree_expander_new();
    gtk_widget_set_hexpand(expander, TRUE);
    gtk_tree_expander_set_indent_for_icon(GTK_TREE_EXPANDER(expander), TRUE);
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    gtk_list_item_set_child(li, expander);
}

static void on_row_bind(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    GtkTreeListRow* row = gtk_list_item_get_item(li);
    GtkWidget* expander = gtk_list_item_get_child(li);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);
    GitStashItem* item = item_of(row);
    GtkWidget* name = gtk_widget_get_first_child(gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander)));
    GtkWidget* message = gtk_widget_get_next_sibling(name);
    GtkWidget* hash = gtk_widget_get_next_sibling(message);
    GtkWidget* date = gtk_widget_get_next_sibling(hash);
    g_object_unref(item); /* the row keeps it */

    gboolean group = is_group(item);
    gtk_widget_set_visible(message, !group);
    gtk_widget_set_visible(date, !group);
    gtk_widget_set_visible(hash, item->tag != NULL && item->tag->short_hash != NULL);
    if (group) {
        /* A group: its title and how many entries, folded or unfolded */
        gtk_widget_remove_css_class(name, "git-name");
        gtk_widget_add_css_class(name, "git-group");
        gtk_widget_set_tooltip_text(name, _("Click or Enter: fold or unfold"));
        gchar* text = g_strdup_printf("%s (%u)", group_title(item->group),
                                      g_list_model_get_n_items(G_LIST_MODEL(item->children)));
        gtk_label_set_text(GTK_LABEL(name), text);
        g_free(text);
        g_signal_connect(row, "notify::expanded", G_CALLBACK(on_row_expanded), data);
        return;
    }
    gtk_widget_remove_css_class(name, "git-group");
    gtk_widget_add_css_class(name, "git-name");
    gtk_label_set_text(GTK_LABEL(name), item_name(item));
    if (item->stash != NULL) {
        gtk_widget_set_tooltip_text(name, NULL);
        gtk_label_set_text(GTK_LABEL(message), item->stash->message);
        gtk_widget_set_tooltip_text(message, item->stash->message);
    } else {
        gtk_widget_set_tooltip_text(name, item->tag->annotated ? _("Annotated tag") : _("Lightweight tag"));
        gtk_label_set_text(GTK_LABEL(message), item->tag->subject);
        gtk_widget_set_tooltip_text(message, item->tag->message);
        gtk_label_set_text(GTK_LABEL(hash), item->tag->short_hash != NULL ? item->tag->short_hash : "");
    }
    gint64 time = item->stash != NULL ? item->stash->time : item->tag->time;
    gchar* ago = relative_date(time);
    gchar* when = exact_date(time);
    gtk_label_set_text(GTK_LABEL(date), ago);
    gtk_widget_set_tooltip_text(date, when);
    g_free(when);
    g_free(ago);
}

static void on_row_unbind(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    GtkTreeListRow* row = gtk_list_item_get_item(li);
    if (row != NULL) g_signal_handlers_disconnect_by_func(row, (gpointer)on_row_expanded, data);
    gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(gtk_list_item_get_child(li)), NULL);
}

void git_stash_view_cancel(GitStashView* v) {
    if (v != NULL) cancel_reads(v);
}

/* With the view's widget */
static void git_stash_view_free(gpointer data) {
    GitStashView* v = data;
    cancel_reads(v);
    g_clear_object(&v->groups);
    g_clear_object(&v->detail_buf);
    g_free(v->root);
    g_free(v->selected_name);
    g_free(v);
}

GitStashView* git_stash_view_new(AppData* app) {
    ensure_css();
    GitStashView* v = g_new0(GitStashView, 1);
    v->app = app;

    for (gint g = 0; g < GROUP_COUNT; g++) v->unfolded[g] = TRUE;
    v->groups = g_list_store_new(GIT_TYPE_STASH_ITEM);
    v->tree = gtk_tree_list_model_new(G_LIST_MODEL(g_object_ref(v->groups)), FALSE, FALSE, children_of, NULL, NULL);
    v->selection = gtk_single_selection_new(G_LIST_MODEL(v->tree));
    gtk_single_selection_set_autoselect(v->selection, FALSE);
    gtk_single_selection_set_can_unselect(v->selection, TRUE);
    g_signal_connect(v->selection, "notify::selected-item", G_CALLBACK(on_selection_changed), v);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_row_setup), v);
    g_signal_connect(factory, "bind", G_CALLBACK(on_row_bind), v);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_row_unbind), v);
    GtkWidget* list = gtk_list_view_new(GTK_SELECTION_MODEL(v->selection), factory);
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_list_key), v);
    gtk_widget_add_controller(list, keys);
    gtk_widget_set_name(list, "git-stash-tags");
    gtk_widget_set_tooltip_text(list, _("Enter: fold or unfold the group; right click: copy"));
    g_signal_connect(list, "activate", G_CALLBACK(on_activate), v);
    focus_route_attach_list(app, list, v->selection);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_widget_set_size_request(scroll, -1, 120);

    v->summary_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(v->summary_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(v->summary_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(v->summary_label, "cell-muted");
    gtk_widget_set_margin_start(v->summary_label, 12);
    gtk_widget_set_margin_top(v->summary_label, 6);
    gtk_widget_set_margin_bottom(v->summary_label, 6);
    gtk_widget_set_visible(v->summary_label, FALSE);

    GtkWidget* list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(list_box), v->summary_label);
    gtk_box_append(GTK_BOX(list_box), scroll);

    /* Below: the entry and its content */
    GtkWidget* header_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(header_bar, "git-header-bar");
    v->header_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(v->header_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(v->header_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(v->header_label, TRUE);
    gtk_box_append(GTK_BOX(header_bar), v->header_label);

    v->detail_buf = gtk_text_buffer_new(NULL);
    GtkWidget* detail_view = gtk_text_view_new_with_buffer(v->detail_buf);
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

    v->widget = paned;
    g_object_set_data_full(G_OBJECT(paned), GIT_STASH_VIEW_KEY, v, git_stash_view_free);
    g_signal_connect(paned, "map", G_CALLBACK(on_view_map), v);
    return v;
}

GtkWidget* git_stash_view_widget(GitStashView* v) {
    return v != NULL ? v->widget : NULL;
}
