/* Language servers for code navigation (see code_intel.h) */
#include "code_intel.h"
#include "app_events.h"
#include "app_paths.h"
#include "text_tags.h"
#include "code_index.h"
#include "editor.h"
#include "lsp_client.h"
#include "lsp_config.h"
#include <string.h>

#define RESTART_DELAY_US (60 * G_USEC_PER_SEC)
#define SYNC_DELAY_MS 600
#define SYNC_TIMER_KEY "code-intel-sync"

typedef struct {
    LspClient* client;
    gchar* name;        /* program name, until the server says its own */
    gint64 failed_at;   /* monotonic time of the failure, 0 when running */
} Server;

typedef struct {
    GHashTable* servers;   /* "root\ncommand" -> Server* */
    GHashTable* commands;  /* CodeLang (as pointer) -> gchar** argv, or a NULL value: none */
    GHashTable* diagnostics; /* canonical path -> GPtrArray of LspDiagnostic (byte columns) */
} CodeIntel;

static void on_server_notify(LspClient* client, const LspMessage* msg, gpointer data);

typedef enum {
    REQ_LOCATIONS,
    REQ_HOVER,
    REQ_SIGNATURE,
    REQ_SYMBOLS
} RequestKind;

typedef struct {
    AppData* app;
    EditorTab* tab;
    RequestKind kind;
    gpointer func;
    gpointer data;
    gboolean utf8;     /* positions in bytes (known once ready; re-read on the answer) */
} Request;

static void server_free(gpointer p) {
    Server* s = p;
    lsp_client_free(s->client);
    g_free(s->name);
    g_free(s);
}

static CodeIntel* intel_of(AppData* app) {
    if (app->code_intel == NULL) {
        CodeIntel* ci = g_new0(CodeIntel, 1);
        ci->servers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, server_free);
        ci->commands = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_strfreev);
        ci->diagnostics = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
        app->code_intel = ci;
    }
    return app->code_intel;
}

static CodeLang tab_lang(EditorTab* tab) {
    if (tab == NULL || tab->path == NULL || tab->mode == EDITOR_MODE_ORGMODE) return CODE_LANG_NONE;
    return code_lang_from_name(tab->path);
}

/* Server command of a language (looked up once per session) */
static gchar** command_of(CodeIntel* ci, CodeLang lang) {
    gpointer key = GINT_TO_POINTER(lang);
    gpointer value = NULL;
    if (g_hash_table_lookup_extended(ci->commands, key, NULL, &value)) return value;
    gchar* dir = app_config_dir();
    gchar* ini = g_build_filename(dir, "lsp.ini", NULL);
    gchar** argv = lsp_config_command(lang, ini);
    g_free(ini);
    g_free(dir);
    g_hash_table_insert(ci->commands, key, argv);
    return argv;
}

static gboolean tab_alive(AppData* app, EditorTab* tab) {
    return tab != NULL && g_list_find(app->editors, tab) != NULL;
}

static void on_server_state(LspClient* client, LspClientState state, gpointer data) {
    (void)client;
    (void)state;
    /* The status bar shows the server and its state */
    app_emit((AppData*)data, APP_EVENT_STATUS_CHANGED);
}

/* Server key of a tab: "root\ncommand" (caller frees), NULL without server */
static gchar* server_key(CodeIntel* ci, EditorTab* tab, gchar*** argv_out, gchar** root_out) {
    CodeLang lang = tab_lang(tab);
    if (lang == CODE_LANG_NONE) return NULL;
    gchar** argv = command_of(ci, lang);
    if (argv == NULL) return NULL;
    gchar* root = code_index_project_root(tab->path);
    if (root == NULL) return NULL;
    gchar* cmd = g_strjoinv(" ", argv);
    gchar* key = g_strdup_printf("%s\n%s", root, cmd);
    g_free(cmd);
    if (argv_out != NULL) *argv_out = argv;
    if (root_out != NULL) *root_out = root;
    else g_free(root);
    return key;
}

/* The server of a tab, started when needed (start); NULL when none runs */
static Server* server_for(AppData* app, EditorTab* tab, gboolean start) {
    CodeIntel* ci = intel_of(app);
    gchar** argv = NULL;
    gchar* root = NULL;
    gchar* key = server_key(ci, tab, &argv, &root);
    if (key == NULL) return NULL;
    Server* s = g_hash_table_lookup(ci->servers, key);
    if (s != NULL && lsp_client_get_state(s->client) == LSP_CLIENT_FAILED) {
        if (s->failed_at == 0) s->failed_at = g_get_monotonic_time();
        if (!start || g_get_monotonic_time() - s->failed_at < RESTART_DELAY_US) {
            g_free(key);
            g_free(root);
            return NULL;
        }
        /* Try again after a while (the server may have been installed / fixed) */
        g_hash_table_remove(ci->servers, key);
        s = NULL;
    }
    if (s == NULL && start) {
        LspClient* client = lsp_client_new(argv, root, on_server_state, app, NULL);
        lsp_client_set_notify_func(client, on_server_notify, app);
        s = g_new0(Server, 1);
        s->client = client;
        s->name = g_path_get_basename(argv[0]);
        if (client == NULL) s->failed_at = g_get_monotonic_time();
        g_hash_table_insert(ci->servers, g_strdup(key), s);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    }
    g_free(key);
    g_free(root);
    return s != NULL && s->client != NULL ? s : NULL;
}

static gchar* buffer_text(GtkTextBuffer* buffer) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    return gtk_text_buffer_get_text(buffer, &s, &e, TRUE);
}

/* Send the open tabs served by this server; drop documents closed since */
static void sync_documents(AppData* app, Server* s, EditorTab* origin) {
    CodeIntel* ci = intel_of(app);
    gchar* origin_key = server_key(ci, origin, NULL, NULL);
    GHashTable* open = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t->path == NULL || t->buffer == NULL) continue;
        gchar* key = t == origin ? NULL : server_key(ci, t, NULL, NULL);
        if (t == origin || g_strcmp0(key, origin_key) == 0) {
            gchar* path = g_canonicalize_filename(t->path, NULL);
            gchar* text = buffer_text(t->buffer);
            lsp_client_sync_document(s->client, path, text);
            g_free(text);
            g_hash_table_add(open, path);
        }
        g_free(key);
    }
    GList* docs = lsp_client_get_documents(s->client);
    GPtrArray* closed = g_ptr_array_new_with_free_func(g_free);
    for (GList* l = docs; l != NULL; l = l->next) {
        if (!g_hash_table_contains(open, l->data)) g_ptr_array_add(closed, g_strdup(l->data));
    }
    g_list_free(docs);
    for (guint i = 0; i < closed->len; i++) lsp_client_close_document(s->client, g_ptr_array_index(closed, i));
    g_ptr_array_unref(closed);
    g_hash_table_destroy(open);
    g_free(origin_key);
}

/* Text of a line of path: from its open tab, else from the file */
static gchar* line_text(AppData* app, const gchar* path, gint line) {
    EditorTab* t = editor_find_tab(app, path);
    if (t != NULL && t->buffer != NULL) {
        if (line < 0 || line >= gtk_text_buffer_get_line_count(t->buffer)) return g_strdup("");
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_line(t->buffer, &s, line);
        e = s;
        if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
        return gtk_text_iter_get_slice(&s, &e);
    }
    gchar* data = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &data, &len, NULL)) return g_strdup("");
    const gchar* p = data;
    const gchar* end = data + len;
    for (gint i = 0; i < line && p < end; i++) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        p = nl != NULL ? nl + 1 : end;
    }
    const gchar* nl = memchr(p, '\n', (gsize)(end - p));
    gchar* out = g_strndup(p, nl != NULL ? (gsize)(nl - p) : (gsize)(end - p));
    g_free(data);
    return out;
}

static gboolean utf8_positions(LspClient* client) {
    const LspCapabilities* caps = lsp_client_get_capabilities(client);
    return caps != NULL && caps->utf8_positions;
}

static void on_answer(LspClient* client, const LspMessage* msg, gpointer data) {
    Request* r = data;
    AppData* app = r->app;
    if (!tab_alive(app, r->tab)) return;
    gboolean utf8 = utf8_positions(client);
    const JsonValue* result = msg != NULL ? msg->result : NULL;
    switch (r->kind) {
        case REQ_LOCATIONS: {
            GPtrArray* locs = msg != NULL ? lsp_parse_locations(result) : NULL;
            for (guint i = 0; locs != NULL && i < locs->len; i++) {
                LspLocation* loc = g_ptr_array_index(locs, i);
                gchar* text = line_text(app, loc->path, loc->line);
                loc->column = lsp_column_to_byte(text, -1, loc->column, utf8);
                g_free(text);
            }
            ((CodeIntelLocationsFunc)r->func)(app, r->tab, locs, r->data);
            if (locs != NULL) g_ptr_array_unref(locs);
            break;
        }
        case REQ_HOVER: {
            gchar* text = msg != NULL ? lsp_parse_hover(result) : NULL;
            ((CodeIntelTextFunc)r->func)(app, r->tab, text, r->data);
            g_free(text);
            break;
        }
        case REQ_SIGNATURE: {
            LspSignature* sig = msg != NULL ? lsp_parse_signature_help(result) : NULL;
            ((CodeIntelSignatureFunc)r->func)(app, r->tab, sig, r->data);
            lsp_signature_free(sig);
            break;
        }
        case REQ_SYMBOLS: {
            GPtrArray* syms = msg != NULL ? lsp_parse_symbols(result) : NULL;
            gchar* path = g_canonicalize_filename(r->tab->path, NULL);
            for (guint i = 0; syms != NULL && i < syms->len; i++) {
                LspSymbol* s = g_ptr_array_index(syms, i);
                gchar* text = line_text(app, s->path != NULL ? s->path : path, s->line);
                s->column = lsp_column_to_byte(text, -1, s->column, utf8);
                g_free(text);
            }
            g_free(path);
            ((CodeIntelSymbolsFunc)r->func)(app, r->tab, syms, r->data);
            if (syms != NULL) g_ptr_array_unref(syms);
            break;
        }
    }
}

/* Is the capability provided (unknown while the server starts: try) */
typedef enum {
    CAP_DEFINITION,
    CAP_DECLARATION,
    CAP_REFERENCES,
    CAP_HOVER,
    CAP_SIGNATURE,
    CAP_SYMBOLS
} Capability;

static gboolean has_capability(Server* s, Capability cap) {
    if (lsp_client_get_state(s->client) == LSP_CLIENT_STARTING) return TRUE;
    const LspCapabilities* c = lsp_client_get_capabilities(s->client);
    if (c == NULL) return FALSE;
    switch (cap) {
        case CAP_DEFINITION: return c->definition;
        case CAP_DECLARATION: return c->declaration;
        case CAP_REFERENCES: return c->references;
        case CAP_HOVER: return c->hover;
        case CAP_SIGNATURE: return c->signature_help;
        case CAP_SYMBOLS: return c->document_symbol;
    }
    return FALSE;
}

static gboolean request(AppData* app, EditorTab* tab, Capability cap, const gchar* method, gint line, gint index,
                        gint include_declaration, RequestKind kind, gpointer func, gpointer data) {
    if (app == NULL || tab == NULL || tab->path == NULL || tab->buffer == NULL) return FALSE;
    Server* s = server_for(app, tab, TRUE);
    if (s == NULL || !has_capability(s, cap)) return FALSE;
    sync_documents(app, s, tab);
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    gchar* params;
    if (kind == REQ_SYMBOLS) {
        params = lsp_params_document(path);
    } else {
        gchar* text = line_text(app, path, line);
        gint column = lsp_byte_to_column(text, -1, index, utf8_positions(s->client));
        g_free(text);
        params = lsp_params_position(path, line, column, include_declaration);
    }
    Request* r = g_new0(Request, 1);
    r->app = app;
    r->tab = tab;
    r->kind = kind;
    r->func = func;
    r->data = data;
    gint64 id = lsp_client_request(s->client, method, params, on_answer, r, g_free);
    g_free(params);
    g_free(path);
    return id > 0;
}

gboolean code_intel_definition(AppData* app, EditorTab* tab, gint line, gint index, gboolean declaration,
                               CodeIntelLocationsFunc func, gpointer data) {
    return request(app, tab, declaration ? CAP_DECLARATION : CAP_DEFINITION,
                   declaration ? "textDocument/declaration" : "textDocument/definition", line, index, -1,
                   REQ_LOCATIONS, (gpointer)func, data);
}

gboolean code_intel_references(AppData* app, EditorTab* tab, gint line, gint index, CodeIntelLocationsFunc func,
                               gpointer data) {
    return request(app, tab, CAP_REFERENCES, "textDocument/references", line, index, 1, REQ_LOCATIONS,
                   (gpointer)func, data);
}

gboolean code_intel_hover(AppData* app, EditorTab* tab, gint line, gint index, CodeIntelTextFunc func,
                          gpointer data) {
    return request(app, tab, CAP_HOVER, "textDocument/hover", line, index, -1, REQ_HOVER, (gpointer)func, data);
}

gboolean code_intel_signature(AppData* app, EditorTab* tab, gint line, gint index, CodeIntelSignatureFunc func,
                              gpointer data) {
    return request(app, tab, CAP_SIGNATURE, "textDocument/signatureHelp", line, index, -1, REQ_SIGNATURE,
                   (gpointer)func, data);
}

gboolean code_intel_document_symbols(AppData* app, EditorTab* tab, CodeIntelSymbolsFunc func, gpointer data) {
    return request(app, tab, CAP_SYMBOLS, "textDocument/documentSymbol", 0, 0, -1, REQ_SYMBOLS, (gpointer)func,
                   data);
}

const gchar* code_intel_signature_triggers(AppData* app, EditorTab* tab) {
    Server* s = app != NULL ? server_for(app, tab, FALSE) : NULL;
    const LspCapabilities* c = s != NULL ? lsp_client_get_capabilities(s->client) : NULL;
    if (c != NULL && c->signature_triggers != NULL && c->signature_triggers[0] != '\0') return c->signature_triggers;
    return "(,";
}

static void count_diagnostics(AppData* app, EditorTab* tab, guint* errors, guint* warnings);

gchar* code_intel_server_label(AppData* app, EditorTab* tab) {
    if (app == NULL || app->code_intel == NULL) return NULL;
    Server* s = server_for(app, tab, FALSE);
    if (s == NULL) return NULL;
    switch (lsp_client_get_state(s->client)) {
        case LSP_CLIENT_READY: {
            guint errors = 0, warnings = 0;
            count_diagnostics(app, tab, &errors, &warnings);
            if (errors == 0 && warnings == 0) return g_strdup(lsp_client_get_name(s->client));
            GString* label = g_string_new(lsp_client_get_name(s->client));
            g_string_append(label, ":");
            if (errors > 0) g_string_append_printf(label, " %u error%s", errors, errors > 1 ? "s" : "");
            if (warnings > 0) {
                g_string_append_printf(label, "%s %u warning%s", errors > 0 ? "," : "", warnings, warnings > 1 ? "s" : "");
            }
            return g_string_free(label, FALSE);
        }
        case LSP_CLIENT_STARTING: return g_strdup_printf("%s (starting)", s->name);
        case LSP_CLIENT_FAILED: break;
    }
    return NULL;
}

void code_intel_shutdown(AppData* app) {
    if (app == NULL || app->code_intel == NULL) return;
    CodeIntel* ci = app->code_intel;
    g_hash_table_destroy(ci->servers);
    g_hash_table_destroy(ci->commands);
    g_hash_table_destroy(ci->diagnostics);
    g_free(ci);
    app->code_intel = NULL;
}

/* ------------------------------------------------------------------------- */
/* Diagnostics and document follow-up                                        */
/* ------------------------------------------------------------------------- */

static const gchar* const DIAG_TAGS[] = {NULL, "lsp-error", "lsp-warning", "lsp-info", "lsp-hint"};

static void ensure_diag_tags(GtkTextBuffer* buf) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    if (gtk_text_tag_table_lookup(table, "lsp-error") != NULL) return;
    static const gchar* const COLORS[] = {NULL, "@error", "@warning", "@link", "@link"};
    for (gint i = 1; i <= 4; i++) {
        /* A plain line: the error (dotted) style is barely visible in GTK 4 */
        text_tag_create(buf, DIAG_TAGS[i], "underline", PANGO_UNDERLINE_SINGLE, "underline-rgba", COLORS[i], NULL);
    }
}

static void iter_at(GtkTextBuffer* buf, GtkTextIter* it, gint line, gint index) {
    gint lines = gtk_text_buffer_get_line_count(buf);
    gtk_text_buffer_get_iter_at_line(buf, it, CLAMP(line, 0, lines - 1));
    GtkTextIter eol = *it;
    if (!gtk_text_iter_ends_line(&eol)) gtk_text_iter_forward_to_line_end(&eol);
    gtk_text_iter_set_line_index(it, CLAMP(index, 0, gtk_text_iter_get_line_index(&eol)));
}

/* Underline the diagnostics of path in its open tab */
static void apply_diagnostics(AppData* app, const gchar* path) {
    EditorTab* tab = editor_find_tab(app, path);
    if (tab == NULL || tab->buffer == NULL) return;
    GtkTextBuffer* buf = tab->buffer;
    ensure_diag_tags(buf);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    for (gint i = 1; i <= 4; i++) gtk_text_buffer_remove_tag_by_name(buf, DIAG_TAGS[i], &s, &e);
    GPtrArray* diags = g_hash_table_lookup(intel_of(app)->diagnostics, path);
    for (guint i = 0; diags != NULL && i < diags->len; i++) {
        LspDiagnostic* d = g_ptr_array_index(diags, i);
        GtkTextIter a, b;
        iter_at(buf, &a, d->line, d->column);
        iter_at(buf, &b, d->end_line, d->end_column);
        if (gtk_text_iter_equal(&a, &b)) {
            /* Empty range: the word there, or one character */
            if (gtk_text_iter_inside_word(&b) || gtk_text_iter_starts_word(&b)) gtk_text_iter_forward_word_end(&b);
            else gtk_text_iter_forward_char(&b);
        }
        gtk_text_buffer_apply_tag_by_name(buf, DIAG_TAGS[d->severity], &a, &b);
    }
}

static void on_server_notify(LspClient* client, const LspMessage* msg, gpointer data) {
    AppData* app = data;
    if (g_strcmp0(msg->method, "textDocument/publishDiagnostics") != 0) return;
    gchar* path = NULL;
    GPtrArray* diags = lsp_parse_diagnostics(msg->params, &path);
    if (diags == NULL) {
        g_free(path);
        return;
    }
    gchar* canon = g_canonicalize_filename(path, NULL);
    g_free(path);
    gboolean utf8 = utf8_positions(client);
    for (guint i = 0; i < diags->len; i++) {
        LspDiagnostic* d = g_ptr_array_index(diags, i);
        gchar* text = line_text(app, canon, d->line);
        d->column = lsp_column_to_byte(text, -1, d->column, utf8);
        g_free(text);
        text = line_text(app, canon, d->end_line);
        d->end_column = lsp_column_to_byte(text, -1, d->end_column, utf8);
        g_free(text);
    }
    g_hash_table_replace(intel_of(app)->diagnostics, g_strdup(canon), diags);
    apply_diagnostics(app, canon);
    g_free(canon);
    app_emit(app, APP_EVENT_STATUS_CHANGED);
}

gchar* code_intel_diagnostics_at(AppData* app, EditorTab* tab, gint line, gint index) {
    if (app == NULL || app->code_intel == NULL || tab == NULL || tab->path == NULL) return NULL;
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    GPtrArray* diags = g_hash_table_lookup(intel_of(app)->diagnostics, path);
    g_free(path);
    GString* out = NULL;
    static const gchar* const NAMES[] = {NULL, "error", "warning", "info", "hint"};
    for (guint i = 0; diags != NULL && i < diags->len; i++) {
        LspDiagnostic* d = g_ptr_array_index(diags, i);
        gboolean after_start = line > d->line || (line == d->line && index >= d->column);
        gboolean before_end = line < d->end_line || (line == d->end_line && index <= MAX(d->end_column, d->column + 1));
        if (!after_start || !before_end) continue;
        if (out == NULL) out = g_string_new(NULL);
        else g_string_append(out, "\n\n");
        g_string_append_printf(out, "**%s**%s%s%s: ", NAMES[d->severity], d->source != NULL ? " (" : "",
                               d->source != NULL ? d->source : "", d->source != NULL ? ")" : "");
        /* Plain text in Markdown: escape the special characters */
        for (const gchar* p = d->message; *p != '\0'; p++) {
            if (strchr("\\`*_[]<>#|", *p) != NULL) g_string_append_c(out, '\\');
            g_string_append_c(out, *p);
        }
    }
    return out != NULL ? g_string_free(out, FALSE) : NULL;
}

/* Error and warning counts of a file */
static void count_diagnostics(AppData* app, EditorTab* tab, guint* errors, guint* warnings) {
    *errors = *warnings = 0;
    if (app->code_intel == NULL || tab == NULL || tab->path == NULL) return;
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    GPtrArray* diags = g_hash_table_lookup(intel_of(app)->diagnostics, path);
    g_free(path);
    for (guint i = 0; diags != NULL && i < diags->len; i++) {
        LspDiagnostic* d = g_ptr_array_index(diags, i);
        if (d->severity == LSP_SEVERITY_ERROR) (*errors)++;
        else if (d->severity == LSP_SEVERITY_WARNING) (*warnings)++;
    }
}

typedef struct {
    AppData* app;
    EditorTab* tab;
    guint timer;
} SyncState;

static gboolean on_sync_timer(gpointer data) {
    SyncState* ss = data;
    ss->timer = 0;
    if (!tab_alive(ss->app, ss->tab)) return G_SOURCE_REMOVE;
    Server* s = server_for(ss->app, ss->tab, TRUE);
    if (s != NULL) sync_documents(ss->app, s, ss->tab);
    return G_SOURCE_REMOVE;
}

static void schedule_sync(SyncState* ss, guint delay) {
    if (ss->timer != 0) g_source_remove(ss->timer);
    ss->timer = g_timeout_add(delay, on_sync_timer, ss);
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    SyncState* ss = data;
    /* Only once a server runs for the tab: edits never start one */
    if (server_for(ss->app, ss->tab, FALSE) != NULL) schedule_sync(ss, SYNC_DELAY_MS);
}

static void sync_state_free(gpointer p) {
    SyncState* ss = p;
    if (ss->timer != 0) g_source_remove(ss->timer);
    g_free(ss);
}

void code_intel_attach(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || tab->path == NULL) return;
    if (g_object_get_data(G_OBJECT(tab->buffer), SYNC_TIMER_KEY) != NULL) return;
    SyncState* ss = g_new0(SyncState, 1);
    ss->app = app;
    ss->tab = tab;
    g_object_set_data_full(G_OBJECT(tab->buffer), SYNC_TIMER_KEY, ss, sync_state_free);
    g_signal_connect(tab->buffer, "changed", G_CALLBACK(on_buffer_changed), ss);
    /* A source file with a server: start it now, its diagnostics follow */
    if (tab_lang(tab) != CODE_LANG_NONE && command_of(intel_of(app), tab_lang(tab)) != NULL) schedule_sync(ss, 50);
}
