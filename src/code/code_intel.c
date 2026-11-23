/* Language servers for code navigation (see code_intel.h) */
#include "code/code_intel.h"
#include "core/vfs.h"
#include "code/code_lint.h"
#include "core/app_events.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "theme/text_tags.h"
#include "code/code_index.h"
#include "editor/editor.h"
#include "code/lsp_client.h"
#include "code/lsp_config.h"
#include "core/ssh_session.h"
#include "git/git_state.h"
#include <string.h>

#ifdef HAVE_LIBSSH
#include "core/ssh_trace.h"
/* Why a remote server does not start, in LOOKINFILES_SSH_LOG */
#define REMOTE_TRACE(...) ssh_trace(__VA_ARGS__)
#else
#define REMOTE_TRACE(...) ((void)0)
#endif

#define RESTART_DELAY_US (60 * G_USEC_PER_SEC)
#define SYNC_DELAY_MS 600
#define SYNC_TIMER_KEY "code-intel-sync"

typedef struct {
    LspClient* client;
    gchar* name;        /* program name, until the server says its own */
    gint64 failed_at;   /* monotonic time of the failure, 0 when running */
} Server;

typedef struct CodeIntel {
    GHashTable* servers;   /* "root\ncommand" -> Server* */
    GHashTable* commands;  /* CodeLang (as pointer) -> gchar** argv, or a NULL value: none */
    GHashTable* diagnostics; /* canonical path -> GPtrArray of LspDiagnostic (byte columns) */
    GHashTable* task_diagnostics; /* the same, of the last build or test task (code_intel_set_task_diagnostics()) */
    GHashTable* remote_commands; /* CodeLang -> gchar** command lines tried on a remote machine, or NULL */
    GHashTable* remote_roots;    /* a machine's folder seen here -> its absolute path there */
} CodeIntel;

static void on_server_notify(LspClient* client, const LspMessage* msg, gpointer data);

typedef enum {
    REQ_LOCATIONS,
    REQ_HOVER,
    REQ_SIGNATURE,
    REQ_SYMBOLS,
    REQ_COMPLETION,
    REQ_PREPARE_RENAME,
    REQ_RENAME
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
        ci->task_diagnostics = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
        ci->remote_commands = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_strfreev);
        ci->remote_roots = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
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

/* Server command lines of a language for a remote machine, which looks for
 * the programs itself (looked up once per session) */
static gchar** remote_command_of(CodeIntel* ci, CodeLang lang) {
    gpointer key = GINT_TO_POINTER(lang);
    gpointer value = NULL;
    if (g_hash_table_lookup_extended(ci->remote_commands, key, NULL, &value)) return value;
    gchar* dir = app_config_dir();
    gchar* ini = g_build_filename(dir, "lsp.ini", NULL);
    gchar** lines = lsp_config_command_lines(lang, ini);
    g_free(ini);
    g_free(dir);
    g_hash_table_insert(ci->remote_commands, key, lines);
    return lines;
}

/* The commands of tab's language: argv here, the command lines to try on a remote machine */
static gchar** commands_for(CodeIntel* ci, EditorTab* tab) {
    CodeLang lang = tab_lang(tab);
    if (lang == CODE_LANG_NONE) return NULL;
    return vfs_is_remote(tab->path) ? remote_command_of(ci, lang) : command_of(ci, lang);
}

/* The project of a remote machine's file: the repository the explorer read
 * there (git status), else its folder */
static gchar* remote_project_root(EditorTab* tab) {
    const GitState* git = tab->app != NULL ? tab->app->git_state : NULL;
    if (git_state_covers(git, tab->path)) return g_strdup(git_state_root(git));
    return g_path_get_dirname(tab->path);
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
    gchar** argv = commands_for(ci, tab);
    if (argv == NULL) return NULL;
    gchar* root = vfs_is_remote(tab->path) ? remote_project_root(tab) : code_index_project_root(tab->path);
    if (root == NULL) return NULL;
    gchar* cmd = g_strjoinv(" ", argv);
    gchar* key = g_strdup_printf("%s\n%s", root, cmd);
    g_free(cmd);
    if (argv_out != NULL) *argv_out = argv;
    if (root_out != NULL) *root_out = root;
    else g_free(root);
    return key;
}

/* A server on a remote machine for the project at root: the first of the
 * command lines installed there, run through ssh in a login shell (its
 * PATH: ~/.local/bin...), the paths of the messages mapped between the
 * machine's folder here and there. NULL when it cannot start. */
static LspClient* start_remote(CodeIntel* ci, gchar** lines, const gchar* root, AppData* app) {
    gchar* mount = vfs_mount_of(root);
    if (mount == NULL) {
        REMOTE_TRACE("lsp: the project is on no mounted machine");
        return NULL;
    }
    const gchar* there = g_hash_table_lookup(ci->remote_roots, mount);
    if (there == NULL) {
        GError* error = NULL;
        gchar* abs = vfs_remote_absolute(mount, NULL, &error); /* one round trip, once per machine */
        if (abs == NULL) {
            REMOTE_TRACE("lsp: the machine's folder is unknown: %s", error != NULL ? error->message : "-");
            g_clear_error(&error);
            g_free(mount);
            return NULL;
        }
        g_hash_table_insert(ci->remote_roots, g_strdup(mount), abs);
        there = abs;
    }
    gchar* script = lsp_config_launch_script(lines);
    gchar* q = ssh_session_quote(script);
    gchar* command = g_strdup_printf("exec \"${SHELL:-/bin/sh}\" -lc %s", q);
    GError* error = NULL;
    GPtrArray* argv = vfs_command_argv(root, command, &error);
    REMOTE_TRACE("lsp: starting a remote server (%u command lines)%s%s", lines != NULL ? g_strv_length(lines) : 0,
                 error != NULL ? ": " : "", error != NULL ? error->message : "");
    g_clear_error(&error);
    LspClient* client = NULL;
    if (argv != NULL) {
        g_ptr_array_add(argv, NULL);
        client = lsp_client_new_mapped((gchar**)argv->pdata, root, mount, there, on_server_state, app, NULL);
        g_ptr_array_unref(argv);
    }
    g_free(command);
    g_free(q);
    g_free(script);
    g_free(mount);
    return client;
}

/* The server of a tab, started when needed (start); NULL when none runs */
static Server* server_for(AppData* app, EditorTab* tab, gboolean start) {
    CodeIntel* ci = intel_of(app);
    gchar** argv = NULL;
    gchar* root = NULL;
    gchar* key = server_key(ci, tab, &argv, &root);
    if (key == NULL) {
        if (start && tab != NULL && vfs_is_remote(tab->path)) {
            REMOTE_TRACE("lsp: no server for this remote file (language %d)", (gint)tab_lang(tab));
        }
        return NULL;
    }
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
        if (vfs_is_remote(tab->path) && !vfs_is_remote(root)) {
            REMOTE_TRACE("lsp: the project of a remote file is not on its machine");
        }
        LspClient* client = vfs_is_remote(root) ? start_remote(ci, argv, root, app)
                                                : lsp_client_new(argv, root, on_server_state, app, NULL);
        lsp_client_set_notify_func(client, on_server_notify, app);
        s = g_new0(Server, 1);
        s->client = client;
        gchar** words = vfs_is_remote(root) ? g_strsplit(argv[0], " ", 2) : NULL; /* a remote command line */
        s->name = g_path_get_basename(words != NULL ? words[0] : argv[0]);
        g_strfreev(words);
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
    /* origin may not be in the list yet (a tab being built) */
    GList* tabs = g_list_find(app->editors, origin) != NULL ? g_list_copy(app->editors)
                                                            : g_list_prepend(g_list_copy(app->editors), origin);
    for (GList* l = tabs; l != NULL; l = l->next) {
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
    g_list_free(tabs);
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
        case REQ_COMPLETION: {
            LspCompletionList* cl = msg != NULL ? lsp_parse_completions(result) : NULL;
            gchar* path = g_canonicalize_filename(r->tab->path, NULL);
            for (guint i = 0; cl != NULL && i < cl->items->len; i++) {
                LspCompletionItem* it = g_ptr_array_index(cl->items, i);
                if (!it->has_edit) continue;
                gchar* text = line_text(app, path, it->edit_start_line);
                it->edit_start_col = lsp_column_to_byte(text, -1, it->edit_start_col, utf8);
                g_free(text);
                text = line_text(app, path, it->edit_end_line);
                it->edit_end_col = lsp_column_to_byte(text, -1, it->edit_end_col, utf8);
                g_free(text);
            }
            g_free(path);
            ((CodeIntelCompletionFunc)r->func)(app, r->tab, cl, r->data);
            lsp_completion_list_free(cl);
            break;
        }
        case REQ_PREPARE_RENAME: {
            gint sl = -1, sc = -1, el = -1, ec = -1;
            gchar* placeholder = NULL;
            gboolean ok = msg != NULL && lsp_parse_prepare_rename(result, &sl, &sc, &el, &ec, &placeholder);
            if (ok && sl >= 0) {
                gchar* path = g_canonicalize_filename(r->tab->path, NULL);
                gchar* text = line_text(app, path, sl);
                sc = lsp_column_to_byte(text, -1, sc, utf8);
                g_free(text);
                text = line_text(app, path, el);
                ec = lsp_column_to_byte(text, -1, ec, utf8);
                g_free(text);
                g_free(path);
            }
            ((CodeIntelPrepareRenameFunc)r->func)(app, r->tab, ok, placeholder, sl, sc, el, ec, r->data);
            g_free(placeholder);
            break;
        }
        case REQ_RENAME: {
            LspWorkspaceEdit* edit = msg != NULL ? lsp_parse_workspace_edit(result) : NULL;
            ((CodeIntelWorkspaceEditFunc)r->func)(app, r->tab, edit, r->data);
            lsp_workspace_edit_free(edit);
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
    CAP_SYMBOLS,
    CAP_WORKSPACE_SYMBOLS,
    CAP_COMPLETION,
    CAP_RENAME,
    CAP_PREPARE_RENAME
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
        case CAP_WORKSPACE_SYMBOLS: return c->workspace_symbol;
        case CAP_COMPLETION: return c->completion;
        case CAP_RENAME: return c->rename;
        case CAP_PREPARE_RENAME: return c->prepare_rename;
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

static gint64 send_custom_request(AppData* app, EditorTab* tab, Capability cap, const gchar* method,
                                  const gchar* params, RequestKind kind, gpointer func, gpointer data) {
    if (app == NULL || tab == NULL || tab->path == NULL || tab->buffer == NULL) return 0;
    Server* s = server_for(app, tab, TRUE);
    if (s == NULL || !has_capability(s, cap)) return 0;
    sync_documents(app, s, tab);
    Request* r = g_new0(Request, 1);
    r->app = app;
    r->tab = tab;
    r->kind = kind;
    r->func = func;
    r->data = data;
    return lsp_client_request(s->client, method, params, on_answer, r, g_free);
}

gint64 code_intel_workspace_symbols(AppData* app, EditorTab* tab, const gchar* query, CodeIntelSymbolsFunc func,
                                    gpointer data) {
    gchar* params = lsp_params_workspace_symbol(query != NULL ? query : "");
    gint64 id = send_custom_request(app, tab, CAP_WORKSPACE_SYMBOLS, "workspace/symbol", params, REQ_SYMBOLS,
                                    (gpointer)func, data);
    g_free(params);
    return id;
}

gint64 code_intel_completion(AppData* app, EditorTab* tab, gint line, gint index, const gchar* trigger_char,
                             CodeIntelCompletionFunc func, gpointer data) {
    if (app == NULL || tab == NULL || tab->path == NULL || tab->buffer == NULL) return 0;
    Server* s = server_for(app, tab, FALSE);
    if (s == NULL || !has_capability(s, CAP_COMPLETION)) return 0;
    sync_documents(app, s, tab);
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    gchar* text = line_text(app, path, line);
    gint column = lsp_byte_to_column(text, -1, index, utf8_positions(s->client));
    g_free(text);
    gchar* params = lsp_params_completion(path, line, column, trigger_char);
    g_free(path);
    Request* r = g_new0(Request, 1);
    r->app = app;
    r->tab = tab;
    r->kind = REQ_COMPLETION;
    r->func = (gpointer)func;
    r->data = data;
    gint64 id = lsp_client_request(s->client, "textDocument/completion", params, on_answer, r, g_free);
    g_free(params);
    return id;
}

void code_intel_cancel(AppData* app, EditorTab* tab, gint64 req_id) {
    if (app == NULL || tab == NULL || req_id <= 0) return;
    Server* s = server_for(app, tab, FALSE);
    if (s != NULL) lsp_client_cancel(s->client, req_id);
}

gboolean code_intel_supports_rename(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL || tab->path == NULL) return FALSE;
    Server* s = server_for(app, tab, FALSE);
    return s != NULL && lsp_client_get_state(s->client) == LSP_CLIENT_READY && has_capability(s, CAP_RENAME);
}

gboolean code_intel_utf8_positions(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL) return FALSE;
    Server* s = server_for(app, tab, FALSE);
    return s != NULL && utf8_positions(s->client);
}

gboolean code_intel_prepare_rename(AppData* app, EditorTab* tab, gint line, gint index,
                                   CodeIntelPrepareRenameFunc func, gpointer data) {
    if (app == NULL || tab == NULL || tab->path == NULL || tab->buffer == NULL) return FALSE;
    Server* s = server_for(app, tab, FALSE);
    if (s == NULL || lsp_client_get_state(s->client) != LSP_CLIENT_READY || !has_capability(s, CAP_PREPARE_RENAME)) {
        return FALSE;
    }
    return request(app, tab, CAP_PREPARE_RENAME, "textDocument/prepareRename", line, index, -1,
                   REQ_PREPARE_RENAME, (gpointer)func, data);
}

gboolean code_intel_rename(AppData* app, EditorTab* tab, gint line, gint index, const gchar* new_name,
                           CodeIntelWorkspaceEditFunc func, gpointer data) {
    if (app == NULL || tab == NULL || tab->path == NULL || tab->buffer == NULL || new_name == NULL) return FALSE;
    Server* s = server_for(app, tab, FALSE);
    if (s == NULL || !has_capability(s, CAP_RENAME)) return FALSE;
    sync_documents(app, s, tab);
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    gchar* text = line_text(app, path, line);
    gint column = lsp_byte_to_column(text, -1, index, utf8_positions(s->client));
    g_free(text);
    gchar* params = lsp_params_rename(path, line, column, new_name);
    g_free(path);
    Request* r = g_new0(Request, 1);
    r->app = app;
    r->tab = tab;
    r->kind = REQ_RENAME;
    r->func = (gpointer)func;
    r->data = data;
    gint64 id = lsp_client_request(s->client, "textDocument/rename", params, on_answer, r, g_free);
    g_free(params);
    return id > 0;
}

const gchar* code_intel_signature_triggers(AppData* app, EditorTab* tab) {
    Server* s = app != NULL ? server_for(app, tab, FALSE) : NULL;
    const LspCapabilities* c = s != NULL ? lsp_client_get_capabilities(s->client) : NULL;
    if (c != NULL && c->signature_triggers != NULL && c->signature_triggers[0] != '\0') return c->signature_triggers;
    return "(,";
}

const gchar* code_intel_completion_triggers(AppData* app, EditorTab* tab) {
    Server* s = app != NULL ? server_for(app, tab, FALSE) : NULL;
    const LspCapabilities* c = s != NULL ? lsp_client_get_capabilities(s->client) : NULL;
    if (c != NULL && c->completion_triggers != NULL && c->completion_triggers[0] != '\0') {
        return c->completion_triggers;
    }
    return NULL;
}

static void count_diagnostics(AppData* app, EditorTab* tab, guint* errors, guint* warnings);

gchar* code_intel_server_status(AppData* app, EditorTab* tab, guint* errors, guint* warnings) {
    *errors = *warnings = 0;
    if (app == NULL || app->code_intel == NULL) return NULL;
    Server* s = server_for(app, tab, FALSE);
    if (s != NULL) {
        switch (lsp_client_get_state(s->client)) {
            case LSP_CLIENT_READY:
                count_diagnostics(app, tab, errors, warnings);
                return g_strdup(lsp_client_get_name(s->client));
            case LSP_CLIENT_STARTING: return g_strdup_printf("%s (starting)", s->name);
            case LSP_CLIENT_FAILED: break;
        }
    }
    if (tab != NULL && tab->path != NULL && code_lint_source_name(tab->path) != NULL) {
        count_diagnostics(app, tab, errors, warnings);
        return g_strdup(_("built-in"));
    }
    /* A server that could not start or stopped: say so, not nothing */
    gchar* key = tab != NULL && tab->path != NULL ? server_key(intel_of(app), tab, NULL, NULL) : NULL;
    Server* failed = key != NULL ? g_hash_table_lookup(intel_of(app)->servers, key) : NULL;
    g_free(key);
    if (failed != NULL) return g_strdup_printf(_("%s (not running)"), failed->name);
    return NULL;
}

gchar* code_intel_server_label(AppData* app, EditorTab* tab) {
    guint errors = 0, warnings = 0;
    gchar* name = code_intel_server_status(app, tab, &errors, &warnings);
    if (name == NULL || (errors == 0 && warnings == 0)) return name;
    GString* label = g_string_new(name);
    g_free(name);
    g_string_append(label, ":");
    if (errors > 0) g_string_append_printf(label, " %u error%s", errors, errors > 1 ? "s" : "");
    if (warnings > 0) {
        g_string_append_printf(label, "%s %u warning%s", errors > 0 ? "," : "", warnings, warnings > 1 ? "s" : "");
    }
    return g_string_free(label, FALSE);
}

void code_intel_shutdown(AppData* app) {
    if (app == NULL || app->code_intel == NULL) return;
    CodeIntel* ci = app->code_intel;
    g_hash_table_destroy(ci->servers);
    g_hash_table_destroy(ci->commands);
    g_hash_table_destroy(ci->diagnostics);
    g_hash_table_destroy(ci->task_diagnostics);
    g_hash_table_destroy(ci->remote_commands);
    g_hash_table_destroy(ci->remote_roots);
    g_free(ci);
    app->code_intel = NULL;
}

/* ------------------------------------------------------------------------- */
/* Diagnostics and document follow-up                                        */
/* ------------------------------------------------------------------------- */

static const gchar* const DIAG_TAGS[] = {NULL, "lsp-error", "lsp-warning", "lsp-info", "lsp-hint"};
#define DIAG_ERROR_LINE_TAG "lsp-error-line"

static void ensure_diag_tags(GtkTextBuffer* buf) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    GtkTextTag* line_tag = gtk_text_tag_table_lookup(table, DIAG_ERROR_LINE_TAG);
    if (line_tag == NULL) {
        line_tag = text_tag_create(buf, DIAG_ERROR_LINE_TAG, "paragraph-background", "@diff_removed_bg", NULL);
    }
    if (gtk_text_tag_table_lookup(table, "lsp-error") == NULL) {
        text_tag_create(buf, "lsp-error", "underline", PANGO_UNDERLINE_DOUBLE,
                        "underline-rgba", "@error", NULL);
        text_tag_create(buf, "lsp-warning", "underline", PANGO_UNDERLINE_DOUBLE,
                        "underline-rgba", "@warning", "background", "@bg_code_inline", NULL);
        text_tag_create(buf, "lsp-info", "underline", PANGO_UNDERLINE_SINGLE,
                        "underline-rgba", "@link", NULL);
        text_tag_create(buf, "lsp-hint", "underline", PANGO_UNDERLINE_SINGLE,
                        "underline-rgba", "@link", NULL);
    }
    /* Keep diagnostic tags above GtkSourceBuffer syntax tags */
    gint top = gtk_text_tag_table_get_size(table) - 1;
    if (top > 0) {
        gtk_text_tag_set_priority(line_tag, top);
        for (gint i = 4; i >= 1; i--) {
            GtkTextTag* t = gtk_text_tag_table_lookup(table, DIAG_TAGS[i]);
            if (t != NULL) gtk_text_tag_set_priority(t, top);
        }
    }
}

static void iter_at(GtkTextBuffer* buf, GtkTextIter* it, gint line, gint index) {
    gint lines = gtk_text_buffer_get_line_count(buf);
    gtk_text_buffer_get_iter_at_line(buf, it, CLAMP(line, 0, lines - 1));
    GtkTextIter eol = *it;
    if (!gtk_text_iter_ends_line(&eol)) gtk_text_iter_forward_to_line_end(&eol);
    gtk_text_iter_set_line_index(it, CLAMP(index, 0, gtk_text_iter_get_line_index(&eol)));
}

/* The diagnostics of path (canonical): the server's (or the built-in
 * checker's) and the last task's, except a task's on a line where the server
 * reports one of the same severity (the compiler and its server say the same
 * thing twice). A new array of borrowed diagnostics, NULL when there is none. */
static GPtrArray* file_diagnostics(AppData* app, const gchar* path) {
    CodeIntel* ci = intel_of(app);
    GPtrArray* own = g_hash_table_lookup(ci->diagnostics, path);
    GPtrArray* task = g_hash_table_lookup(ci->task_diagnostics, path);
    if ((own == NULL || own->len == 0) && (task == NULL || task->len == 0)) return NULL;
    GPtrArray* out = g_ptr_array_new();
    for (guint i = 0; own != NULL && i < own->len; i++) g_ptr_array_add(out, g_ptr_array_index(own, i));
    for (guint i = 0; task != NULL && i < task->len; i++) {
        LspDiagnostic* t = g_ptr_array_index(task, i);
        gboolean same = FALSE;
        for (guint j = 0; own != NULL && j < own->len && !same; j++) {
            const LspDiagnostic* d = g_ptr_array_index(own, j);
            same = d->line == t->line && d->severity == t->severity;
        }
        if (!same) g_ptr_array_add(out, t);
    }
    return out;
}

/* Highlight and underline the diagnostics of path in buffer */
static void apply_diagnostics_to_buffer(AppData* app, GtkTextBuffer* buf, const gchar* path) {
    if (app == NULL || buf == NULL || path == NULL) return;
    ensure_diag_tags(buf);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    gtk_text_buffer_remove_tag_by_name(buf, DIAG_ERROR_LINE_TAG, &s, &e);
    for (gint i = 1; i <= 4; i++) gtk_text_buffer_remove_tag_by_name(buf, DIAG_TAGS[i], &s, &e);
    gint lines = gtk_text_buffer_get_line_count(buf);
    GPtrArray* diags = file_diagnostics(app, path);
    for (guint i = 0; diags != NULL && i < diags->len; i++) {
        LspDiagnostic* d = g_ptr_array_index(diags, i);
        if (d->severity == LSP_SEVERITY_ERROR && lines > 0) {
            GtkTextIter ls, le;
            gtk_text_buffer_get_iter_at_line(buf, &ls, CLAMP(d->line, 0, lines - 1));
            le = ls;
            if (!gtk_text_iter_ends_line(&le)) gtk_text_iter_forward_to_line_end(&le);
            if (!gtk_text_iter_is_end(&le)) gtk_text_iter_forward_char(&le);
            gtk_text_buffer_apply_tag_by_name(buf, DIAG_ERROR_LINE_TAG, &ls, &le);
        }
        GtkTextIter a, b;
        iter_at(buf, &a, d->line, d->column);
        iter_at(buf, &b, d->end_line, d->end_column);
        if (gtk_text_iter_equal(&a, &b)) {
            /* Empty range: if at end-of-line (e.g. missing ';'), cover the
             * preceding visible word/char instead of an invisible newline */
            if (gtk_text_iter_ends_line(&a)) {
                if (gtk_text_iter_get_line_offset(&a) > 0) {
                    gtk_text_iter_backward_char(&a);
                    if (gtk_text_iter_inside_word(&a)) gtk_text_iter_backward_word_start(&a);
                }
            } else if (gtk_text_iter_inside_word(&b) || gtk_text_iter_starts_word(&b)) {
                gtk_text_iter_forward_word_end(&b);
            } else {
                gtk_text_iter_forward_char(&b);
            }
        }
        if (!gtk_text_iter_equal(&a, &b)) {
            gtk_text_buffer_apply_tag_by_name(buf, DIAG_TAGS[d->severity], &a, &b);
        }
    }
    if (diags != NULL) g_ptr_array_unref(diags);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->buffer == buf && t->minimap != NULL) {
            gtk_widget_queue_draw(t->minimap);
        }
    }
}

/* Highlight and underline the diagnostics of path in its open tab */
static void apply_diagnostics(AppData* app, const gchar* path) {
    EditorTab* tab = editor_find_tab(app, path);
    if (tab == NULL || tab->buffer == NULL) return;
    apply_diagnostics_to_buffer(app, tab->buffer, path);
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
    if (editor_find_tab(app, canon) == NULL) {
        g_ptr_array_unref(diags);
        if (g_hash_table_remove(intel_of(app)->diagnostics, canon)) {
            app_emit(app, APP_EVENT_STATUS_CHANGED);
            app_emit(app, APP_EVENT_DIAGNOSTICS_CHANGED);
        }
        g_free(canon);
        return;
    }
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
    app_emit(app, APP_EVENT_DIAGNOSTICS_CHANGED);
}

void code_diag_entry_free(CodeDiagEntry* entry) {
    if (entry == NULL) return;
    g_free(entry->path);
    g_free(entry->diag.message);
    g_free(entry->diag.source);
    g_free(entry);
}

static gint cmp_diag_entry(gconstpointer a, gconstpointer b) {
    const CodeDiagEntry* da = *(const CodeDiagEntry* const*)a;
    const CodeDiagEntry* db = *(const CodeDiagEntry* const*)b;
    gint p = g_strcmp0(da->path, db->path);
    if (p != 0) return p;
    if (da->diag.line != db->diag.line) return da->diag.line - db->diag.line;
    if (da->diag.column != db->diag.column) return da->diag.column - db->diag.column;
    return (gint)da->diag.severity - (gint)db->diag.severity;
}

GPtrArray* code_intel_all_diagnostics(AppData* app) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)code_diag_entry_free);
    if (app == NULL || app->code_intel == NULL) return out;
    GHashTable* paths = g_hash_table_new(g_str_hash, g_str_equal);
    GHashTable* tables[] = {intel_of(app)->diagnostics, intel_of(app)->task_diagnostics};
    for (guint t = 0; t < G_N_ELEMENTS(tables); t++) {
        GHashTableIter pit;
        gpointer path_key = NULL;
        g_hash_table_iter_init(&pit, tables[t]);
        while (g_hash_table_iter_next(&pit, &path_key, NULL)) g_hash_table_add(paths, path_key);
    }
    GHashTableIter it;
    gpointer key = NULL;
    g_hash_table_iter_init(&it, paths);
    while (g_hash_table_iter_next(&it, &key, NULL)) {
        const gchar* path = key;
        GPtrArray* list = file_diagnostics(app, path);
        for (guint i = 0; list != NULL && i < list->len; i++) {
            const LspDiagnostic* d = g_ptr_array_index(list, i);
            CodeDiagEntry* e = g_new0(CodeDiagEntry, 1);
            e->path = g_strdup(path);
            e->diag = *d;
            e->diag.message = g_strdup(d->message);
            e->diag.source = g_strdup(d->source);
            g_ptr_array_add(out, e);
        }
        if (list != NULL) g_ptr_array_unref(list);
    }
    g_hash_table_unref(paths);
    g_ptr_array_sort(out, cmp_diag_entry);
    return out;
}

static gint cmp_diag_pos(gconstpointer a, gconstpointer b) {
    const LspDiagnostic* da = *(const LspDiagnostic* const*)a;
    const LspDiagnostic* db = *(const LspDiagnostic* const*)b;
    if (da->line != db->line) return da->line - db->line;
    if (da->column != db->column) return da->column - db->column;
    return (gint)da->severity - (gint)db->severity;
}

gboolean code_intel_step_diagnostic(AppData* app, EditorTab* tab, gint line, gint index, gboolean forward,
                                    gint* out_line, gint* out_index, LspDiagnostic** out_diag) {
    if (out_diag != NULL) *out_diag = NULL;
    if (app == NULL || app->code_intel == NULL || tab == NULL || tab->path == NULL) return FALSE;
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    GPtrArray* sorted = file_diagnostics(app, path);
    g_free(path);
    if (sorted == NULL) return FALSE;
    g_ptr_array_sort(sorted, cmp_diag_pos);

    const LspDiagnostic* chosen = NULL;
    if (forward) {
        for (guint i = 0; i < sorted->len; i++) {
            const LspDiagnostic* d = g_ptr_array_index(sorted, i);
            if (d->line > line || (d->line == line && d->column > index)) {
                chosen = d;
                break;
            }
        }
        if (chosen == NULL) chosen = g_ptr_array_index(sorted, 0);
    } else {
        for (gint i = (gint)sorted->len - 1; i >= 0; i--) {
            const LspDiagnostic* d = g_ptr_array_index(sorted, (guint)i);
            if (d->line < line || (d->line == line && d->column < index)) {
                chosen = d;
                break;
            }
        }
        if (chosen == NULL) chosen = g_ptr_array_index(sorted, sorted->len - 1);
    }
    g_ptr_array_free(sorted, TRUE);
    if (chosen == NULL) return FALSE;

    if (out_line != NULL) *out_line = chosen->line;
    if (out_index != NULL) *out_index = chosen->column;
    if (out_diag != NULL) {
        LspDiagnostic* copy = g_new0(LspDiagnostic, 1);
        *copy = *chosen;
        copy->message = g_strdup(chosen->message);
        copy->source = g_strdup(chosen->source);
        *out_diag = copy;
    }
    return TRUE;
}

gchar* code_intel_diagnostics_at(AppData* app, EditorTab* tab, gint line, gint index) {
    if (app == NULL || app->code_intel == NULL || tab == NULL || tab->path == NULL) return NULL;
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    GPtrArray* diags = file_diagnostics(app, path);
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
    if (diags != NULL) g_ptr_array_unref(diags);
    return out != NULL ? g_string_free(out, FALSE) : NULL;
}

gchar* code_intel_line_diagnostic(AppData* app, EditorTab* tab, gint line, LspSeverity* out_severity) {
    if (out_severity != NULL) *out_severity = LSP_SEVERITY_ERROR;
    if (app == NULL || app->code_intel == NULL || tab == NULL || tab->path == NULL || line < 0) return NULL;
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    GPtrArray* diags = file_diagnostics(app, path);
    g_free(path);
    if (diags == NULL) return NULL;

    const LspDiagnostic* best = NULL;
    for (guint i = 0; i < diags->len; i++) {
        const LspDiagnostic* d = g_ptr_array_index(diags, i);
        if (line < d->line || line > d->end_line) continue;
        if (best == NULL || d->severity < best->severity ||
            (d->severity == best->severity && d->column < best->column)) {
            best = d;
        }
    }
    g_ptr_array_unref(diags); /* best stays: the tables own it */
    if (best == NULL || best->message == NULL) return NULL;
    if (out_severity != NULL) *out_severity = best->severity;

    GString* s = g_string_new(NULL);
    gboolean prev_sp = FALSE;
    for (const gchar* p = best->message; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            if (!prev_sp && s->len > 0) g_string_append_c(s, ' ');
            prev_sp = TRUE;
        } else {
            g_string_append_c(s, *p);
            prev_sp = (*p == ' ');
        }
    }
    if (best->source != NULL && best->source[0] != '\0') {
        g_string_append_printf(s, " [%s]", best->source);
    }
    return g_string_free(s, FALSE);
}

/* Error and warning counts of a file */
static void count_diagnostics(AppData* app, EditorTab* tab, guint* errors, guint* warnings) {
    *errors = *warnings = 0;
    if (app->code_intel == NULL || tab == NULL || tab->path == NULL) return;
    gchar* path = g_canonicalize_filename(tab->path, NULL);
    GPtrArray* diags = file_diagnostics(app, path);
    g_free(path);
    for (guint i = 0; diags != NULL && i < diags->len; i++) {
        LspDiagnostic* d = g_ptr_array_index(diags, i);
        if (d->severity == LSP_SEVERITY_ERROR) (*errors)++;
        else if (d->severity == LSP_SEVERITY_WARNING) (*warnings)++;
    }
    if (diags != NULL) g_ptr_array_unref(diags);
}

/* Kept on the buffer, which the views of a split file share: the tab is
 * looked up, any view of the buffer will do */
typedef struct {
    AppData* app;
    GtkTextBuffer* buffer;
    guint timer;
    GCancellable* lint_canc;
    guint lint_serial;
} SyncState;

typedef struct {
    AppData* app;
    GtkTextBuffer* buffer; /* weak-checked via sync_tab() on completion */
    gchar* canon_path;
    gchar* text;
    gsize len;
    guint serial;
    GPtrArray* diags;
} LintJob;

static void lint_job_free(gpointer data) {
    LintJob* j = data;
    g_free(j->canon_path);
    g_free(j->text);
    if (j->diags != NULL) g_ptr_array_unref(j->diags);
    g_free(j);
}

static EditorTab* sync_tab(SyncState* ss) {
    for (GList* l = ss->app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->buffer == ss->buffer) return t;
    }
    return NULL;
}

static void lint_worker(GTask* task, gpointer src, gpointer task_data, GCancellable* canc) {
    (void)src;
    LintJob* j = task_data;
    if (g_cancellable_is_cancelled(canc)) {
        g_task_return_boolean(task, FALSE);
        return;
    }
    j->diags = code_lint_check(j->canon_path, j->text, j->len);
    g_task_return_boolean(task, !g_cancellable_is_cancelled(canc));
}

static void on_lint_done(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src; (void)data;
    GTask* task = G_TASK(res);
    LintJob* j = g_task_get_task_data(task);
    if (j == NULL || j->app == NULL || j->app->code_intel == NULL) return;
    if (!g_task_propagate_boolean(task, NULL)) return;

    /* Find an open tab still using this buffer */
    EditorTab* tab = NULL;
    for (GList* l = j->app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->buffer == j->buffer) {
            tab = t;
            break;
        }
    }
    if (tab == NULL) return;
    SyncState* ss = g_object_get_data(G_OBJECT(j->buffer), SYNC_TIMER_KEY);
    if (ss == NULL || ss->lint_serial != j->serial) return;
    /* LSP always wins if a server started in the meantime */
    if (server_for(j->app, tab, FALSE) != NULL) return;

    GPtrArray* diags = j->diags;
    j->diags = NULL;
    g_hash_table_replace(intel_of(j->app)->diagnostics, g_strdup(j->canon_path), diags);
    apply_diagnostics_to_buffer(j->app, j->buffer, j->canon_path);
    app_emit(j->app, APP_EVENT_STATUS_CHANGED);
    app_emit(j->app, APP_EVENT_DIAGNOSTICS_CHANGED);
}

static void start_builtin_lint(SyncState* ss, EditorTab* tab) {
    if (tab == NULL || tab->path == NULL || tab->buffer == NULL) return;
    if (ss->lint_canc != NULL) {
        g_cancellable_cancel(ss->lint_canc);
        g_clear_object(&ss->lint_canc);
    }
    ss->lint_serial++;
    ss->lint_canc = g_cancellable_new();

    LintJob* j = g_new0(LintJob, 1);
    j->app = ss->app;
    j->buffer = ss->buffer;
    j->canon_path = g_canonicalize_filename(tab->path, NULL);
    j->text = buffer_text(tab->buffer);
    j->len = strlen(j->text);
    j->serial = ss->lint_serial;

    GTask* task = g_task_new(NULL, ss->lint_canc, on_lint_done, NULL);
    g_task_set_task_data(task, j, lint_job_free);
    g_task_run_in_thread(task, lint_worker);
    g_object_unref(task);
}

static gboolean on_sync_timer(gpointer data) {
    SyncState* ss = data;
    ss->timer = 0;
    EditorTab* tab = sync_tab(ss);
    if (tab == NULL) return G_SOURCE_REMOVE;
    Server* s = server_for(ss->app, tab, TRUE);
    if (s != NULL) {
        sync_documents(ss->app, s, tab);
    } else if (tab->path != NULL && code_lint_source_name(tab->path) != NULL) {
        start_builtin_lint(ss, tab);
    }
    return G_SOURCE_REMOVE;
}

static void schedule_sync(SyncState* ss, guint delay) {
    if (ss->timer != 0) g_source_remove(ss->timer);
    ss->timer = g_timeout_add(delay, on_sync_timer, ss);
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    SyncState* ss = data;
    /* Only once a server runs for the tab, or for built-in structured formats */
    EditorTab* tab = sync_tab(ss);
    if (tab == NULL) return;
    if (server_for(ss->app, tab, FALSE) != NULL ||
        (tab->path != NULL && code_lint_source_name(tab->path) != NULL)) {
        schedule_sync(ss, SYNC_DELAY_MS);
    }
}

static void sync_state_free(gpointer p) {
    SyncState* ss = p;
    if (ss->timer != 0) g_source_remove(ss->timer);
    if (ss->lint_canc != NULL) {
        g_cancellable_cancel(ss->lint_canc);
        g_clear_object(&ss->lint_canc);
    }
    g_free(ss);
}

void code_intel_attach(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || tab->path == NULL) return;
    if (g_object_get_data(G_OBJECT(tab->buffer), SYNC_TIMER_KEY) != NULL) return;
    SyncState* ss = g_new0(SyncState, 1);
    ss->app = app;
    ss->buffer = tab->buffer;
    g_object_set_data_full(G_OBJECT(tab->buffer), SYNC_TIMER_KEY, ss, sync_state_free);
    g_signal_connect(tab->buffer, "changed", G_CALLBACK(on_buffer_changed), ss);
    /* Re-apply any diagnostics already known for this file */
    gchar* canon = g_canonicalize_filename(tab->path, NULL);
    if (g_hash_table_contains(intel_of(app)->diagnostics, canon) ||
        g_hash_table_contains(intel_of(app)->task_diagnostics, canon)) {
        apply_diagnostics_to_buffer(app, tab->buffer, canon);
    }
    g_free(canon);
    /* Start LSP server if configured, else run built-in JSON/TOML/YAML/XML checker */
    if (commands_for(intel_of(app), tab) != NULL ||
        code_lint_source_name(tab->path) != NULL) {
        schedule_sync(ss, 50);
    }
}

void code_intel_detach(AppData* app, EditorTab* tab) {
    if (app == NULL || app->code_intel == NULL || tab == NULL || tab->path == NULL) return;
    gchar* canon = g_canonicalize_filename(tab->path, NULL);
    gboolean still_open = FALSE;
    for (GList* l = app->editors; l != NULL && !still_open; l = l->next) {
        EditorTab* t = l->data;
        if (t == NULL || t == tab || t->path == NULL) continue;
        gchar* have = g_canonicalize_filename(t->path, NULL);
        still_open = (g_strcmp0(have, canon) == 0);
        g_free(have);
    }
    if (!still_open) {
        Server* s = server_for(app, tab, FALSE);
        if (s != NULL) lsp_client_close_document(s->client, canon);
        if (g_hash_table_remove(intel_of(app)->diagnostics, canon)) {
            app_emit(app, APP_EVENT_STATUS_CHANGED);
            app_emit(app, APP_EVENT_DIAGNOSTICS_CHANGED);
        }
    }
    g_free(canon);
}

/* The byte index of the 0-based character column in line of path, from
 * its open tab (a closed file is not read: the column is taken as bytes) */
static gint task_column_to_byte(AppData* app, const gchar* path, gint line, gint column) {
    EditorTab* t = editor_find_tab(app, path);
    if (t == NULL || t->buffer == NULL || column <= 0) return MAX(column, 0);
    if (line < 0 || line >= gtk_text_buffer_get_line_count(t->buffer)) return column;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(t->buffer, &it, line);
    gint chars = gtk_text_iter_get_chars_in_line(&it);
    gtk_text_iter_set_line_offset(&it, MIN(column, MAX(chars - 1, 0)));
    return gtk_text_iter_get_line_index(&it);
}

void code_intel_set_task_diagnostics(AppData* app, const GPtrArray* entries) {
    if (app == NULL) return;
    CodeIntel* ci = intel_of(app);
    if (g_hash_table_size(ci->task_diagnostics) == 0 && (entries == NULL || entries->len == 0)) return;
    /* The files to underline again: those of the last task and of this one */
    GHashTable* touched = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTableIter it;
    gpointer key = NULL;
    g_hash_table_iter_init(&it, ci->task_diagnostics);
    while (g_hash_table_iter_next(&it, &key, NULL)) g_hash_table_add(touched, g_strdup(key));
    g_hash_table_remove_all(ci->task_diagnostics);
    for (guint i = 0; entries != NULL && i < entries->len; i++) {
        const CodeDiagEntry* e = g_ptr_array_index(entries, i);
        if (e->path == NULL) continue;
        gchar* canon = g_canonicalize_filename(e->path, NULL);
        GPtrArray* list = g_hash_table_lookup(ci->task_diagnostics, canon);
        if (list == NULL) {
            list = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_diagnostic_free);
            g_hash_table_insert(ci->task_diagnostics, g_strdup(canon), list);
        }
        LspDiagnostic* d = g_new0(LspDiagnostic, 1);
        *d = e->diag;
        d->column = task_column_to_byte(app, canon, e->diag.line, e->diag.column);
        d->end_line = d->line;
        d->end_column = d->column;
        d->message = g_strdup(e->diag.message);
        d->source = g_strdup(e->diag.source);
        g_ptr_array_add(list, d);
        g_hash_table_add(touched, canon);
    }
    g_hash_table_iter_init(&it, touched);
    while (g_hash_table_iter_next(&it, &key, NULL)) apply_diagnostics(app, key);
    g_hash_table_unref(touched);
    app_emit(app, APP_EVENT_STATUS_CHANGED);
    app_emit(app, APP_EVENT_DIAGNOSTICS_CHANGED);
}
