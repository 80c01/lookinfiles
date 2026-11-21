#include "git/git_history.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "widgets/panels.h"
#include "theme/text_style.h"
#include "git/git_diff_text.h"
#include "git/git_refs.h"
#include "git/git_utils.h"
#include "widgets/focus_route.h"

#define GIT_ROW_MAX_REFS 6
/* Commits read at a time (the newest, or the newest that match the filter);
 * reaching the end of the list reads as many more */
#define GIT_LOG_PAGE 100
/* Rows from the end of the list that read the next page when scrolled to */
#define GIT_MORE_MARGIN_ROWS 10
/* Data of a commit cell: its GtkListItem */
#define GIT_ITEM_KEY "lookin-git-item"
/* Data of the view's widget: the GitHistory, freed with it */
#define GIT_HISTORY_KEY "lookin-git-history"

struct GitHistory {
    AppData* app;
    GtkWidget* widget;          /* the view (owns this struct) */
    GListStore* store;          /* commit list (GitCommit items) */
    GtkSingleSelection* selection; /* selection over store */
    GtkWidget* filter;          /* search entry above the list */
    GtkWidget* count_label;     /* how many commits are listed, beside the filter */
    gchar** words;              /* filter words (git_log_words()), NULL: every commit */
    GtkWidget* header_refs_box; /* branch/tag pills of the selected commit */
    GtkWidget* header_label;    /* summary of the selected commit */
    GtkTextBuffer* detail_buf;  /* diff of the selected commit */
    gchar* current_root;        /* repository shown, or NULL */
    gchar* selected_hash;       /* selected commit, or NULL */
    GCancellable* cancellable;  /* log and show tasks of current_root */
    GCancellable* show_cancellable; /* diff of the selected commit being read */
    gboolean log_stale;         /* current_root's history is read when the view is shown */
    gboolean reading;           /* a read of the whole list is running */
    gboolean reading_more;      /* a read of the next page is running */
    gboolean more;              /* the history goes on after the last commit listed */
    guint wanted;               /* commits the next whole read lists (pages loaded) */
};

/* One row of the commit list (item of GitHistory.store) */
#define GIT_TYPE_COMMIT (git_commit_get_type())
G_DECLARE_FINAL_TYPE(GitCommit, git_commit, GIT, COMMIT, GObject)

struct _GitCommit {
    GObject parent_instance;
    GitCommitInfo* info; /* owned */
    GPtrArray* refs;     /* owned array of GitRefInfo*, or NULL */
};

G_DEFINE_FINAL_TYPE(GitCommit, git_commit, G_TYPE_OBJECT)

static void git_commit_finalize(GObject* object) {
    GitCommit* self = GIT_COMMIT(object);
    git_commit_info_free(self->info);
    g_clear_pointer(&self->refs, g_ptr_array_unref);
    G_OBJECT_CLASS(git_commit_parent_class)->finalize(object);
}

static void git_commit_class_init(GitCommitClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = git_commit_finalize;
}

static void git_commit_init(GitCommit* self) {
    (void)self;
}

/* Takes ownership of info and refs (refs may be NULL) */
static GitCommit* git_commit_new(GitCommitInfo* info, GPtrArray* refs) {
    GitCommit* self = g_object_new(GIT_TYPE_COMMIT, NULL);
    self->info = info;
    self->refs = refs;
    return self;
}

/* Commit at position i of the list, NULL if out of range (borrowed) */
static GitCommitInfo* git_commit_at(GitHistory* h, guint i) {
    GitCommit* c = g_list_model_get_item(G_LIST_MODEL(h->store), i);
    if (c == NULL) return NULL;
    GitCommitInfo* info = c->info;
    g_object_unref(c); /* the store keeps it alive */
    return info;
}

/* The tasks reach their GitHistory through their callback data: a task is
 * cancelled before its view is freed, and a cancelled task's callback
 * returns before touching it (g_task_propagate_boolean() fails). */
typedef struct {
    gchar* git_root;
    gchar** words;       /* filter, NULL: every commit */
    guint skip;          /* commits before the first one read (a next page) */
    guint max;           /* commits read */
    gboolean append;     /* a next page: added to the list, not replacing it */
    gboolean more;       /* the history goes on after the commits read */
    gboolean failed;     /* the repository could not be read */
    GPtrArray* commits;  /* array of GitCommitInfo* */
    GHashTable* refs_map; /* sha -> GPtrArray* of GitRefInfo* */
} GitLogTaskData;

static void git_log_task_data_free(gpointer data) {
    GitLogTaskData* td = (GitLogTaskData*)data;
    if (td == NULL) return;
    g_free(td->git_root);
    g_strfreev(td->words);
    if (td->commits != NULL) {
        g_ptr_array_unref(td->commits);
    }
    if (td->refs_map != NULL) {
        g_hash_table_unref(td->refs_map);
    }
    g_free(td);
}

static void git_log_thread_func(GTask* task, gpointer source_object,
                                gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    GitLogTaskData* td = (GitLogTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) return;

    GPtrArray* commits = git_read_log(td->git_root, td->skip, td->max, td->words, cancellable, &td->more);
    GHashTable* refs_map = NULL;
    if (!g_cancellable_is_cancelled(cancellable) && commits != NULL && commits->len > 0) {
        refs_map = git_read_refs(td->git_root);
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        if (commits != NULL) g_ptr_array_unref(commits);
        if (refs_map != NULL) g_hash_table_unref(refs_map);
        g_task_return_error_if_cancelled(task);
        return;
    }

    td->failed = commits == NULL;
    td->commits = commits != NULL ? commits : g_ptr_array_new_with_free_func(git_commit_info_free);
    td->refs_map = refs_map;
    g_task_return_boolean(task, TRUE);
}

typedef struct {
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

    gchar* text = git_show_commit(td->git_root, td->commit_hash);

    if (g_cancellable_is_cancelled(cancellable)) {
        g_free(text);
        g_task_return_error_if_cancelled(task);
        return;
    }

    td->diff_output = text != NULL ? git_diff_text_cut(text, GIT_DIFF_MAX_LINES)
                                   : g_strdup(_("(No diff available or git error)"));
    g_task_return_boolean(task, TRUE);
}

static void on_git_show_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    GTask* task = G_TASK(res);
    GitShowTaskData* td = (GitShowTaskData*)g_task_get_task_data(task);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: another commit, repository or exit */
    GitHistory* h = user_data;
    if (td->commit_hash != NULL && g_strcmp0(td->commit_hash, h->selected_hash) == 0) {
        git_diff_text_show(h->detail_buf, td->diff_output);
    }
}

static const gchar* ref_css_class(GitRefKind kind) {
    switch (kind) {
    case GIT_REF_HEAD:   return "git-ref-head";
    case GIT_REF_BRANCH: return "git-ref-branch";
    case GIT_REF_TAG:    return "git-ref-tag";
    case GIT_REF_REMOTE: return "git-ref-remote";
    default:             return "git-ref-branch";
    }
}

static gchar* ref_tooltip(const GitRefInfo* ref) {
    if (ref == NULL || ref->name == NULL) return NULL;
    switch (ref->kind) {
    case GIT_REF_HEAD:
        if (ref->detached) return g_strdup(_("Detached HEAD"));
        return g_strdup_printf(_("Current branch: %s"), ref->name);
    case GIT_REF_BRANCH:
        return g_strdup_printf(_("Branch: %s"), ref->name);
    case GIT_REF_TAG:
        return g_strdup_printf(_("Tag: %s"), ref->name);
    case GIT_REF_REMOTE:
        return g_strdup_printf(_("Remote branch: %s"), ref->name);
    default:
        return g_strdup(ref->name);
    }
}

/* Fill box with compact branch/tag pills (max_badges == 0 shows all) */
static void populate_refs_box(GtkWidget* box, GPtrArray* refs, guint max_badges) {
    if (box == NULL) return;
    GtkWidget* ch;
    while ((ch = gtk_widget_get_first_child(box)) != NULL) {
        gtk_box_remove(GTK_BOX(box), ch);
    }
    if (refs == NULL || refs->len == 0) {
        gtk_widget_set_visible(box, FALSE);
        return;
    }
    gtk_widget_set_visible(box, TRUE);
    guint limit = (max_badges > 0 && refs->len > max_badges) ? max_badges : refs->len;
    for (guint i = 0; i < limit; i++) {
        const GitRefInfo* ref = g_ptr_array_index(refs, i);
        gchar* text = git_ref_display_name(ref);
        GtkWidget* pill = gtk_label_new(text);
        g_free(text);
        gtk_widget_add_css_class(pill, "git-ref");
        gtk_widget_add_css_class(pill, ref_css_class(ref->kind));
        gtk_widget_set_valign(pill, GTK_ALIGN_CENTER);
        gchar* tip = ref_tooltip(ref);
        if (tip != NULL) {
            gtk_widget_set_tooltip_text(pill, tip);
            g_free(tip);
        }
        gtk_box_append(GTK_BOX(box), pill);
    }
    if (refs->len > limit) {
        guint extra = refs->len - limit;
        gchar* more = g_strdup_printf("+%u", extra);
        GtkWidget* pill = gtk_label_new(more);
        g_free(more);
        gtk_widget_add_css_class(pill, "git-ref");
        gtk_widget_add_css_class(pill, "git-ref-more");
        gtk_widget_set_valign(pill, GTK_ALIGN_CENTER);
        GString* tip = g_string_new(NULL);
        for (guint i = limit; i < refs->len; i++) {
            const GitRefInfo* ref = g_ptr_array_index(refs, i);
            gchar* d = git_ref_display_name(ref);
            if (tip->len > 0) g_string_append_c(tip, '\n');
            g_string_append(tip, d);
            g_free(d);
        }
        gtk_widget_set_tooltip_text(pill, tip->str);
        g_string_free(tip, TRUE);
        gtk_box_append(GTK_BOX(box), pill);
    }
}

static void read_more(GitHistory* h);

/* Cancel the diff being read for the selected commit, if any */
static void cancel_show(GitHistory* h) {
    if (h->show_cancellable != NULL) {
        g_cancellable_cancel(h->show_cancellable);
        g_clear_object(&h->show_cancellable);
    }
}

/* Cancel every read of the view: the history and the diff */
static void cancel_reads(GitHistory* h) {
    cancel_show(h);
    if (h->cancellable != NULL) {
        g_cancellable_cancel(h->cancellable);
        g_clear_object(&h->cancellable);
    }
}

/* The date of a commit in full, in the local time zone ("2026-11-21 08:19") */
static gchar* exact_date(const GitCommitInfo* ci) {
    GDateTime* t = g_date_time_new_from_unix_local(ci->time);
    if (t == NULL) return g_strdup("");
    gchar* text = g_date_time_format(t, "%Y-%m-%d %H:%M");
    g_date_time_unref(t);
    return text != NULL ? text : g_strdup("");
}

static void on_git_selection_changed(GtkSingleSelection* selection, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    GitHistory* h = user_data;
    if (h->current_root == NULL) return;

    GitCommit* item = gtk_single_selection_get_selected_item(selection);
    if (item == NULL || item->info == NULL || item->info->hash == NULL) {
        return;
    }
    const gchar* hash = item->info->hash;
    const gchar* short_hash = item->info->short_hash;
    const gchar* subject = item->info->subject;
    const gchar* author = item->info->author;
    const gchar* date = item->info->date;

    g_free(h->selected_hash);
    h->selected_hash = g_strdup(hash);
    /* The keyboard reached the last commit listed: the next page */
    if (gtk_single_selection_get_selected(selection) + 1 >= g_list_model_get_n_items(G_LIST_MODEL(h->store))) {
        read_more(h);
    }

    populate_refs_box(h->header_refs_box, item->refs, 0);
    gchar* when = exact_date(item->info);
    /* TRANSLATORS: above the diff of a commit: short hash, subject, author,
     * relative date ("2 days ago") and full date ("2026-11-21 08:19"), in
     * this order */
    gchar* header_text = g_strdup_printf(_("[%s] %s  •  %s  (%s, %s)"),
                                        short_hash ? short_hash : "",
                                        subject ? subject : "",
                                        author ? author : "",
                                        date ? date : "", when);
    g_free(when);
    gtk_label_set_text(GTK_LABEL(h->header_label), header_text);
    g_free(header_text);

    GitShowTaskData* td = g_new0(GitShowTaskData, 1);
    td->git_root = g_strdup(h->current_root);
    td->commit_hash = g_strdup(hash);

    /* Moving fast in the list: the diff of the previous commit is not wanted */
    cancel_show(h);
    h->show_cancellable = g_cancellable_new();
    GTask* task = g_task_new(NULL, h->show_cancellable, on_git_show_ready, h);
    g_task_set_task_data(task, td, git_show_task_data_free);
    g_task_run_in_thread(task, git_show_thread_func);
    g_object_unref(task);
}

/* Beside the filter: how many commits are listed (n, more when the history
 * goes on after them) */
static void show_count(GitHistory* h, guint n, gboolean more) {
    gchar* text;
    if (h->words != NULL) {
        if (n == 0) {
            text = g_strdup(_("No commit matches"));
        } else if (more) {
            text = g_strdup_printf(_("The first %u matching commits"), n);
        } else {
            text = g_strdup_printf(ngettext("%u matching commit", "%u matching commits", n), n);
        }
    } else if (more) {
        text = g_strdup_printf(_("The last %u commits"), n);
    } else {
        text = g_strdup_printf(ngettext("%u commit", "%u commits", n), n);
    }
    gtk_label_set_text(GTK_LABEL(h->count_label), text);
    g_free(text);
}

/* Add the commits read by td to the end of the list (the store takes them
 * and their ref arrays over) */
static void store_commits(GitHistory* h, GitLogTaskData* td) {
    if (td->commits == NULL) return;
    g_ptr_array_set_free_func(td->commits, NULL);
    GPtrArray* items = g_ptr_array_new_with_free_func(g_object_unref);
    for (guint i = 0; i < td->commits->len; i++) {
        GitCommitInfo* ci = g_ptr_array_index(td->commits, i);
        GPtrArray* refs = NULL;
        if (td->refs_map != NULL && ci->hash != NULL) {
            gpointer orig_key = NULL;
            gpointer val = NULL;
            if (g_hash_table_steal_extended(td->refs_map, ci->hash, &orig_key, &val)) {
                g_free(orig_key);
                refs = (GPtrArray*)val;
            }
        }
        g_ptr_array_add(items, git_commit_new(ci, refs));
    }
    g_ptr_array_set_size(td->commits, 0);
    /* One items-changed for the whole page */
    guint n = g_list_model_get_n_items(G_LIST_MODEL(h->store));
    g_list_store_splice(h->store, n, 0, items->pdata, items->len);
    g_ptr_array_unref(items);
}

/* A next page was read: added after the commits listed */
static void append_page(GitHistory* h, GitLogTaskData* td) {
    h->reading_more = FALSE;
    h->more = td->more;
    store_commits(h, td);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(h->store));
    h->wanted = MAX(n, GIT_LOG_PAGE);
    show_count(h, n, h->more);
}

static void on_git_log_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    GTask* task = G_TASK(res);
    GitLogTaskData* td = (GitLogTaskData*)g_task_get_task_data(task);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: a newer read follows, or exit */

    GitHistory* h = user_data;
    if (td->append) {
        append_page(h, td);
        return;
    }
    h->reading = FALSE;
    h->more = td->more;

    GtkSingleSelection* sel = h->selection;
    g_signal_handlers_block_by_func(sel, (gpointer)on_git_selection_changed, h);

    /* Follow the newest commit unless an older one was selected on purpose */
    gboolean follow_head = TRUE;
    GitCommitInfo* old_first = git_commit_at(h, 0);
    if (h->selected_hash != NULL && old_first != NULL) {
        follow_head = (g_strcmp0(old_first->hash, h->selected_hash) == 0);
    }

    g_list_store_remove_all(h->store);
    store_commits(h, td);
    gtk_single_selection_set_selected(sel, GTK_INVALID_LIST_POSITION);

    g_signal_handlers_unblock_by_func(sel, (gpointer)on_git_selection_changed, h);

    /* Select the newest commit, or keep an older commit the user selected */
    guint n = g_list_model_get_n_items(G_LIST_MODEL(h->store));
    if (td->failed) {
        gtk_label_set_text(GTK_LABEL(h->count_label), "");
    } else {
        show_count(h, n, td->more);
    }
    if (n > 0) {
        guint target = 0;
        if (!follow_head && h->selected_hash != NULL) {
            for (guint i = 0; i < n; i++) {
                if (g_strcmp0(git_commit_at(h, i)->hash, h->selected_hash) == 0) {
                    target = i;
                    break;
                }
            }
        }
        gtk_single_selection_set_selected(sel, target); /* shows its diff */
    } else {
        populate_refs_box(h->header_refs_box, NULL, 0);
        gtk_label_set_text(GTK_LABEL(h->header_label),
                           td->failed         ? _("The history of this repository cannot be read.")
                           : h->words != NULL ? _("No commit matches the filter.")
                                              : _("No commits in this repository."));
        gtk_text_buffer_set_text(h->detail_buf, "", -1);
    }
}

/* Read the history of h->current_root on a worker thread: its first
 * h->wanted commits, replacing the list */
static void start_log_read(GitHistory* h) {
    cancel_reads(h);
    h->cancellable = g_cancellable_new();

    GitLogTaskData* td = g_new0(GitLogTaskData, 1);
    td->git_root = g_strdup(h->current_root);
    td->words = g_strdupv(h->words);
    td->max = MAX(h->wanted, GIT_LOG_PAGE);
    h->reading = TRUE;
    h->reading_more = FALSE; /* its read was cancelled with the cancellable */
    /* Filtered, the walk may go through the whole history */
    gtk_label_set_text(GTK_LABEL(h->count_label), _("Reading the history…"));

    GTask* task = g_task_new(NULL, h->cancellable, on_git_log_ready, h);
    g_task_set_task_data(task, td, git_log_task_data_free);
    g_task_run_in_thread(task, git_log_thread_func);
    g_object_unref(task);
}

/* Read the history now if the view is on screen, else when it is shown
 * (on_view_map): nothing to do at start-up or on each commit while nobody
 * looks at it */
static void read_when_shown(GitHistory* h) {
    cancel_reads(h);
    h->log_stale = !gtk_widget_get_mapped(h->widget);
    if (!h->log_stale) {
        start_log_read(h);
    }
}

void git_history_set_root(GitHistory* h, const gchar* root, gboolean reload) {
    if (h == NULL) return;
    if (root == NULL) {
        cancel_reads(h);
        g_clear_pointer(&h->current_root, g_free);
        return;
    }
    if (g_strcmp0(h->current_root, root) == 0) {
        if (!reload) return;
    } else {
        g_free(h->current_root);
        h->current_root = g_strdup(root);
        h->wanted = GIT_LOG_PAGE; /* another repository: its first page */
    }
    read_when_shown(h);
}

/* The end of the list is near: read the next page of the history, unless
 * it is all listed or a read is running */
static void read_more(GitHistory* h) {
    if (!h->more || h->reading || h->reading_more || h->current_root == NULL || h->cancellable == NULL) {
        return;
    }
    h->reading_more = TRUE;
    GitLogTaskData* td = g_new0(GitLogTaskData, 1);
    td->git_root = g_strdup(h->current_root);
    td->words = g_strdupv(h->words);
    td->skip = g_list_model_get_n_items(G_LIST_MODEL(h->store));
    td->max = GIT_LOG_PAGE;
    td->append = TRUE;
    gtk_label_set_text(GTK_LABEL(h->count_label), _("Reading more commits…"));

    /* Cancelled with the whole list (another repository, filter or reload) */
    GTask* task = g_task_new(NULL, h->cancellable, on_git_log_ready, h);
    g_task_set_task_data(task, td, git_log_task_data_free);
    g_task_run_in_thread(task, git_log_thread_func);
    g_object_unref(task);
}

/* The list scrolled near its end: the next page */
static void on_list_scrolled(GtkAdjustment* adj, gpointer data) {
    GitHistory* h = data;
    if (!h->more) return;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(h->store));
    gdouble upper = gtk_adjustment_get_upper(adj);
    if (n == 0 || upper <= 0.0) return;
    gdouble margin = upper / n * GIT_MORE_MARGIN_ROWS;
    if (gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj) >= upper - margin) {
        read_more(h);
    }
}

/* What the menu of a commit copies */
typedef enum {
    GIT_COPY_HASH,
    GIT_COPY_SHORT_HASH,
    GIT_COPY_MESSAGE
} GitCopyKind;

/* Copy a field of commit to the clipboard, with a notice */
static void copy_commit(AppData* app, GitCommit* commit, GitCopyKind kind) {
    if (commit == NULL || commit->info == NULL) return;
    GitCommitInfo* ci = commit->info;
    const gchar* text = kind == GIT_COPY_HASH         ? ci->hash
                        : kind == GIT_COPY_SHORT_HASH ? ci->short_hash
                                                      : ci->message;
    if (text == NULL || text[0] == '\0') return;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(app->window)), text);
    gchar* msg = kind == GIT_COPY_MESSAGE
                     /* TRANSLATORS: %s is a short commit hash */
                     ? g_strdup_printf(_("Copied the message of %s"), ci->short_hash != NULL ? ci->short_hash : "")
                     : g_strdup_printf(_("Copied “%s”"), text);
    app_notice(app, msg, FALSE);
    g_free(msg);
}

/* The key that copies the hash in the current mode (shown in the menu) */
static const gchar* copy_hash_key(AppData* app) {
    switch (app_nav_style(app)) {
    case NAV_STYLE_VIM: return "y";
    case NAV_STYLE_EMACS: return "M-w";
    default: return "Ctrl+C";
    }
}

/* Ctrl+C (Notepad), y (Vim), M-w (Emacs) in the list: copy the hash of the
 * selected commit. After the navigation keys (focus_route_attach_list()),
 * which let these through. */
static gboolean on_list_copy_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                                 GdkModifierType state, gpointer data) {
    (void)ctrl;
    (void)keycode;
    GitHistory* h = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    guint key = gdk_keyval_to_lower(keyval);
    gboolean copy = FALSE;
    switch (app_nav_style(h->app)) {
    case NAV_STYLE_VIM: copy = mods == 0 && keyval == GDK_KEY_y; break;
    case NAV_STYLE_EMACS: copy = mods == GDK_ALT_MASK && key == GDK_KEY_w; break;
    default: copy = mods == GDK_CONTROL_MASK && key == GDK_KEY_c; break;
    }
    if (!copy) return FALSE;
    copy_commit(h->app, gtk_single_selection_get_selected_item(h->selection), GIT_COPY_HASH);
    return TRUE;
}

static void on_copy_item(GtkButton* button, gpointer data) {
    GtkWidget* popover = data;
    AppData* app = g_object_get_data(G_OBJECT(popover), "app");
    GitCommit* commit = g_object_get_data(G_OBJECT(popover), GIT_ITEM_KEY);
    GitCopyKind kind = (GitCopyKind)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "copy-kind"));
    copy_commit(app, commit, kind);
    gtk_popover_popdown(GTK_POPOVER(popover));
}

static void menu_item(GtkWidget* box, GtkWidget* popover, const gchar* text, const gchar* hint, GitCopyKind kind) {
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
    g_signal_connect(btn, "clicked", G_CALLBACK(on_copy_item), popover);
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

/* A right click on a commit: select it, then its menu */
static void on_cell_menu(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    GitHistory* h = data;
    GtkWidget* cell = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkListItem* item = g_object_get_data(G_OBJECT(cell), GIT_ITEM_KEY);
    GitCommit* commit = item != NULL ? gtk_list_item_get_item(item) : NULL;
    GtkWidget* view = gtk_widget_get_ancestor(cell, GTK_TYPE_COLUMN_VIEW);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (commit == NULL || view == NULL) return;
    gtk_single_selection_set_selected(h->selection, gtk_list_item_get_position(item));
    gtk_widget_grab_focus(view);

    graphene_point_t at;
    if (!gtk_widget_compute_point(cell, view, &GRAPHENE_POINT_INIT((float)x, (float)y), &at)) {
        at = GRAPHENE_POINT_INIT(0.0f, 0.0f);
    }
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, view);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = {(gint)at.x, (gint)at.y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(popover), "app", h->app);
    g_object_set_data_full(G_OBJECT(popover), GIT_ITEM_KEY, g_object_ref(commit), g_object_unref);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    menu_item(box, popover, _("Copy Hash"), copy_hash_key(h->app), GIT_COPY_HASH);
    menu_item(box, popover, _("Copy Short Hash"), NULL, GIT_COPY_SHORT_HASH);
    menu_item(box, popover, _("Copy Message"), NULL, GIT_COPY_MESSAGE);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

/* Right click on the cell of a commit (child of item): its menu */
static void attach_cell_menu(GitHistory* h, GtkWidget* child, GtkListItem* item) {
    g_object_set_data(G_OBJECT(child), GIT_ITEM_KEY, item);
    GtkGesture* menu = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(menu), GDK_BUTTON_SECONDARY);
    g_signal_connect(menu, "pressed", G_CALLBACK(on_cell_menu), h);
    gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(menu));
}

/* Column cells: a label showing one GitCommitInfo string field */
static void on_git_cell_setup(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, (const gchar*)data);
    attach_cell_menu(g_object_get_data(G_OBJECT(factory), GIT_HISTORY_KEY), label, item);
    gtk_list_item_set_child(item, label);
}

static void on_git_cell_bind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)data;
    GitCommit* commit = gtk_list_item_get_item(item);
    gsize offset = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(factory), "field-offset"));
    const gchar* text = (commit != NULL && commit->info != NULL)
                            ? G_STRUCT_MEMBER(const gchar*, commit->info, offset) : NULL;
    GtkWidget* label = gtk_list_item_get_child(item);
    gtk_label_set_text(GTK_LABEL(label), text != NULL ? text : "");
    /* The Date column: the full date in its tooltip */
    if (offset == G_STRUCT_OFFSET(GitCommitInfo, date)) {
        gchar* when = commit != NULL && commit->info != NULL ? exact_date(commit->info) : NULL;
        gtk_widget_set_tooltip_text(label, when);
        g_free(when);
    }
}

static void git_add_column(GitHistory* h, GtkWidget* view, const gchar* title, gsize field_offset,
                           const gchar* css_class, gboolean expand) {
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_object_set_data(G_OBJECT(factory), GIT_HISTORY_KEY, h);
    g_object_set_data(G_OBJECT(factory), "field-offset", GSIZE_TO_POINTER(field_offset));
    g_signal_connect(factory, "setup", G_CALLBACK(on_git_cell_setup), (gpointer)css_class);
    g_signal_connect(factory, "bind", G_CALLBACK(on_git_cell_bind), NULL);
    GtkColumnViewColumn* column = gtk_column_view_column_new(title, factory);
    gtk_column_view_column_set_resizable(column, TRUE);
    gtk_column_view_column_set_expand(column, expand);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), column);
    g_object_unref(column);
}

/* Message column: branch/tag pills followed by the commit subject */
static void on_msg_cell_setup(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)factory;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* refs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(refs, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(refs, FALSE);
    gtk_box_append(GTK_BOX(box), refs);

    GtkWidget* subject = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(subject), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(subject), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(subject, TRUE);
    gtk_widget_add_css_class(subject, "git-subject");
    gtk_box_append(GTK_BOX(box), subject);

    attach_cell_menu(data, box, item);
    gtk_list_item_set_child(item, box);
}

static void on_msg_cell_bind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)factory;
    (void)data;
    GitCommit* commit = gtk_list_item_get_item(item);
    GtkWidget* box = gtk_list_item_get_child(item);
    GtkWidget* refs = gtk_widget_get_first_child(box);
    GtkWidget* subject = gtk_widget_get_last_child(box);
    populate_refs_box(refs, commit != NULL ? commit->refs : NULL, GIT_ROW_MAX_REFS);
    const gchar* text = (commit != NULL && commit->info != NULL) ? commit->info->subject : NULL;
    gtk_label_set_text(GTK_LABEL(subject), text != NULL ? text : "");
}

static void on_msg_cell_unbind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer data) {
    (void)factory;
    (void)data;
    GtkWidget* box = gtk_list_item_get_child(item);
    if (box != NULL) {
        populate_refs_box(gtk_widget_get_first_child(box), NULL, 0);
    }
}

static void git_add_message_column(GitHistory* h, GtkWidget* view) {
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_msg_cell_setup), h);
    g_signal_connect(factory, "bind", G_CALLBACK(on_msg_cell_bind), NULL);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_msg_cell_unbind), NULL);
    GtkColumnViewColumn* column = gtk_column_view_column_new(_("Message"), factory);
    gtk_column_view_column_set_resizable(column, TRUE);
    gtk_column_view_column_set_expand(column, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), column);
    g_object_unref(column);
}

/* The filter changed: read the history again with it (the whole history
 * when needed, on a worker thread) */
static void on_filter_changed(GtkSearchEntry* entry, gpointer data) {
    GitHistory* h = data;
    gchar** words = git_log_words(gtk_editable_get_text(GTK_EDITABLE(entry)));
    gboolean same = (words == NULL && h->words == NULL) ||
                    (words != NULL && h->words != NULL && g_strv_equal((const gchar* const*)words,
                                                                       (const gchar* const*)h->words));
    g_strfreev(h->words);
    h->words = words;
    if (same || h->current_root == NULL) return;
    h->wanted = GIT_LOG_PAGE;
    read_when_shown(h);
}

/* The view comes on screen: read the history it waits for */
static void on_view_map(GtkWidget* widget, gpointer data) {
    (void)widget;
    GitHistory* h = data;
    if (h->log_stale && h->current_root != NULL) {
        h->log_stale = FALSE;
        start_log_read(h);
    }
}

void git_history_cancel(GitHistory* h) {
    if (h != NULL) cancel_reads(h);
}

/* With the view's widget */
static void git_history_free(gpointer data) {
    GitHistory* h = data;
    cancel_reads(h);
    g_clear_object(&h->store);
    g_clear_object(&h->detail_buf);
    g_free(h->current_root);
    g_free(h->selected_hash);
    g_strfreev(h->words);
    g_free(h);
}

GitHistory* git_history_new(AppData* app) {
    GitHistory* h = g_new0(GitHistory, 1);
    h->app = app;
    h->wanted = GIT_LOG_PAGE;

    /* Commit list: newest first, one GitCommit per row */
    h->store = g_list_store_new(GIT_TYPE_COMMIT);
    h->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(h->store)));
    gtk_single_selection_set_autoselect(h->selection, FALSE);
    gtk_single_selection_set_selected(h->selection, GTK_INVALID_LIST_POSITION);
    g_signal_connect(h->selection, "notify::selected-item",
                     G_CALLBACK(on_git_selection_changed), h);

    GtkWidget* tree_view = gtk_column_view_new(GTK_SELECTION_MODEL(h->selection));
    focus_route_attach_list(app, tree_view, h->selection);
    GtkEventController* copy_keys = gtk_event_controller_key_new();
    g_signal_connect(copy_keys, "key-pressed", G_CALLBACK(on_list_copy_key), h);
    gtk_widget_add_controller(tree_view, copy_keys);
    gtk_widget_set_name(tree_view, "git-tree");
    gtk_widget_add_css_class(tree_view, "data-table");
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(tree_view), FALSE);
    git_add_column(h, tree_view, _("Commit"), G_STRUCT_OFFSET(GitCommitInfo, short_hash), "git-hash", FALSE);
    git_add_message_column(h, tree_view);
    git_add_column(h, tree_view, _("Author"), G_STRUCT_OFFSET(GitCommitInfo, author), "git-author", FALSE);
    git_add_column(h, tree_view, _("Date"), G_STRUCT_OFFSET(GitCommitInfo, date), "cell-muted", FALSE);

    GtkWidget* tree_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(tree_scroll, TRUE);
    gtk_widget_set_vexpand(tree_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tree_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tree_scroll), tree_view);
    gtk_widget_set_size_request(tree_scroll, -1, 120);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tree_scroll)), "value-changed",
                     G_CALLBACK(on_list_scrolled), h);

    /* Above the list: how many commits, and the filter */
    GtkWidget* list_head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    h->count_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(h->count_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(h->count_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(h->count_label, TRUE);
    gtk_widget_set_margin_start(h->count_label, 12);
    gtk_widget_add_css_class(h->count_label, "cell-muted");
    gtk_box_append(GTK_BOX(list_head), h->count_label);
    h->filter = gtk_search_entry_new();
    gtk_search_entry_set_search_delay(GTK_SEARCH_ENTRY(h->filter), 250);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(h->filter), _("Filter the commits"));
    gtk_widget_set_tooltip_text(h->filter,
                                _("Only the commits whose message, author or hash hold every word, in the whole history"));
    gtk_editable_set_width_chars(GTK_EDITABLE(h->filter), 24);
    gtk_widget_set_margin_top(h->filter, 4);
    gtk_widget_set_margin_end(h->filter, 12);
    gtk_widget_set_margin_bottom(h->filter, 4);
    g_signal_connect(h->filter, "search-changed", G_CALLBACK(on_filter_changed), h);
    focus_route_list_search(app, tree_view, h->filter);
    gtk_box_append(GTK_BOX(list_head), h->filter);

    GtkWidget* list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(list_box), list_head);
    gtk_box_append(GTK_BOX(list_box), tree_scroll);

    /* Bottom: Commit header & diff viewer */
    GtkWidget* detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(detail_box, TRUE);
    gtk_widget_set_vexpand(detail_box, TRUE);

    GtkWidget* header_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(header_bar, "git-header-bar");
    GtkWidget* header_refs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(header_refs, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(header_refs, FALSE);
    gtk_box_append(GTK_BOX(header_bar), header_refs);
    h->header_refs_box = header_refs;
    GtkWidget* header_lbl = gtk_label_new(_("Select a commit to view diff"));
    gtk_label_set_xalign(GTK_LABEL(header_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(header_lbl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(header_lbl, TRUE);
    gtk_box_append(GTK_BOX(header_bar), header_lbl);
    h->header_label = header_lbl;

    h->detail_buf = gtk_text_buffer_new(NULL);
    GtkWidget* detail_view = gtk_text_view_new_with_buffer(h->detail_buf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(detail_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(detail_view), FALSE);
    set_view_text_padding(detail_view);
    set_view_monospace(detail_view, TRUE);
    apply_viewer_background(detail_view);

    GtkWidget* detail_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(detail_scroll, TRUE);
    gtk_widget_set_vexpand(detail_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(detail_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(detail_scroll), detail_view);

    gtk_box_append(GTK_BOX(detail_box), header_bar);
    gtk_box_append(GTK_BOX(detail_box), detail_scroll);

    /* Paned dividing commits table and commit diff */
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    paned_use_thin_handle(paned);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), list_box);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(paned), detail_box);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 220);

    h->widget = paned;
    g_object_set_data_full(G_OBJECT(paned), GIT_HISTORY_KEY, h, git_history_free);
    g_signal_connect(paned, "map", G_CALLBACK(on_view_map), h);
    return h;
}

GtkWidget* git_history_widget(GitHistory* h) {
    return h != NULL ? h->widget : NULL;
}
