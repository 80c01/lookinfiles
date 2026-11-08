/* The outline of an editor tab (see code_outline.h) */
#include "code_outline.h"
#include "code_intel.h"
#include "code_nav.h"
#include "code_scan.h"
#include "file_types.h"
#include "i18n.h"
#include <string.h>

/* A server that has not answered by then (starting, indexing, hung) is not
 * waited for: the built-in scanner answers instead */
#define SERVER_WAIT_MS 2500

typedef struct {
    AppData* app;
    EditorTab* tab;
    GCancellable* cancellable;
    CodeOutlineFunc func;
    gpointer data;
    GDestroyNotify destroy;
    gint refs;           /* the server answer, the wait timeout, the scan */
    guint wait_source;   /* SERVER_WAIT_MS timeout, 0 once over */
    gboolean scanning;   /* the built-in scanner was started */
    gboolean answered;   /* func was called: later answers are dropped */
    /* Worker input */
    gchar* text;
    CodeLang lang;
    gboolean markdown;
} Request;

static void request_unref(Request* r) {
    if (--r->refs > 0) return;
    if (r->destroy != NULL) r->destroy(r->data);
    g_clear_object(&r->cancellable);
    g_free(r->text);
    g_free(r);
}

static gboolean is_markdown(EditorTab* tab) {
    return tab != NULL && tab->name != NULL && is_markdown_file(tab->name);
}

gboolean code_outline_supported(EditorTab* tab) {
    return tab != NULL && tab->buffer != NULL && (is_markdown(tab) || code_nav_is_code(tab));
}

/* Is tab open, in the current workspace or another one */
static gboolean tab_open(AppData* app, EditorTab* tab) {
    if (g_list_find(app->editors, tab) != NULL) return TRUE;
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (i != app->current_ws && g_list_find(app->workspaces[i].editors, tab) != NULL) return TRUE;
    }
    return FALSE;
}

/* The request can still answer: not answered yet nor cancelled, its tab open */
static gboolean request_live(Request* r) {
    if (r->answered) return FALSE;
    if (r->cancellable != NULL && g_cancellable_is_cancelled(r->cancellable)) return FALSE;
    return tab_open(r->app, r->tab);
}

static void answer(Request* r, GPtrArray* entries, const gchar* source) {
    if (!request_live(r)) return;
    r->answered = TRUE;
    r->func(r->app, r->tab, entries, source, r->data);
}

/* ------------------------------------------------------------------------- */
/* Built-in scanner and Markdown headings, in a worker                       */
/* ------------------------------------------------------------------------- */

static void scan_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    Request* r = data;
    gsize len = strlen(r->text);
    GPtrArray* entries;
    if (r->markdown) {
        entries = code_outline_markdown(r->text, len);
    } else {
        entries = code_outline_entries_new();
        GPtrArray* syms = code_scan_text(r->text, len, r->lang, NULL);
        for (guint i = 0; i < syms->len; i++) {
            CodeSymbol* s = g_ptr_array_index(syms, i);
            CodeOutlineEntry* e = code_outline_entry_new(s->name, s->signature, NULL, s->kind, s->line, s->column);
            e->declaration = s->declaration;
            g_ptr_array_add(entries, e);
        }
        g_ptr_array_unref(syms);
    }
    code_outline_sort(entries);
    g_task_return_pointer(task, entries, (GDestroyNotify)g_ptr_array_unref);
}

static void on_scanned(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    Request* r = g_task_get_task_data(G_TASK(res));
    GPtrArray* entries = g_task_propagate_pointer(G_TASK(res), NULL);
    if (entries != NULL) {
        answer(r, entries, r->markdown ? _("Markdown headings") : _("built-in scanner"));
        g_ptr_array_unref(entries);
    }
}

/* Takes one reference of r. Started from code_outline_collect(), the tab
 * may not be in the editor list yet (the tagbar reads it while the tab is
 * built): the caller vouches for it, only later starts check it. */
static void start_scan(Request* r, gboolean check_tab) {
    if (r->scanning || r->answered || (check_tab && !request_live(r))) {
        request_unref(r);
        return;
    }
    r->scanning = TRUE;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(r->tab->buffer, &s, &e);
    r->text = gtk_text_buffer_get_text(r->tab->buffer, &s, &e, TRUE);
    r->markdown = is_markdown(r->tab);
    r->lang = r->markdown ? CODE_LANG_NONE : code_lang_from_name(r->tab->path);
    GTask* task = g_task_new(NULL, NULL, on_scanned, NULL);
    g_task_set_task_data(task, r, (GDestroyNotify)request_unref);
    g_task_run_in_thread(task, scan_thread);
    g_object_unref(task);
}

/* ------------------------------------------------------------------------- */
/* Language server                                                           */
/* ------------------------------------------------------------------------- */

static void on_server_symbols(AppData* app, EditorTab* tab, GPtrArray* symbols, gpointer data) {
    (void)app;
    (void)tab;
    Request* r = data;
    if (r->wait_source != 0) {
        /* Answered in time: the wait's reference goes (the server's stays) */
        g_clear_handle_id(&r->wait_source, g_source_remove);
        r->refs--;
    }
    if (!request_live(r)) {
        request_unref(r);
        return;
    }
    if (symbols == NULL || symbols->len == 0) {
        start_scan(r, TRUE);
        return;
    }
    GPtrArray* entries = code_outline_entries_new();
    for (guint i = 0; i < symbols->len; i++) {
        LspSymbol* s = g_ptr_array_index(symbols, i);
        g_ptr_array_add(entries, code_outline_entry_new(s->name, s->detail, s->container, s->kind, s->line, s->column));
    }
    code_outline_sort(entries);
    gchar* server = code_intel_server_label(r->app, r->tab);
    answer(r, entries, server != NULL ? server : _("language server"));
    g_free(server);
    g_ptr_array_unref(entries);
    request_unref(r);
}

static gboolean on_server_wait(gpointer data) {
    Request* r = data;
    r->wait_source = 0;
    start_scan(r, TRUE); /* the timeout's reference */
    return G_SOURCE_REMOVE;
}

void code_outline_collect(AppData* app, EditorTab* tab, GCancellable* cancellable, CodeOutlineFunc func,
                          gpointer data, GDestroyNotify destroy) {
    Request* r = g_new0(Request, 1);
    r->app = app;
    r->tab = tab;
    r->cancellable = cancellable != NULL ? g_object_ref(cancellable) : NULL;
    r->func = func;
    r->data = data;
    r->destroy = destroy;
    r->refs = 1;
    if (app == NULL || !code_outline_supported(tab)) {
        request_unref(r);
        return;
    }
    if (is_markdown(tab) || !code_intel_document_symbols(app, tab, on_server_symbols, r)) {
        start_scan(r, FALSE);
        return;
    }
    /* The server holds the first reference, the wait the second */
    r->refs++;
    r->wait_source = g_timeout_add(SERVER_WAIT_MS, on_server_wait, r);
}
