#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

/* One language server process (docs/code-nav.md): started with
 * GSubprocess, spoken to over stdin / stdout with lsp_protocol messages,
 * asynchronously on the thread-default main context (the UI thread). The
 * client sends "initialize" at once and queues everything else until the
 * server answers. Requests the server sends are answered with null.
 * Documents are synchronized with full text. No widgets, unit-tested with
 * a fake server.
 *
 * Never free a client from one of its own callbacks (state or answer):
 * defer it to an idle callback. */
#include <gio/gio.h>

#include "lsp_protocol.h"

typedef struct LspClient LspClient;

typedef enum {
    LSP_CLIENT_STARTING = 0,
    LSP_CLIENT_READY,
    LSP_CLIENT_FAILED      /* could not start, died, or refused initialize */
} LspClientState;

/* Answer of a request: msg is NULL when the request failed (server gone,
 * error response, client freed) */
typedef void (*LspResponseFunc)(LspClient* client, const LspMessage* msg, gpointer data);
/* The client became ready or failed */
typedef void (*LspStateFunc)(LspClient* client, LspClientState state, gpointer data);

/* A notification from the server (diagnostics, logs...) */
typedef void (*LspNotifyFunc)(LspClient* client, const LspMessage* msg, gpointer data);

/* Start argv (argv[0] looked up in PATH) for the project at root. NULL
 * with error when the program cannot be started. */
LspClient* lsp_client_new(gchar** argv, const gchar* root, LspStateFunc on_state, gpointer data,
                          GError** error);
/* Shut the server down (shutdown, exit, then killed after a delay) and free
 * the client; pending answers are dropped without calling their functions */
void lsp_client_free(LspClient* client);

/* Receive the server's notifications */
void lsp_client_set_notify_func(LspClient* client, LspNotifyFunc func, gpointer data);

LspClientState lsp_client_get_state(LspClient* client);
/* Capabilities of the server once ready, else NULL */
const LspCapabilities* lsp_client_get_capabilities(LspClient* client);
const gchar* lsp_client_get_root(LspClient* client);
/* Server name ("clangd"), or the program name */
const gchar* lsp_client_get_name(LspClient* client);

/* Send a request; func gets the answer (destroy frees data afterwards).
 * Returns its id (0 when the client failed: func is not called). */
gint64 lsp_client_request(LspClient* client, const gchar* method, const gchar* params, LspResponseFunc func,
                          gpointer data, GDestroyNotify destroy);
/* Forget a request ($/cancelRequest is sent; func is not called) */
void lsp_client_cancel(LspClient* client, gint64 id);
void lsp_client_notify(LspClient* client, const gchar* method, const gchar* params);

/* Make the server see text as the content of path: didOpen the first time,
 * then didChange when the text differs from the last one sent */
void lsp_client_sync_document(LspClient* client, const gchar* path, const gchar* text);
/* didClose a synchronized document (nothing when it was not opened) */
void lsp_client_close_document(LspClient* client, const gchar* path);
/* Paths of the synchronized documents (do not free the strings) */
GList* lsp_client_get_documents(LspClient* client);

#endif /* LSP_CLIENT_H */
