/* Unit tests for src/lsp_protocol.c: framing, requests, results and
 * position conversions of the Language Server Protocol. */
#include "lsp_protocol.h"

#include <string.h>

static LspMessage* parse(const gchar* json) {
    return lsp_message_parse(g_strdup(json), strlen(json));
}

static void test_framing(void) {
    gsize len = 0;
    gchar* msg = lsp_frame("{\"a\":1}", &len);
    g_assert_cmpstr(msg, ==, "Content-Length: 7\r\n\r\n{\"a\":1}");
    g_assert_cmpuint(len, ==, strlen(msg));
    LspReader* r = lsp_reader_new();
    /* Two messages, fed in pieces */
    gchar* two = g_strconcat(msg, "Content-Type: x\r\ncontent-length: 2\r\n\r\n[]", NULL);
    gsize n = strlen(two);
    gsize body_len = 0;
    lsp_reader_feed(r, two, 10);
    g_assert_null(lsp_reader_next(r, &body_len));
    lsp_reader_feed(r, two + 10, n - 10 - 1);
    gchar* b = lsp_reader_next(r, &body_len);
    g_assert_cmpstr(b, ==, "{\"a\":1}");
    g_assert_cmpuint(body_len, ==, 7);
    g_free(b);
    g_assert_null(lsp_reader_next(r, &body_len));
    lsp_reader_feed(r, two + n - 1, 1);
    b = lsp_reader_next(r, &body_len);
    g_assert_cmpstr(b, ==, "[]");
    g_free(b);
    /* UTF-8 bodies: the length counts bytes */
    gchar* utf = lsp_frame("\"é\"", NULL);
    g_assert_true(g_str_has_prefix(utf, "Content-Length: 4\r\n"));
    lsp_reader_feed(r, utf, strlen(utf));
    b = lsp_reader_next(r, &body_len);
    g_assert_cmpstr(b, ==, "\"é\"");
    g_free(b);
    g_free(utf);
    lsp_reader_free(r);
    g_free(two);
    g_free(msg);
}

static void test_requests(void) {
    gchar* p = lsp_params_position("/tmp/a b.c", 3, 7, -1);
    g_assert_cmpstr(p, ==, "{\"textDocument\":{\"uri\":\"file:///tmp/a%20b.c\"},\"position\":{\"line\":3,\"character\":7}}");
    gchar* r = lsp_request(5, "textDocument/definition", p);
    g_assert_true(g_str_has_prefix(r, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/definition\",\"params\":{"));
    g_free(r);
    g_free(p);
    p = lsp_params_position("/a.c", 0, 0, 1);
    g_assert_nonnull(strstr(p, "\"context\":{\"includeDeclaration\":true}"));
    g_free(p);
    p = lsp_params_did_change("/a.c", 2, "line \"1\"\n\ttab");
    g_assert_cmpstr(p, ==, "{\"textDocument\":{\"uri\":\"file:///a.c\",\"version\":2},\"contentChanges\":[{\"text\":\"line \\\"1\\\"\\n\\ttab\"}]}");
    g_free(p);
    p = lsp_params_did_open("/a.py", "python", 1, "x");
    g_assert_nonnull(strstr(p, "\"languageId\":\"python\",\"version\":1,\"text\":\"x\""));
    g_free(p);
    p = lsp_params_initialize(42, "/w/proj");
    LspMessage* m = parse(p); /* valid JSON (not a message: no id / method) */
    g_assert_nonnull(m);
    lsp_message_free(m);
    g_assert_nonnull(strstr(p, "\"rootUri\":\"file:///w/proj\""));
    g_free(p);
    gchar* reply = lsp_reply("\"abc\"", NULL);
    g_assert_cmpstr(reply, ==, "{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"result\":null}");
    g_free(reply);
    g_assert_cmpstr(lsp_language_id("x.tsx"), ==, "typescriptreact");
    g_assert_cmpstr(lsp_language_id("x.hpp"), ==, "cpp");
    g_assert_cmpstr(lsp_language_id("Makefile"), ==, "makefile");
    gchar* path = lsp_uri_to_path("file:///tmp/a%20b.c");
    g_assert_cmpstr(path, ==, "/tmp/a b.c");
    g_free(path);
    g_assert_null(lsp_uri_to_path("untitled:1"));
}

static void test_messages(void) {
    LspMessage* m = parse("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":null}");
    g_assert_null(m->method);
    g_assert_cmpint(m->id, ==, 7);
    g_assert_nonnull(m->result);
    lsp_message_free(m);
    m = parse("{\"jsonrpc\":\"2.0\",\"id\":\"s1\",\"method\":\"window/workDoneProgress/create\",\"params\":{}}");
    g_assert_cmpstr(m->method, ==, "window/workDoneProgress/create");
    g_assert_cmpstr(m->id_json, ==, "\"s1\"");
    g_assert_cmpint(m->id, ==, -1);
    lsp_message_free(m);
    m = parse("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"x\"}}");
    g_assert_null(m->id_json);
    g_assert_nonnull(m->params);
    lsp_message_free(m);
    m = parse("{\"jsonrpc\":\"2.0\",\"id\":3,\"error\":{\"code\":-32601,\"message\":\"no\"}}");
    g_assert_nonnull(m->error);
    lsp_message_free(m);
    g_assert_null(parse("[1,2]"));
    g_assert_null(parse("garbage"));
}

static void test_capabilities(void) {
    LspMessage* m = parse("{\"id\":1,\"result\":{\"capabilities\":{\"positionEncoding\":\"utf-8\","
                          "\"definitionProvider\":true,\"declarationProvider\":false,\"hoverProvider\":{},"
                          "\"referencesProvider\":true,\"documentSymbolProvider\":true,"
                          "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"],\"retriggerCharacters\":[\",\",\")\"]}},"
                          "\"serverInfo\":{\"name\":\"clangd\",\"version\":\"19\"}}}");
    LspCapabilities* c = lsp_parse_capabilities(m->result);
    g_assert_true(c->utf8_positions);
    g_assert_true(c->definition);
    g_assert_false(c->declaration);
    g_assert_true(c->hover);
    g_assert_true(c->references);
    g_assert_true(c->signature_help);
    g_assert_false(c->workspace_symbol);
    g_assert_cmpstr(c->signature_triggers, ==, "(,)");
    g_assert_cmpstr(c->server_name, ==, "clangd");
    lsp_capabilities_free(c);
    lsp_message_free(m);
}

static void test_locations(void) {
    LspMessage* m = parse("{\"id\":1,\"result\":{\"uri\":\"file:///p/a.c\",\"range\":{\"start\":{\"line\":4,\"character\":2},"
                          "\"end\":{\"line\":4,\"character\":6}}}}");
    GPtrArray* l = lsp_parse_locations(m->result);
    g_assert_cmpuint(l->len, ==, 1);
    LspLocation* loc = g_ptr_array_index(l, 0);
    g_assert_cmpstr(loc->path, ==, "/p/a.c");
    g_assert_cmpint(loc->line, ==, 4);
    g_assert_cmpint(loc->column, ==, 2);
    g_ptr_array_unref(l);
    lsp_message_free(m);
    /* LocationLink[]: the selection range */
    m = parse("{\"id\":1,\"result\":[{\"targetUri\":\"file:///p/b.h\",\"targetRange\":{\"start\":{\"line\":1,\"character\":0},"
              "\"end\":{\"line\":9,\"character\":1}},\"targetSelectionRange\":{\"start\":{\"line\":2,\"character\":5},"
              "\"end\":{\"line\":2,\"character\":9}}},{\"targetUri\":\"untitled:x\",\"targetRange\":{}}]}");
    l = lsp_parse_locations(m->result);
    g_assert_cmpuint(l->len, ==, 1);
    loc = g_ptr_array_index(l, 0);
    g_assert_cmpstr(loc->path, ==, "/p/b.h");
    g_assert_cmpint(loc->line, ==, 2);
    g_assert_cmpint(loc->column, ==, 5);
    g_ptr_array_unref(l);
    lsp_message_free(m);
    m = parse("{\"id\":1,\"result\":null}");
    l = lsp_parse_locations(m->result);
    g_assert_cmpuint(l->len, ==, 0);
    g_ptr_array_unref(l);
    lsp_message_free(m);
}

static void test_hover(void) {
    LspMessage* m = parse("{\"id\":1,\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"### function `add`\\n\\nAdds.\"}}}");
    gchar* h = lsp_parse_hover(m->result);
    g_assert_cmpstr(h, ==, "### function `add`\n\nAdds.");
    g_free(h);
    lsp_message_free(m);
    m = parse("{\"id\":1,\"result\":{\"contents\":[{\"language\":\"python\",\"value\":\"def f(x)\"},\"Doc text\"]}}");
    h = lsp_parse_hover(m->result);
    g_assert_cmpstr(h, ==, "```python\ndef f(x)\n```\n\nDoc text");
    g_free(h);
    lsp_message_free(m);
    m = parse("{\"id\":1,\"result\":{\"contents\":\"\"}}");
    g_assert_null(lsp_parse_hover(m->result));
    lsp_message_free(m);
    m = parse("{\"id\":1,\"result\":null}");
    g_assert_null(lsp_parse_hover(m->result));
    lsp_message_free(m);
}

static void test_signature(void) {
    LspMessage* m = parse("{\"id\":1,\"result\":{\"signatures\":[{\"label\":\"int add(int a, int b)\","
                          "\"documentation\":{\"kind\":\"markdown\",\"value\":\"Sum.\"},"
                          "\"parameters\":[{\"label\":[8,13]},{\"label\":[15,20]}]}],"
                          "\"activeSignature\":0,\"activeParameter\":1}}");
    LspSignature* s = lsp_parse_signature_help(m->result);
    g_assert_cmpstr(s->label, ==, "int add(int a, int b)");
    g_assert_cmpstr(s->documentation, ==, "Sum.");
    g_assert_cmpint(s->param_start, ==, 15);
    g_assert_cmpint(s->param_end, ==, 20);
    g_assert_cmpuint(s->count, ==, 1);
    lsp_signature_free(s);
    lsp_message_free(m);
    /* String labels, per-signature active parameter, UTF-16 offsets */
    m = parse("{\"id\":1,\"result\":{\"signatures\":[{\"label\":\"f(é, x)\",\"activeParameter\":1,"
              "\"parameters\":[{\"label\":\"é\"},{\"label\":\"x\"}]},{\"label\":\"g()\"}],\"activeSignature\":0}}");
    s = lsp_parse_signature_help(m->result);
    g_assert_cmpint(s->param_start, ==, 6);
    g_assert_cmpint(s->param_end, ==, 7);
    g_assert_cmpuint(s->count, ==, 2);
    lsp_signature_free(s);
    lsp_message_free(m);
    m = parse("{\"id\":1,\"result\":{\"signatures\":[]}}");
    g_assert_null(lsp_parse_signature_help(m->result));
    lsp_message_free(m);
}

static void test_symbols(void) {
    LspMessage* m = parse("{\"id\":1,\"result\":[{\"name\":\"Shop\",\"kind\":5,\"range\":{\"start\":{\"line\":0,\"character\":0}},"
                          "\"selectionRange\":{\"start\":{\"line\":0,\"character\":6}},\"children\":["
                          "{\"name\":\"add\",\"detail\":\"(int)\",\"kind\":6,\"range\":{\"start\":{\"line\":2,\"character\":4}},"
                          "\"selectionRange\":{\"start\":{\"line\":2,\"character\":8}}}]},"
                          "{\"name\":\"main\",\"kind\":12,\"range\":{\"start\":{\"line\":9,\"character\":0}}}]}");
    GPtrArray* syms = lsp_parse_symbols(m->result);
    g_assert_cmpuint(syms->len, ==, 3);
    LspSymbol* s = g_ptr_array_index(syms, 1);
    g_assert_cmpstr(s->name, ==, "add");
    g_assert_cmpstr(s->container, ==, "Shop");
    g_assert_cmpstr(s->detail, ==, "(int)");
    g_assert_cmpint(s->kind, ==, CODE_SYM_METHOD);
    g_assert_cmpint(s->line, ==, 2);
    g_assert_cmpint(s->column, ==, 8);
    g_assert_cmpint(((LspSymbol*)g_ptr_array_index(syms, 2))->kind, ==, CODE_SYM_FUNCTION);
    g_ptr_array_unref(syms);
    lsp_message_free(m);
    m = parse("{\"id\":1,\"result\":[{\"name\":\"x\",\"kind\":13,\"containerName\":\"m\",\"location\":{\"uri\":\"file:///p/a.py\","
              "\"range\":{\"start\":{\"line\":3,\"character\":1}}}}]}");
    syms = lsp_parse_symbols(m->result);
    s = g_ptr_array_index(syms, 0);
    g_assert_cmpstr(s->path, ==, "/p/a.py");
    g_assert_cmpstr(s->container, ==, "m");
    g_assert_cmpint(s->kind, ==, CODE_SYM_VARIABLE);
    g_ptr_array_unref(syms);
    lsp_message_free(m);
}

static void test_diagnostics(void) {
    LspMessage* m = parse("{\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///p/a.c\","
                          "\"diagnostics\":[{\"range\":{\"start\":{\"line\":2,\"character\":4},\"end\":{\"line\":2,"
                          "\"character\":9}},\"severity\":2,\"message\":\"unused variable\",\"source\":\"clang\"},"
                          "{\"range\":{\"start\":{\"line\":5,\"character\":0}},\"message\":\"x\"},{\"message\":\"no range\"}]}}");
    gchar* path = NULL;
    GPtrArray* d = lsp_parse_diagnostics(m->params, &path);
    g_assert_cmpstr(path, ==, "/p/a.c");
    g_assert_cmpuint(d->len, ==, 2);
    LspDiagnostic* first = g_ptr_array_index(d, 0);
    g_assert_cmpint(first->severity, ==, LSP_SEVERITY_WARNING);
    g_assert_cmpint(first->end_column, ==, 9);
    g_assert_cmpstr(first->source, ==, "clang");
    LspDiagnostic* second = g_ptr_array_index(d, 1);
    g_assert_cmpint(second->severity, ==, LSP_SEVERITY_ERROR);
    g_assert_cmpint(second->end_line, ==, 5);
    g_ptr_array_unref(d);
    g_free(path);
    lsp_message_free(m);
}

static void test_positions(void) {
    /* "aé𝄞b": a(1 byte, 1 unit) é(2 bytes, 1 unit) 𝄞(4 bytes, 2 units) b */
    const gchar* line = "a\xc3\xa9\xf0\x9d\x84\x9e" "b\n";
    g_assert_cmpint(lsp_column_to_byte(line, -1, 2, FALSE), ==, 3);
    g_assert_cmpint(lsp_column_to_byte(line, -1, 4, FALSE), ==, 7);
    g_assert_cmpint(lsp_column_to_byte(line, -1, 99, FALSE), ==, 8);
    g_assert_cmpint(lsp_column_to_byte(line, -1, 3, TRUE), ==, 3);
    g_assert_cmpint(lsp_column_to_byte(line, -1, 99, TRUE), ==, 8);
    g_assert_cmpint(lsp_byte_to_column(line, -1, 7, FALSE), ==, 4);
    g_assert_cmpint(lsp_byte_to_column(line, -1, 3, FALSE), ==, 2);
    g_assert_cmpint(lsp_byte_to_column(line, -1, 7, TRUE), ==, 7);
    g_assert_cmpint(lsp_column_to_byte(NULL, -1, 3, FALSE), ==, 0);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/lsp_protocol/framing", test_framing);
    g_test_add_func("/lsp_protocol/requests", test_requests);
    g_test_add_func("/lsp_protocol/messages", test_messages);
    g_test_add_func("/lsp_protocol/capabilities", test_capabilities);
    g_test_add_func("/lsp_protocol/locations", test_locations);
    g_test_add_func("/lsp_protocol/hover", test_hover);
    g_test_add_func("/lsp_protocol/signature", test_signature);
    g_test_add_func("/lsp_protocol/symbols", test_symbols);
    g_test_add_func("/lsp_protocol/positions", test_positions);
    g_test_add_func("/lsp_protocol/diagnostics", test_diagnostics);
    return g_test_run();
}
