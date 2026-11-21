/* The Repository view of the Git tab (see git_repo_view.h) */
#include "git/git_repo_view.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "git/git_repo_info.h"
#include <string.h>
#include "widgets/focus_route.h"

/* Data of the view's widget: the GitRepoView, freed with it */
#define REPO_VIEW_KEY "lookin-git-repo-view"

struct GitRepoView {
    AppData* app;
    GtkWidget* widget;       /* the view (owns this struct) */
    GtkWidget* status;       /* reading, unreadable */
    GtkWidget* identity;     /* GtkGrid */
    GtkWidget* remotes;      /* GtkGrid */
    GtkWidget* branches;     /* GtkGrid */
    GtkWidget* config;       /* GtkGrid */
    GtkWidget* filter;       /* search entry over the configuration */
    GtkWidget* edit_global;  /* button opening the global config file */
    GitRepoInfo* info;       /* shown, or NULL */
    gchar* root;
    GCancellable* cancellable;
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".git-repo { padding: 6px 14px 14px 14px; }"
                  ".git-repo .git-repo-title { color: @fg_strong; font-weight: 600; margin-top: 14px; }"
                  ".git-repo .git-repo-key { color: @fg_subtle; }"
                  ".git-repo .git-repo-value { color: @fg_text; }"
                  ".git-repo .git-repo-code { color: @fg_text; font-family: @font_code; }"
                  ".git-repo .git-repo-level { color: @fg_subtle; }"
                  ".git-repo .git-repo-head { color: @fg_strong; font-weight: 600; }"
                  ".git-repo .git-repo-ahead { color: @git_added; }"
                  ".git-repo .git-repo-behind { color: @git_modified; }"
                  ".git-repo .git-repo-warn { color: @git_deleted; }"
                  ".git-repo .git-repo-dim, .git-repo .git-repo-dim label { color: @fg_subtle; }"
                  ".git-repo .git-repo-overridden { color: @fg_subtle; text-decoration-line: line-through; }");
}

static void clear_grid(GtkWidget* grid) {
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(grid)) != NULL) gtk_grid_remove(GTK_GRID(grid), child);
}

/* A label in grid at col, row; selectable values can be copied */
static GtkWidget* cell(GtkWidget* grid, const gchar* text, const gchar* css_class, gint col, gint row,
                       gboolean selectable) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), selectable);
    /* Wrapped: a long setting or message never widens the preview panel (a
     * notebook is as wide as its widest page) */
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_widget_set_focusable(label, FALSE); /* selectable labels would take the Tab key */
    if (css_class != NULL) gtk_widget_add_css_class(label, css_class);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
    return label;
}

static GtkWidget* new_grid(void) {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    return grid;
}

/* "(Global)": where a setting comes from */
static gchar* level_note(GitConfigLevel level) {
    return g_strdup_printf("(%s)", git_config_level_title(level));
}

static void identity_row(GitRepoView* v, gint row, const gchar* key, const gchar* value, GitConfigLevel level) {
    cell(v->identity, key, "git-repo-key", 0, row, FALSE);
    if (value == NULL) {
        /* TRANSLATORS: an identity setting of Git (user.name, user.email) missing */
        cell(v->identity, _("Not set"), "git-repo-warn", 1, row, FALSE);
        return;
    }
    cell(v->identity, value, "git-repo-value", 1, row, TRUE);
    gchar* note = level_note(level);
    cell(v->identity, note, "git-repo-level", 2, row, FALSE);
    g_free(note);
}

static void show_identity(GitRepoView* v) {
    clear_grid(v->identity);
    const GitRepoInfo* info = v->info;
    if (info == NULL) return;
    identity_row(v, 0, _("Name"), info->user_name, info->user_name_level);
    identity_row(v, 1, _("Email"), info->user_email, info->user_email_level);
    if (info->signs) {
        cell(v->identity, _("Signing"), "git-repo-key", 0, 2, FALSE);
        gchar* text = info->signing_key != NULL ? g_strdup_printf(_("Commits are signed with %s"), info->signing_key)
                                                : g_strdup(_("Commits are signed"));
        cell(v->identity, text, "git-repo-value", 1, 2, TRUE);
        g_free(text);
    }
}

static void show_remotes(GitRepoView* v) {
    clear_grid(v->remotes);
    const GitRepoInfo* info = v->info;
    if (info == NULL) return;
    if (info->remotes->len == 0) {
        cell(v->remotes, _("No remote: this repository is not pushed anywhere."), "git-repo-dim", 0, 0, FALSE);
        return;
    }
    gint row = 0;
    for (guint i = 0; i < info->remotes->len; i++) {
        const GitRemoteInfo* r = g_ptr_array_index(info->remotes, i);
        cell(v->remotes, r->name, "git-repo-head", 0, row, TRUE);
        cell(v->remotes, r->url, "git-repo-code", 1, row, TRUE);
        if (r->push_url != NULL) {
            /* TRANSLATORS: the URL this remote fetches from */
            cell(v->remotes, _("fetch"), "git-repo-level", 2, row, FALSE);
            row++;
            cell(v->remotes, r->push_url, "git-repo-code", 1, row, TRUE);
            /* TRANSLATORS: the URL this remote pushes to, when it differs */
            cell(v->remotes, _("push"), "git-repo-level", 2, row, FALSE);
        }
        row++;
    }
}

static void show_branches(GitRepoView* v) {
    clear_grid(v->branches);
    const GitRepoInfo* info = v->info;
    if (info == NULL) return;
    if (info->branches->len == 0) {
        cell(v->branches, _("No branch yet: nothing is committed."), "git-repo-dim", 0, 0, FALSE);
        return;
    }
    for (guint i = 0; i < info->branches->len; i++) {
        const GitBranchState* b = g_ptr_array_index(info->branches, i);
        gint row = (gint)i;
        GtkWidget* name = cell(v->branches, b->name, b->is_head ? "git-repo-head" : "git-repo-value", 0, row, TRUE);
        if (b->is_head) gtk_widget_set_tooltip_text(name, _("Checked out"));
        if (b->upstream == NULL) {
            cell(v->branches, _("no upstream"), "git-repo-dim", 1, row, FALSE);
            continue;
        }
        cell(v->branches, b->upstream, "git-repo-code", 1, row, TRUE);
        if (b->gone) {
            cell(v->branches, _("upstream gone"), "git-repo-warn", 2, row, FALSE);
        } else if (b->ahead == 0 && b->behind == 0) {
            cell(v->branches, _("up to date"), "git-repo-dim", 2, row, FALSE);
        } else {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            if (b->ahead > 0) {
                gchar* text = g_strdup_printf("↑%u", b->ahead);
                GtkWidget* l = gtk_label_new(text);
                g_free(text);
                gtk_widget_add_css_class(l, "git-repo-ahead");
                gchar* tip = g_strdup_printf(ngettext("%u commit to push", "%u commits to push", b->ahead), b->ahead);
                gtk_widget_set_tooltip_text(l, tip);
                g_free(tip);
                gtk_box_append(GTK_BOX(box), l);
            }
            if (b->behind > 0) {
                gchar* text = g_strdup_printf("↓%u", b->behind);
                GtkWidget* l = gtk_label_new(text);
                g_free(text);
                gtk_widget_add_css_class(l, "git-repo-behind");
                gchar* tip = g_strdup_printf(ngettext("%u commit to pull", "%u commits to pull", b->behind), b->behind);
                gtk_widget_set_tooltip_text(l, tip);
                g_free(tip);
                gtk_box_append(GTK_BOX(box), l);
            }
            gtk_grid_attach(GTK_GRID(v->branches), box, 2, row, 1, 1);
        }
    }
}

/* item matches every word of the filter (key or value, any case) */
static gboolean config_matches(const GitConfigItem* item, gchar** words) {
    if (words == NULL) return TRUE;
    gchar* text = g_strdup_printf("%s %s", item->key, item->value);
    gchar* folded = g_utf8_casefold(text, -1);
    gboolean all = TRUE;
    for (gchar** w = words; *w != NULL && all; w++) all = strstr(folded, *w) != NULL;
    g_free(folded);
    g_free(text);
    return all;
}

static void show_config(GitRepoView* v) {
    clear_grid(v->config);
    const GitRepoInfo* info = v->info;
    if (info == NULL) return;
    gchar* folded = g_utf8_casefold(gtk_editable_get_text(GTK_EDITABLE(v->filter)), -1);
    gchar** words = g_strsplit_set(g_strstrip(folded), " \t", -1);
    if (words[0] == NULL || words[0][0] == '\0') g_clear_pointer(&words, g_strfreev);
    gint row = 0;
    for (guint i = 0; i < info->config->len; i++) {
        const GitConfigItem* item = g_ptr_array_index(info->config, i);
        if (!config_matches(item, words)) continue;
        const gchar* look = item->overridden ? "git-repo-overridden" : "git-repo-code";
        GtkWidget* key = cell(v->config, item->key, look, 0, row, TRUE);
        GtkWidget* value = cell(v->config, item->value, look, 1, row, TRUE);
        gtk_widget_set_hexpand(value, TRUE);
        cell(v->config, git_config_level_title(item->level), "git-repo-level", 2, row, FALSE);
        if (item->overridden) {
            const gchar* tip = _("Not in effect: set again at a closer level");
            gtk_widget_set_tooltip_text(key, tip);
            gtk_widget_set_tooltip_text(value, tip);
        }
        row++;
    }
    if (row == 0) {
        cell(v->config, words != NULL ? _("No setting matches the filter.") : _("No setting."), "git-repo-dim", 0, 0,
             FALSE);
    }
    g_strfreev(words);
    g_free(folded);
}

static void show_info(GitRepoView* v, GitRepoInfo* info) {
    git_repo_info_free(v->info);
    v->info = info;
    gtk_widget_set_visible(v->status, FALSE);
    gtk_widget_set_sensitive(v->edit_global, info != NULL && info->global_config != NULL);
    show_identity(v);
    show_remotes(v);
    show_branches(v);
    show_config(v);
}

static void on_filter_changed(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    show_config(data);
}

/* Open a config file in an editor tab, through the event bus */
static void edit_file(GitRepoView* v, const gchar* path) {
    if (path == NULL) return;
    AppData* app = v->app;
    g_free(app->edit_request_path);
    app->edit_request_path = g_strdup(path);
    app_emit(app, APP_EVENT_EDIT_PATH);
    g_clear_pointer(&app->edit_request_path, g_free); /* not taken: nothing subscribed */
}

static void on_edit_repo(GtkButton* button, gpointer data) {
    (void)button;
    GitRepoView* v = data;
    if (v->info != NULL) edit_file(v, v->info->repo_config);
}

static void on_edit_global(GtkButton* button, gpointer data) {
    (void)button;
    GitRepoView* v = data;
    if (v->info != NULL) edit_file(v, v->info->global_config);
}

/* --- Reading */

typedef struct {
    gchar* root;
    GitRepoInfo* info;
} ReadTask;

static void read_task_free(gpointer data) {
    ReadTask* td = data;
    g_free(td->root);
    git_repo_info_free(td->info);
    g_free(td);
}

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    ReadTask* td = data;
    td->info = git_repo_info_read(td->root);
    if (g_task_return_error_if_cancelled(task)) return;
    g_task_return_boolean(task, TRUE);
}

/* The task reaches its view through its callback data: it is cancelled
 * before the view is freed, and a cancelled task's callback returns first */
static void on_read_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: a newer read, or exit */
    GitRepoView* v = data;
    ReadTask* td = g_task_get_task_data(task);
    if (td->info == NULL) {
        show_info(v, NULL);
        gtk_label_set_text(GTK_LABEL(v->status), _("This repository cannot be read."));
        gtk_widget_set_visible(v->status, TRUE);
        return;
    }
    show_info(v, g_steal_pointer(&td->info));
}

static void cancel_read(GitRepoView* v) {
    if (v->cancellable != NULL) {
        g_cancellable_cancel(v->cancellable);
        g_clear_object(&v->cancellable);
    }
}

static void start_read(GitRepoView* v) {
    cancel_read(v);
    v->cancellable = g_cancellable_new();
    ReadTask* td = g_new0(ReadTask, 1);
    td->root = g_strdup(v->root);
    if (v->info == NULL) {
        gtk_label_set_text(GTK_LABEL(v->status), _("Reading the repository…"));
        gtk_widget_set_visible(v->status, TRUE);
    }
    GTask* task = g_task_new(NULL, v->cancellable, on_read_ready, v);
    g_task_set_task_data(task, td, read_task_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
}

void git_repo_view_set_root(GitRepoView* v, const gchar* root, gboolean reload) {
    if (v == NULL) return;
    if (g_strcmp0(v->root, root) != 0) {
        cancel_read(v);
        g_free(v->root);
        v->root = g_strdup(root);
        show_info(v, NULL);
    } else if (!reload) {
        return;
    }
    if (v->root != NULL && gtk_widget_get_mapped(v->widget)) start_read(v);
}

/* The view comes on screen: read again (the global config is not watched) */
static void on_view_map(GtkWidget* widget, gpointer data) {
    (void)widget;
    GitRepoView* v = data;
    if (v->root != NULL) start_read(v);
}

void git_repo_view_cancel(GitRepoView* v) {
    if (v != NULL) cancel_read(v);
}

static void git_repo_view_free(gpointer data) {
    GitRepoView* v = data;
    cancel_read(v);
    git_repo_info_free(v->info);
    g_free(v->root);
    g_free(v);
}

static GtkWidget* section(GtkWidget* box, const gchar* title, GtkWidget* content) {
    GtkWidget* label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "git-repo-title");
    gtk_box_append(GTK_BOX(box), label);
    if (content != NULL) gtk_box_append(GTK_BOX(box), content);
    return label;
}

GitRepoView* git_repo_view_new(AppData* app) {
    ensure_css();
    GitRepoView* v = g_new0(GitRepoView, 1);
    v->app = app;

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box, "git-repo");
    v->status = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(v->status), 0.0f);
    gtk_widget_add_css_class(v->status, "git-repo-dim");
    gtk_widget_set_visible(v->status, FALSE);
    gtk_box_append(GTK_BOX(box), v->status);

    v->identity = new_grid();
    v->remotes = new_grid();
    v->branches = new_grid();
    v->config = new_grid();
    /* TRANSLATORS: the name and email commits are made with */
    section(box, _("Identity"), v->identity);
    section(box, _("Remotes"), v->remotes);
    section(box, _("Branches"), v->branches);
    section(box, _("Configuration"), NULL);

    /* Above the configuration: its filter, then the files to edit (on two
     * rows: on one, they would make the preview panel wide) */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    v->filter = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(v->filter), _("Filter the settings"));
    gtk_widget_set_tooltip_text(v->filter, _("Only the settings whose name or value hold every word"));
    gtk_editable_set_width_chars(GTK_EDITABLE(v->filter), 24);
    gtk_widget_set_halign(v->filter, GTK_ALIGN_START);
    g_signal_connect(v->filter, "search-changed", G_CALLBACK(on_filter_changed), v);
    gtk_box_append(GTK_BOX(box), v->filter);
    GtkWidget* edit_repo = icon_button_new("document-edit", _("Open .git/config in an Editor Tab"));
    icon_button_set_content(edit_repo, "document-edit", _("Edit .git/config"));
    g_signal_connect(edit_repo, "clicked", G_CALLBACK(on_edit_repo), v);
    gtk_box_append(GTK_BOX(bar), edit_repo);
    v->edit_global = icon_button_new("document-edit", _("Open the Global Config File in an Editor Tab"));
    icon_button_set_content(v->edit_global, "document-edit", _("Edit Global Config"));
    g_signal_connect(v->edit_global, "clicked", G_CALLBACK(on_edit_global), v);
    gtk_box_append(GTK_BOX(bar), v->edit_global);
    gtk_box_append(GTK_BOX(box), bar);
    gtk_box_append(GTK_BOX(box), v->config);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    focus_route_mark_focus(scroll);

    v->widget = scroll;
    g_object_set_data_full(G_OBJECT(scroll), REPO_VIEW_KEY, v, git_repo_view_free);
    g_signal_connect(scroll, "map", G_CALLBACK(on_view_map), v);
    return v;
}

GtkWidget* git_repo_view_widget(GitRepoView* v) {
    return v != NULL ? v->widget : NULL;
}
