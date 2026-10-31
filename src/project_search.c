/* Search in files (see project_search.h) */
#include "project_search.h"
#include "code_index.h"
#include "code_refs.h"
#include "dialog.h"
#include "editor.h"
#include "text_search.h"

#include <string.h>

#define RESPONSE_SEARCH 1

static gchar* last_query;
static GCancellable* running; /* the search in progress, if any */

typedef struct {
    AppData* app;
    GCancellable* cancellable;
    gchar* query;
    gchar* root;
    GPtrArray* refs;
    gboolean truncated;
} Search;

static void search_free(gpointer data) {
    Search* s = data;
    g_object_unref(s->cancellable);
    g_free(s->query);
    g_free(s->root);
    if (s->refs != NULL) g_ptr_array_unref(s->refs);
    g_free(s);
}

static void search_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)task;
    (void)source;
    Search* s = data;
    gboolean cut_files = FALSE;
    GPtrArray* files = code_index_all_files(s->root, cancellable, &cut_files);
    s->refs = text_search_files(s->query, files, cancellable, &s->truncated);
    s->truncated = s->truncated || cut_files;
    g_ptr_array_unref(files);
}

static void search_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    Search* s = g_task_get_task_data(G_TASK(res));
    if (g_cancellable_is_cancelled(s->cancellable)) return; /* a newer search replaced it */
    if (running == s->cancellable) g_clear_object(&running);
    code_refs_show_search(s->app, s->query, s->refs, s->root, text_search_exact_case(s->query), s->truncated);
}

static void start_search(AppData* app, const gchar* query) {
    if (running != NULL) {
        g_cancellable_cancel(running);
        g_clear_object(&running);
    }
    g_free(last_query);
    last_query = g_strdup(query);

    Search* s = g_new0(Search, 1);
    s->app = app;
    s->cancellable = g_cancellable_new();
    s->query = g_strdup(query);
    s->root = code_index_project_root(app->base_dir);
    running = g_object_ref(s->cancellable);
    code_refs_show_searching(app, s->query, s->root);

    GTask* task = g_task_new(NULL, s->cancellable, search_done, NULL);
    g_task_set_task_data(task, s, search_free);
    g_task_run_in_thread(task, search_thread);
    g_object_unref(task);
}

/* The selection of the current editor tab when it is on one line */
static gchar* selected_text(AppData* app) {
    EditorTab* tab = editor_current_tab(app);
    GtkTextIter a, b;
    if (tab == NULL || tab->buffer == NULL || !gtk_text_buffer_get_selection_bounds(tab->buffer, &a, &b)) {
        return NULL;
    }
    gchar* text = gtk_text_buffer_get_text(tab->buffer, &a, &b, FALSE);
    if (text[0] == '\0' || strchr(text, '\n') != NULL) {
        g_free(text);
        return NULL;
    }
    return text;
}

typedef struct {
    AppData* app;
    GtkWidget* entry;
} Prompt;

static void on_response(AppDialog* dialog, gint response, gpointer data) {
    Prompt* p = data;
    gchar* query = response == RESPONSE_SEARCH ? g_strdup(gtk_editable_get_text(GTK_EDITABLE(p->entry))) : NULL;
    AppData* app = p->app;
    app_dialog_destroy(dialog); /* frees p */
    if (query != NULL && query[0] != '\0') {
        start_search(app, query);
    }
    g_free(query);
}

void project_search_show(AppData* app) {
    if (app == NULL || app->window == NULL || app->base_dir == NULL || app_dialog_is_open(app->window)) {
        return;
    }
    gchar* root = code_index_project_root(app->base_dir);
    Prompt* p = g_new0(Prompt, 1);
    p->app = app;
    GtkWidget* dlg = app_dialog_new("Search in Files", app->window, "_Cancel", APP_RESPONSE_CANCEL, NULL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), "_Search", RESPONSE_SEARCH);
    gtk_widget_add_css_class(ok, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), RESPONSE_SEARCH);
    app_dialog_set_width(APP_DIALOG(dlg), 520);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);

    p->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry), "Text to find");
    gtk_entry_set_activates_default(GTK_ENTRY(p->entry), TRUE);
    gchar* initial = selected_text(app);
    gtk_editable_set_text(GTK_EDITABLE(p->entry), initial != NULL ? initial : last_query != NULL ? last_query : "");
    g_free(initial);
    gtk_box_append(GTK_BOX(box), p->entry);

    gchar* where = g_strdup_printf("In every file of %s (any case unless the text has a capital letter)", root);
    GtkWidget* note = gtk_label_new(where);
    g_free(where);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_box_append(GTK_BOX(box), note);
    g_free(root);

    g_signal_connect(dlg, "response", G_CALLBACK(on_response), p);
    g_object_set_data_full(G_OBJECT(dlg), "prompt", p, g_free);
    app_dialog_present(APP_DIALOG(dlg));
    gtk_widget_grab_focus(p->entry);
    gtk_editable_select_region(GTK_EDITABLE(p->entry), 0, -1);
}
