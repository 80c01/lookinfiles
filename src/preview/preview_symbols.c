/* Symbols of a source code preview (see preview_symbols.h) */
#include "preview/preview_symbols.h"
#include "code/code_outline_tree.h"
#include "code/code_scan.h"
#include "preview/preview_tagbar.h"
#include <string.h>

typedef struct {
    AppData* app;
    GCancellable* owner; /* the preview's cancellable */
    gchar* text;
    CodeLang lang;
    gint first_line;
} ScanData;

static void scan_data_free(ScanData* sd) {
    g_clear_object(&sd->owner);
    g_free(sd->text);
    g_free(sd);
}

static void scan_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    ScanData* sd = data;
    if (g_task_return_error_if_cancelled(task)) return;
    GPtrArray* syms = code_scan_text(sd->text, strlen(sd->text), sd->lang, NULL);
    GPtrArray* entries = code_outline_entries_new();
    for (guint i = 0; i < syms->len && !g_cancellable_is_cancelled(cancellable); i++) {
        const CodeSymbol* s = g_ptr_array_index(syms, i);
        CodeOutlineEntry* e =
            code_outline_entry_new(s->name, s->signature, NULL, s->kind, s->line + sd->first_line, s->column);
        e->declaration = s->declaration;
        g_ptr_array_add(entries, e);
    }
    g_ptr_array_unref(syms);
    code_outline_sort(entries);
    g_task_return_pointer(task, entries, (GDestroyNotify)g_ptr_array_unref);
}

static void on_scanned(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    ScanData* sd = g_task_get_task_data(G_TASK(res));
    GPtrArray* entries = g_task_propagate_pointer(G_TASK(res), NULL);
    if (entries == NULL) return;
    if (sd->owner == sd->app->preview_cancellable && !g_cancellable_is_cancelled(sd->owner)) {
        preview_tagbar_set_outline(sd->app, sd->owner, entries);
    }
    g_ptr_array_unref(entries);
}

void preview_symbols_start(AppData* app, const gchar* name, const gchar* text, gint first_line) {
    if (app == NULL || app->preview_cancellable == NULL || text == NULL) return;
    CodeLang lang = code_lang_detect(name, text, -1);
    if (!code_scan_supported(lang)) return;
    ScanData* sd = g_new0(ScanData, 1);
    sd->app = app;
    sd->owner = g_object_ref(app->preview_cancellable);
    sd->text = g_strdup(text);
    sd->lang = lang;
    sd->first_line = first_line;
    GTask* task = g_task_new(NULL, sd->owner, on_scanned, NULL);
    g_task_set_task_data(task, sd, (GDestroyNotify)scan_data_free);
    g_task_run_in_thread(task, scan_thread);
    g_object_unref(task);
}
