/* One language server process (see lsp_client.h) */
#include "lsp_client.h"
#include <string.h>
#if defined(G_OS_WIN32) || defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#define READ_CHUNK 65536
#define EXIT_DELAY_MS 2000

typedef struct {
    LspResponseFunc func;
    gpointer data;
    GDestroyNotify destroy;
} Pending;

typedef struct {
    gint version;
    gchar* digest;   /* SHA-1 of the last text sent */
} Document;

struct LspClient {
    GSubprocess* proc;
    GOutputStream* in;     /* server stdin */
    GInputStream* out;     /* server stdout */
    GCancellable* cancellable;
    LspReader* reader;
    LspClientState state;
    LspCapabilities* caps;
    gchar* root;
    gchar* program;
    LspStateFunc on_state;
    gpointer state_data;
    LspNotifyFunc on_notify;
    gpointer notify_data;
    gint64 next_id;
    GHashTable* pending;   /* id (gint64*) -> Pending* */
    GQueue* queued;        /* messages waiting for initialize (gchar*) */
    GQueue* writes;        /* framed messages to write (GBytes*) */
    gboolean writing;
    GHashTable* documents; /* path -> Document* */
};

static void pending_free(gpointer p) {
    Pending* pd = p;
    if (pd->destroy != NULL) pd->destroy(pd->data);
    g_free(pd);
}

static void document_free(gpointer p) {
    Document* d = p;
    g_free(d->digest);
    g_free(d);
}

/* ------------------------------------------------------------------------- */
/* Writing                                                                   */
/* ------------------------------------------------------------------------- */

static void write_next(LspClient* c);
static void fail(LspClient* c);

static void on_written(GObject* source, GAsyncResult* res, gpointer data) {
    GError* err = NULL;
    gssize n = g_output_stream_write_bytes_finish(G_OUTPUT_STREAM(source), res, &err);
    if (err != NULL && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_error_free(err);
        return; /* the client is gone */
    }
    LspClient* c = data;
    c->writing = FALSE;
    if (n < 0) {
        g_clear_error(&err);
        fail(c);
        return;
    }
    GBytes* head = g_queue_pop_head(c->writes);
    if (head != NULL && (gsize)n < g_bytes_get_size(head)) {
        /* Partial write: the rest goes first */
        GBytes* rest = g_bytes_new_from_bytes(head, (gsize)n, g_bytes_get_size(head) - (gsize)n);
        g_queue_push_head(c->writes, rest);
    }
    if (head != NULL) g_bytes_unref(head);
    write_next(c);
}

static void write_next(LspClient* c) {
    if (c->writing || c->state == LSP_CLIENT_FAILED || g_queue_is_empty(c->writes)) return;
    c->writing = TRUE;
    g_output_stream_write_bytes_async(c->in, g_queue_peek_head(c->writes), G_PRIORITY_DEFAULT, c->cancellable,
                                      on_written, c);
}

/* Frame and write a complete message (taken) */
static void send_now(LspClient* c, gchar* body) {
    gsize len = 0;
    gchar* framed = lsp_frame(body, &len);
    g_free(body);
    g_queue_push_tail(c->writes, g_bytes_new_take(framed, len));
    write_next(c);
}

/* Write now, or once initialized */
static void send(LspClient* c, gchar* body) {
    if (c->state == LSP_CLIENT_FAILED) {
        g_free(body);
    } else if (c->state == LSP_CLIENT_STARTING) {
        g_queue_push_tail(c->queued, body);
    } else {
        send_now(c, body);
    }
}

/* ------------------------------------------------------------------------- */
/* Reading                                                                   */
/* ------------------------------------------------------------------------- */

static void fail(LspClient* c) {
    if (c->state == LSP_CLIENT_FAILED) return;
    /* Failed first: the answers below must not fail it again */
    c->state = LSP_CLIENT_FAILED;
    g_cancellable_cancel(c->cancellable);
    /* Pending answers will never come */
    GHashTable* pending = c->pending;
    c->pending = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, pending_free);
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, pending);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        Pending* pd = value;
        if (pd->func != NULL) pd->func(c, NULL, pd->data);
    }
    g_hash_table_destroy(pending);
    g_queue_free_full(c->queued, g_free);
    c->queued = g_queue_new();
    if (c->on_state != NULL) c->on_state(c, LSP_CLIENT_FAILED, c->state_data);
}

static void on_initialized(LspClient* c, const LspMessage* msg, gpointer data) {
    (void)data;
    if (msg == NULL || msg->error != NULL) {
        fail(c);
        return;
    }
    c->caps = lsp_parse_capabilities(msg->result);
    send_now(c, lsp_notification("initialized", "{}"));
    c->state = LSP_CLIENT_READY;
    while (!g_queue_is_empty(c->queued)) send_now(c, g_queue_pop_head(c->queued));
    if (c->on_state != NULL) c->on_state(c, LSP_CLIENT_READY, c->state_data);
}

static void handle(LspClient* c, LspMessage* msg) {
    if (msg->method != NULL) {
        if (msg->id_json != NULL) {
            /* A server request: answer it so the server does not wait */
            gchar* result = NULL;
            if (strcmp(msg->method, "workspace/configuration") == 0) {
                const JsonValue* items = msg->params != NULL ? json_value_get(msg->params, "items") : NULL;
                GString* arr = g_string_new("[");
                for (guint i = 0; items != NULL && i < json_value_get_length(items); i++) {
                    g_string_append(arr, i > 0 ? ",null" : "null");
                }
                g_string_append_c(arr, ']');
                result = g_string_free(arr, FALSE);
            }
            send_now(c, lsp_reply(msg->id_json, result));
            g_free(result);
        }
        if (c->on_notify != NULL) c->on_notify(c, msg, c->notify_data);
        return;
    }
    if (msg->id < 0) return;
    Pending* pd = NULL;
    gpointer key = NULL;
    if (!g_hash_table_steal_extended(c->pending, &msg->id, &key, (gpointer*)&pd)) return;
    g_free(key);
    if (pd->func != NULL) pd->func(c, msg->error != NULL ? NULL : msg, pd->data);
    pending_free(pd);
}

static void read_next(LspClient* c);

static void on_read(GObject* source, GAsyncResult* res, gpointer data) {
    GError* err = NULL;
    GBytes* bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), res, &err);
    if (err != NULL && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_error_free(err);
        if (bytes != NULL) g_bytes_unref(bytes);
        return; /* the client is gone */
    }
    LspClient* c = data;
    if (bytes == NULL || g_bytes_get_size(bytes) == 0) {
        g_clear_error(&err);
        if (bytes != NULL) g_bytes_unref(bytes);
        fail(c);
        return;
    }
    gsize len = 0;
    const gchar* chunk = g_bytes_get_data(bytes, &len);
    lsp_reader_feed(c->reader, chunk, len);
    g_bytes_unref(bytes);
    gchar* body;
    gsize body_len = 0;
    while (c->state != LSP_CLIENT_FAILED && (body = lsp_reader_next(c->reader, &body_len)) != NULL) {
        LspMessage* msg = lsp_message_parse(body, body_len);
        if (msg != NULL) {
            handle(c, msg);
            lsp_message_free(msg);
        }
    }
    if (c->state != LSP_CLIENT_FAILED) read_next(c);
}

static void read_next(LspClient* c) {
    g_input_stream_read_bytes_async(c->out, READ_CHUNK, G_PRIORITY_DEFAULT, c->cancellable, on_read, c);
}

/* ------------------------------------------------------------------------- */
/* Life cycle                                                                */
/* ------------------------------------------------------------------------- */

static gint64 add_pending(LspClient* c, LspResponseFunc func, gpointer data, GDestroyNotify destroy) {
    gint64 id = c->next_id++;
    Pending* pd = g_new0(Pending, 1);
    pd->func = func;
    pd->data = data;
    pd->destroy = destroy;
    g_hash_table_insert(c->pending, g_memdup2(&id, sizeof id), pd);
    return id;
}

LspClient* lsp_client_new(gchar** argv, const gchar* root, LspStateFunc on_state, gpointer data,
                          GError** error) {
    if (argv == NULL || argv[0] == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No server command");
        return NULL;
    }
    GSubprocessLauncher* launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    if (root != NULL) g_subprocess_launcher_set_cwd(launcher, root);
    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, (const gchar* const*)argv, error);
    g_object_unref(launcher);
    if (proc == NULL) return NULL;

    LspClient* c = g_new0(LspClient, 1);
    c->proc = proc;
    c->in = g_subprocess_get_stdin_pipe(proc);
    c->out = g_subprocess_get_stdout_pipe(proc);
    c->cancellable = g_cancellable_new();
    c->reader = lsp_reader_new();
    c->root = g_strdup(root);
    c->program = g_path_get_basename(argv[0]);
    c->on_state = on_state;
    c->state_data = data;
    c->next_id = 1;
    c->pending = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, pending_free);
    c->queued = g_queue_new();
    c->writes = g_queue_new();
    c->documents = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, document_free);

    gchar* params = lsp_params_initialize((gint)getpid(), root != NULL ? root : "/");
    gint64 id = add_pending(c, on_initialized, NULL, NULL);
    send_now(c, lsp_request(id, "initialize", params));
    g_free(params);
    read_next(c);
    return c;
}

static gboolean force_exit(gpointer data) {
    GSubprocess* proc = data;
    g_subprocess_force_exit(proc);
    return G_SOURCE_REMOVE;
}

static void on_exited(GObject* source, GAsyncResult* res, gpointer data) {
    (void)data;
    g_subprocess_wait_finish(G_SUBPROCESS(source), res, NULL);
    guint timeout = GPOINTER_TO_UINT(g_object_get_data(source, "lsp-exit-timeout"));
    if (timeout != 0) g_source_remove(timeout);
    g_object_unref(source);
}

void lsp_client_free(LspClient* client) {
    if (client == NULL) return;
    LspClient* c = client;
    g_cancellable_cancel(c->cancellable);
    if (c->state == LSP_CLIENT_READY) {
        /* Synchronous writes: shutdown then exit (small messages) */
        gchar* msgs[2] = {lsp_request(c->next_id++, "shutdown", NULL), lsp_notification("exit", NULL)};
        for (gint i = 0; i < 2; i++) {
            gsize len = 0;
            gchar* framed = lsp_frame(msgs[i], &len);
            g_output_stream_write_all(c->in, framed, len, NULL, NULL, NULL);
            g_free(framed);
            g_free(msgs[i]);
        }
    }
    g_output_stream_close(c->in, NULL, NULL);
    /* Reap the process; kill it if it does not exit by itself */
    guint timeout = g_timeout_add(EXIT_DELAY_MS, force_exit, c->proc);
    g_object_set_data(G_OBJECT(c->proc), "lsp-exit-timeout", GUINT_TO_POINTER(timeout));
    g_subprocess_wait_async(c->proc, NULL, on_exited, NULL);
    c->proc = NULL; /* owned by the wait */

    g_hash_table_destroy(c->pending);
    g_queue_free_full(c->queued, g_free);
    g_queue_free_full(c->writes, (GDestroyNotify)g_bytes_unref);
    g_hash_table_destroy(c->documents);
    lsp_reader_free(c->reader);
    lsp_capabilities_free(c->caps);
    g_object_unref(c->cancellable);
    g_free(c->root);
    g_free(c->program);
    g_free(c);
}

void lsp_client_set_notify_func(LspClient* client, LspNotifyFunc func, gpointer data) {
    if (client == NULL) return;
    client->on_notify = func;
    client->notify_data = data;
}

LspClientState lsp_client_get_state(LspClient* client) {
    return client != NULL ? client->state : LSP_CLIENT_FAILED;
}

const LspCapabilities* lsp_client_get_capabilities(LspClient* client) {
    return client != NULL && client->state == LSP_CLIENT_READY ? client->caps : NULL;
}

const gchar* lsp_client_get_root(LspClient* client) {
    return client != NULL ? client->root : NULL;
}

const gchar* lsp_client_get_name(LspClient* client) {
    if (client == NULL) return NULL;
    if (client->caps != NULL && client->caps->server_name != NULL) return client->caps->server_name;
    return client->program;
}

gint64 lsp_client_request(LspClient* client, const gchar* method, const gchar* params, LspResponseFunc func,
                          gpointer data, GDestroyNotify destroy) {
    if (client == NULL || client->state == LSP_CLIENT_FAILED) {
        if (destroy != NULL) destroy(data);
        return 0;
    }
    gint64 id = add_pending(client, func, data, destroy);
    send(client, lsp_request(id, method, params));
    return id;
}

void lsp_client_cancel(LspClient* client, gint64 id) {
    if (client == NULL || id <= 0) return;
    if (g_hash_table_remove(client->pending, &id)) {
        gchar* params = lsp_params_cancel(id);
        send(client, lsp_notification("$/cancelRequest", params));
        g_free(params);
    }
}

void lsp_client_notify(LspClient* client, const gchar* method, const gchar* params) {
    if (client == NULL) return;
    send(client, lsp_notification(method, params));
}

void lsp_client_sync_document(LspClient* client, const gchar* path, const gchar* text) {
    if (client == NULL || client->state == LSP_CLIENT_FAILED || path == NULL || text == NULL) return;
    gchar* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA1, text, -1);
    Document* doc = g_hash_table_lookup(client->documents, path);
    gchar* params = NULL;
    const gchar* method;
    if (doc == NULL) {
        doc = g_new0(Document, 1);
        doc->version = 1;
        g_hash_table_insert(client->documents, g_strdup(path), doc);
        params = lsp_params_did_open(path, lsp_language_id(path), doc->version, text);
        method = "textDocument/didOpen";
    } else if (g_strcmp0(doc->digest, digest) != 0) {
        doc->version++;
        params = lsp_params_did_change(path, doc->version, text);
        method = "textDocument/didChange";
    } else {
        g_free(digest);
        return;
    }
    g_free(doc->digest);
    doc->digest = digest;
    send(client, lsp_notification(method, params));
    g_free(params);
}

void lsp_client_close_document(LspClient* client, const gchar* path) {
    if (client == NULL || path == NULL || !g_hash_table_remove(client->documents, path)) return;
    gchar* params = lsp_params_did_close(path);
    send(client, lsp_notification("textDocument/didClose", params));
    g_free(params);
}

GList* lsp_client_get_documents(LspClient* client) {
    return client != NULL ? g_hash_table_get_keys(client->documents) : NULL;
}
