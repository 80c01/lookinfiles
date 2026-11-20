/* Unit tests for src/code/lsp_client.c against tests/fixtures/fake_lsp.py (a
 * minimal language server in Python; skipped without python3). */
#include "code/lsp_client.h"

#include <string.h>

typedef struct {
    LspClientState state;
    gint state_calls;
    gint answers;
    gint failures;
    gchar* last;      /* hover text or "line:col" of a definition */
    gint destroyed;
} Ctx;

static void on_state(LspClient* client, LspClientState state, gpointer data) {
    (void)client;
    Ctx* ctx = data;
    ctx->state = state;
    ctx->state_calls++;
}

static void on_definition(LspClient* client, const LspMessage* msg, gpointer data) {
    (void)client;
    Ctx* ctx = data;
    if (msg == NULL) {
        ctx->failures++;
        return;
    }
    GPtrArray* locs = lsp_parse_locations(msg->result);
    g_free(ctx->last);
    LspLocation* loc = locs->len > 0 ? g_ptr_array_index(locs, 0) : NULL;
    ctx->last = loc != NULL ? g_strdup_printf("%d:%d", loc->line, loc->column) : g_strdup("none");
    g_ptr_array_unref(locs);
    ctx->answers++;
}

static void on_hover(LspClient* client, const LspMessage* msg, gpointer data) {
    (void)client;
    Ctx* ctx = data;
    if (msg == NULL) {
        ctx->failures++;
        return;
    }
    g_free(ctx->last);
    ctx->last = lsp_parse_hover(msg->result);
    ctx->answers++;
}

static void on_completion(LspClient* client, const LspMessage* msg, gpointer data) {
    (void)client;
    Ctx* ctx = data;
    if (msg == NULL) {
        ctx->failures++;
        return;
    }
    LspCompletionList* cl = lsp_parse_completions(msg->result);
    g_free(ctx->last);
    LspCompletionItem* it = cl->items->len > 0 ? g_ptr_array_index(cl->items, 0) : NULL;
    ctx->last = g_strdup(it != NULL ? it->label : "");
    lsp_completion_list_free(cl);
    ctx->answers++;
}

static void on_ws_symbols(LspClient* client, const LspMessage* msg, gpointer data) {
    (void)client;
    Ctx* ctx = data;
    if (msg == NULL) {
        ctx->failures++;
        return;
    }
    GPtrArray* syms = lsp_parse_symbols(msg->result);
    g_free(ctx->last);
    LspSymbol* s = syms->len > 0 ? g_ptr_array_index(syms, 0) : NULL;
    ctx->last = g_strdup(s != NULL ? s->name : "");
    g_ptr_array_unref(syms);
    ctx->answers++;
}

static void on_rename(LspClient* client, const LspMessage* msg, gpointer data) {
    (void)client;
    Ctx* ctx = data;
    if (msg == NULL) {
        ctx->failures++;
        return;
    }
    LspWorkspaceEdit* ws = lsp_parse_workspace_edit(msg->result);
    g_free(ctx->last);
    LspFileEdits* fe = (ws != NULL && ws->files->len > 0) ? g_ptr_array_index(ws->files, 0) : NULL;
    LspTextEdit* te = (fe != NULL && fe->edits->len > 0) ? g_ptr_array_index(fe->edits, 0) : NULL;
    ctx->last = g_strdup(te != NULL ? te->new_text : "");
    lsp_workspace_edit_free(ws);
    ctx->answers++;
}

static void count_destroy(gpointer data) {
    ((Ctx*)data)->destroyed++;
}

static gboolean on_timeout(gpointer data) {
    *(gboolean*)data = TRUE;
    return G_SOURCE_REMOVE;
}

/* Run the main context until cond becomes true (or 10 s pass) */
#define WAIT_FOR(cond)                                                   \
    do {                                                                 \
        gboolean timed_out_ = FALSE;                                     \
        guint t_ = g_timeout_add(10000, on_timeout, &timed_out_);        \
        while (!(cond) && !timed_out_) g_main_context_iteration(NULL, TRUE); \
        g_assert_false(timed_out_);                                      \
        g_source_remove(t_);                                             \
    } while (0)

static gchar** server_argv(const gchar* extra) {
    gchar* python = g_find_program_in_path("python3");
    if (python == NULL) return NULL;
    gchar** argv = g_new0(gchar*, 4);
    argv[0] = python;
    /* Absolute: the server runs in the project root (the temporary directory here) */
    gchar* cwd = g_get_current_dir();
    argv[1] = g_build_filename(cwd, "tests", "fixtures", "fake_lsp.py", NULL);
    g_free(cwd);
    argv[2] = g_strdup(extra);
    return argv;
}

static void test_requests(void) {
    gchar** argv = server_argv(NULL);
    if (argv == NULL) {
        g_test_skip("python3 is not installed");
        return;
    }
    Ctx ctx = {0};
    g_autofree gchar* doc = g_build_filename(g_get_tmp_dir(), "a.c", NULL);
    GError* err = NULL;
    LspClient* c = lsp_client_new(argv, g_get_tmp_dir(), on_state, &ctx, &err);
    g_assert_no_error(err);
    g_assert_cmpint(lsp_client_get_state(c), ==, LSP_CLIENT_STARTING);
    /* Queued until initialized */
    lsp_client_sync_document(c, doc, "first text");
    gchar* params = lsp_params_position(doc, 3, 5, -1);
    gint64 id = lsp_client_request(c, "textDocument/definition", params, on_definition, &ctx, count_destroy);
    g_assert_cmpint(id, >, 0);
    WAIT_FOR(ctx.answers == 1);
    g_assert_cmpint(ctx.state, ==, LSP_CLIENT_READY);
    g_assert_cmpstr(ctx.last, ==, "4:5");
    g_assert_cmpint(ctx.destroyed, ==, 1);
    g_assert_cmpstr(lsp_client_get_name(c), ==, "fake-lsp");
    const LspCapabilities* caps = lsp_client_get_capabilities(c);
    g_assert_true(caps->definition);
    g_assert_false(caps->references);
    g_assert_true(caps->workspace_symbol);
    g_assert_true(caps->completion);
    g_assert_cmpstr(caps->completion_triggers, ==, ".>");
    g_assert_true(caps->rename);
    g_assert_true(caps->prepare_rename);
    g_assert_cmpstr(caps->signature_triggers, ==, "(");

    /* The server sees the documents as synchronized */
    gchar* hover = lsp_params_position(doc, 0, 0, -1);
    lsp_client_request(c, "textDocument/hover", hover, on_hover, &ctx, NULL);
    WAIT_FOR(ctx.answers == 2);
    g_assert_cmpstr(ctx.last, ==, "first text");
    lsp_client_sync_document(c, doc, "first text"); /* unchanged: nothing sent */
    lsp_client_sync_document(c, doc, "second \"text\"\n");
    lsp_client_request(c, "textDocument/hover", hover, on_hover, &ctx, NULL);
    WAIT_FOR(ctx.answers == 3);
    g_assert_cmpstr(ctx.last, ==, "second \"text\""); /* hover text is trimmed */
    GList* docs = lsp_client_get_documents(c);
    g_assert_cmpuint(g_list_length(docs), ==, 1);
    g_list_free(docs);
    lsp_client_close_document(c, doc);
    docs = lsp_client_get_documents(c);
    g_assert_null(docs);

    /* Completion, workspace/symbol, and rename round-trips */
    gchar* comp_p = lsp_params_completion(doc, 0, 2, ".");
    lsp_client_request(c, "textDocument/completion", comp_p, on_completion, &ctx, NULL);
    WAIT_FOR(ctx.answers == 4);
    g_assert_cmpstr(ctx.last, ==, "fake_fn");
    g_free(comp_p);

    gchar* ws_p = lsp_params_workspace_symbol("foo");
    lsp_client_request(c, "workspace/symbol", ws_p, on_ws_symbols, &ctx, NULL);
    WAIT_FOR(ctx.answers == 5);
    g_assert_cmpstr(ctx.last, ==, "ws_foo");
    g_free(ws_p);

    gchar* ren_p = lsp_params_rename(doc, 0, 0, "bar");
    lsp_client_request(c, "textDocument/rename", ren_p, on_rename, &ctx, NULL);
    WAIT_FOR(ctx.answers == 6);
    g_assert_cmpstr(ctx.last, ==, "bar");
    g_free(ren_p);

    /* A cancelled request is never answered; an unknown method fails */
    id = lsp_client_request(c, "textDocument/definition", params, on_definition, &ctx, count_destroy);
    lsp_client_cancel(c, id);
    g_assert_cmpint(ctx.destroyed, ==, 2);
    lsp_client_request(c, "textDocument/unknown", params, on_definition, &ctx, NULL);
    WAIT_FOR(ctx.failures == 1);
    g_assert_cmpint(ctx.answers, ==, 6);

    /* Freed with a request pending: dropped, destroy called */
    lsp_client_request(c, "textDocument/definition", params, on_definition, &ctx, count_destroy);
    lsp_client_free(c);
    g_assert_cmpint(ctx.destroyed, ==, 3);
    g_assert_cmpint(ctx.answers, ==, 6);
    /* Let the exit / reaping run */
    for (gint i = 0; i < 50; i++) g_main_context_iteration(NULL, FALSE);
    g_free(hover);
    g_free(params);
    g_free(ctx.last);
    g_strfreev(argv);
}

static void test_server_dies(void) {
    gchar** argv = server_argv("--crash-on-definition");
    if (argv == NULL) {
        g_test_skip("python3 is not installed");
        return;
    }
    Ctx ctx = {0};
    g_autofree gchar* doc = g_build_filename(g_get_tmp_dir(), "a.c", NULL);
    LspClient* c = lsp_client_new(argv, g_get_tmp_dir(), on_state, &ctx, NULL);
    gchar* params = lsp_params_position(doc, 0, 0, -1);
    lsp_client_request(c, "textDocument/definition", params, on_definition, &ctx, count_destroy);
    WAIT_FOR(ctx.state == LSP_CLIENT_FAILED);
    g_assert_cmpint(ctx.failures, ==, 1);
    g_assert_cmpint(ctx.destroyed, ==, 1);
    /* Requests on a failed client: refused at once */
    g_assert_cmpint(lsp_client_request(c, "x", NULL, on_definition, &ctx, count_destroy), ==, 0);
    g_assert_cmpint(ctx.destroyed, ==, 2);
    lsp_client_free(c);
    g_free(params);
    g_strfreev(argv);
}

static void test_missing_program(void) {
    gchar* argv[] = {"lookin-no-such-language-server", NULL};
    GError* err = NULL;
    LspClient* c = lsp_client_new(argv, g_get_tmp_dir(), NULL, NULL, &err);
    g_assert_null(c);
    g_assert_nonnull(err);
    g_error_free(err);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/lsp_client/requests", test_requests);
    g_test_add_func("/lsp_client/server_dies", test_server_dies);
    g_test_add_func("/lsp_client/missing_program", test_missing_program);
    return g_test_run();
}
