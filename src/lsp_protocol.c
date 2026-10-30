/* Language Server Protocol messages (see lsp_protocol.h) */
#include "lsp_protocol.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Framing                                                                   */
/* ------------------------------------------------------------------------- */

gchar* lsp_frame(const gchar* body, gsize* out_len) {
    gsize n = strlen(body);
    gchar* msg = g_strdup_printf("Content-Length: %" G_GSIZE_FORMAT "\r\n\r\n%s", n, body);
    if (out_len != NULL) *out_len = strlen(msg);
    return msg;
}

struct LspReader {
    GByteArray* buf;
};

LspReader* lsp_reader_new(void) {
    LspReader* r = g_new0(LspReader, 1);
    r->buf = g_byte_array_new();
    return r;
}

void lsp_reader_free(LspReader* reader) {
    if (reader == NULL) return;
    g_byte_array_free(reader->buf, TRUE);
    g_free(reader);
}

void lsp_reader_feed(LspReader* reader, const gchar* data, gsize len) {
    g_byte_array_append(reader->buf, (const guint8*)data, (guint)len);
}

gchar* lsp_reader_next(LspReader* reader, gsize* len) {
    for (;;) {
        const gchar* data = (const gchar*)reader->buf->data;
        gsize size = reader->buf->len;
        const gchar* end = NULL;
        for (gsize i = 0; i + 3 < size; i++) {
            if (memcmp(data + i, "\r\n\r\n", 4) == 0) {
                end = data + i;
                break;
            }
        }
        if (end == NULL) return NULL;
        gsize header_len = (gsize)(end - data) + 4;
        gchar* headers = g_strndup(data, (gsize)(end - data));
        gssize body_len = -1;
        gchar** lines = g_strsplit(headers, "\r\n", -1);
        for (gint i = 0; lines[i] != NULL; i++) {
            if (g_ascii_strncasecmp(lines[i], "Content-Length:", 15) == 0) {
                body_len = (gssize)g_ascii_strtoll(lines[i] + 15, NULL, 10);
            }
        }
        g_strfreev(lines);
        g_free(headers);
        if (body_len < 0) {
            /* Broken header: drop it and look for the next message */
            g_byte_array_remove_range(reader->buf, 0, (guint)header_len);
            continue;
        }
        if (size < header_len + (gsize)body_len) return NULL;
        gchar* body = g_strndup(data + header_len, (gsize)body_len);
        g_byte_array_remove_range(reader->buf, 0, (guint)(header_len + (gsize)body_len));
        if (len != NULL) *len = (gsize)body_len;
        return body;
    }
}

/* ------------------------------------------------------------------------- */
/* JSON writing                                                              */
/* ------------------------------------------------------------------------- */

void lsp_json_append_string(GString* out, const gchar* s) {
    g_string_append_c(out, '"');
    for (const guchar* p = (const guchar*)(s != NULL ? s : ""); *p != '\0'; p++) {
        switch (*p) {
            case '"': g_string_append(out, "\\\""); break;
            case '\\': g_string_append(out, "\\\\"); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': g_string_append(out, "\\r"); break;
            case '\t': g_string_append(out, "\\t"); break;
            case '\b': g_string_append(out, "\\b"); break;
            case '\f': g_string_append(out, "\\f"); break;
            default:
                if (*p < 0x20) g_string_append_printf(out, "\\u%04x", *p);
                else g_string_append_c(out, (gchar)*p);
                break;
        }
    }
    g_string_append_c(out, '"');
}

gchar* lsp_path_to_uri(const gchar* path) {
    return g_filename_to_uri(path, NULL, NULL);
}

gchar* lsp_uri_to_path(const gchar* uri) {
    if (uri == NULL || !g_str_has_prefix(uri, "file:")) return NULL;
    return g_filename_from_uri(uri, NULL, NULL);
}

const gchar* lsp_language_id(const gchar* path) {
    const gchar* dot = path != NULL ? strrchr(path, '.') : NULL;
    if (dot != NULL) {
        if (strcmp(dot, ".ts") == 0 || strcmp(dot, ".mts") == 0 || strcmp(dot, ".cts") == 0) return "typescript";
        if (strcmp(dot, ".tsx") == 0) return "typescriptreact";
        if (strcmp(dot, ".jsx") == 0) return "javascriptreact";
    }
    switch (code_lang_from_name(path)) {
        case CODE_LANG_C: return "c";
        case CODE_LANG_CPP: return "cpp";
        case CODE_LANG_OBJC: return "objective-c";
        case CODE_LANG_JAVA: return "java";
        case CODE_LANG_CSHARP: return "csharp";
        case CODE_LANG_PYTHON: return "python";
        case CODE_LANG_SHELL: return "shellscript";
        case CODE_LANG_JS: return "javascript";
        case CODE_LANG_GO: return "go";
        case CODE_LANG_RUST: return "rust";
        case CODE_LANG_LUA: return "lua";
        case CODE_LANG_RUBY: return "ruby";
        case CODE_LANG_MAKE: return "makefile";
        case CODE_LANG_PHP: return "php";
        case CODE_LANG_PERL: return "perl";
        case CODE_LANG_KOTLIN: return "kotlin";
        case CODE_LANG_SWIFT: return "swift";
        case CODE_LANG_SCALA: return "scala";
        case CODE_LANG_DART: return "dart";
        case CODE_LANG_HASKELL: return "haskell";
        case CODE_LANG_ELIXIR: return "elixir";
        case CODE_LANG_ERLANG: return "erlang";
        case CODE_LANG_JULIA: return "julia";
        case CODE_LANG_R: return "r";
        case CODE_LANG_TCL: return "tcl";
        case CODE_LANG_NONE: break;
    }
    return "plaintext";
}

gchar* lsp_request(gint64 id, const gchar* method, const gchar* params_json) {
    GString* s = g_string_new("{\"jsonrpc\":\"2.0\",\"id\":");
    g_string_append_printf(s, "%" G_GINT64_FORMAT ",\"method\":", id);
    lsp_json_append_string(s, method);
    g_string_append_printf(s, ",\"params\":%s}", params_json != NULL ? params_json : "null");
    return g_string_free(s, FALSE);
}

gchar* lsp_notification(const gchar* method, const gchar* params_json) {
    GString* s = g_string_new("{\"jsonrpc\":\"2.0\",\"method\":");
    lsp_json_append_string(s, method);
    g_string_append_printf(s, ",\"params\":%s}", params_json != NULL ? params_json : "null");
    return g_string_free(s, FALSE);
}

gchar* lsp_reply(const gchar* id_json, const gchar* result_json) {
    return g_strdup_printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}", id_json,
                           result_json != NULL ? result_json : "null");
}

static void append_uri(GString* s, const gchar* path) {
    gchar* uri = lsp_path_to_uri(path);
    lsp_json_append_string(s, uri != NULL ? uri : "");
    g_free(uri);
}

gchar* lsp_params_initialize(gint pid, const gchar* root_path) {
    GString* s = g_string_new(NULL);
    g_string_append_printf(s, "{\"processId\":%d,\"clientInfo\":{\"name\":\"Lookin\"},\"rootPath\":", pid);
    lsp_json_append_string(s, root_path);
    g_string_append(s, ",\"rootUri\":");
    append_uri(s, root_path);
    g_string_append(s, ",\"workspaceFolders\":[{\"uri\":");
    append_uri(s, root_path);
    gchar* base = g_path_get_basename(root_path);
    g_string_append(s, ",\"name\":");
    lsp_json_append_string(s, base);
    g_free(base);
    g_string_append(s,
                    "}],\"capabilities\":{"
                    "\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-16\"]},"
                    "\"textDocument\":{"
                    "\"synchronization\":{\"didSave\":false,\"dynamicRegistration\":false},"
                    "\"hover\":{\"contentFormat\":[\"markdown\",\"plaintext\"]},"
                    "\"signatureHelp\":{\"signatureInformation\":{\"documentationFormat\":[\"markdown\",\"plaintext\"],"
                    "\"parameterInformation\":{\"labelOffsetSupport\":true},\"activeParameterSupport\":true}},"
                    "\"definition\":{\"linkSupport\":true},\"declaration\":{\"linkSupport\":true},"
                    "\"references\":{},"
                    "\"documentSymbol\":{\"hierarchicalDocumentSymbolSupport\":true},"
                    "\"publishDiagnostics\":{}},"
                    "\"workspace\":{\"symbol\":{},\"workspaceFolders\":true,\"configuration\":false},"
                    "\"window\":{\"workDoneProgress\":false}}}");
    return g_string_free(s, FALSE);
}

static void append_document(GString* s, const gchar* path) {
    g_string_append(s, "\"textDocument\":{\"uri\":");
    append_uri(s, path);
}

gchar* lsp_params_did_open(const gchar* path, const gchar* language_id, gint version, const gchar* text) {
    GString* s = g_string_new("{");
    append_document(s, path);
    g_string_append(s, ",\"languageId\":");
    lsp_json_append_string(s, language_id);
    g_string_append_printf(s, ",\"version\":%d,\"text\":", version);
    lsp_json_append_string(s, text);
    g_string_append(s, "}}");
    return g_string_free(s, FALSE);
}

gchar* lsp_params_did_change(const gchar* path, gint version, const gchar* text) {
    GString* s = g_string_new("{");
    append_document(s, path);
    g_string_append_printf(s, ",\"version\":%d},\"contentChanges\":[{\"text\":", version);
    lsp_json_append_string(s, text);
    g_string_append(s, "}]}");
    return g_string_free(s, FALSE);
}

gchar* lsp_params_document(const gchar* path) {
    GString* s = g_string_new("{");
    append_document(s, path);
    g_string_append(s, "}}");
    return g_string_free(s, FALSE);
}

gchar* lsp_params_did_close(const gchar* path) {
    return lsp_params_document(path);
}

gchar* lsp_params_position(const gchar* path, gint line, gint column, gint include_declaration) {
    GString* s = g_string_new("{");
    append_document(s, path);
    g_string_append_printf(s, "},\"position\":{\"line\":%d,\"character\":%d}", line, column);
    if (include_declaration >= 0) {
        g_string_append_printf(s, ",\"context\":{\"includeDeclaration\":%s}", include_declaration ? "true" : "false");
    }
    g_string_append_c(s, '}');
    return g_string_free(s, FALSE);
}

gchar* lsp_params_workspace_symbol(const gchar* query) {
    GString* s = g_string_new("{\"query\":");
    lsp_json_append_string(s, query);
    g_string_append_c(s, '}');
    return g_string_free(s, FALSE);
}

gchar* lsp_params_cancel(gint64 id) {
    return g_strdup_printf("{\"id\":%" G_GINT64_FORMAT "}", id);
}

/* ------------------------------------------------------------------------- */
/* Incoming messages                                                         */
/* ------------------------------------------------------------------------- */

static gchar* dup_member(const JsonValue* obj, const gchar* key) {
    const JsonValue* v = obj != NULL ? json_value_get(obj, key) : NULL;
    if (v == NULL || json_value_get_type(v) == JSON_VALUE_OBJECT || json_value_get_type(v) == JSON_VALUE_ARRAY) {
        return NULL;
    }
    gchar* s = json_value_dup_string(v);
    if (json_value_get_type(v) == JSON_VALUE_KEYWORD && g_strcmp0(s, "null") == 0) {
        g_free(s);
        return NULL;
    }
    return s;
}

LspMessage* lsp_message_parse(gchar* body, gsize len) {
    JsonValue* root = json_value_parse(body, len, NULL);
    if (root == NULL || json_value_get_type(root) != JSON_VALUE_OBJECT) {
        if (root != NULL) json_value_free(root);
        g_free(body);
        return NULL;
    }
    LspMessage* m = g_new0(LspMessage, 1);
    m->body = body;
    m->root = root;
    m->method = dup_member(root, "method");
    m->id = -1;
    const JsonValue* id = json_value_get(root, "id");
    if (id != NULL && json_value_get_type(id) != JSON_VALUE_KEYWORD) {
        gchar* raw = json_value_dup_string(id);
        if (m->method != NULL) {
            if (json_value_get_type(id) == JSON_VALUE_STRING) {
                GString* s = g_string_new(NULL);
                lsp_json_append_string(s, raw);
                m->id_json = g_string_free(s, FALSE);
            } else {
                m->id_json = g_strdup(raw);
            }
        } else if (json_value_get_type(id) == JSON_VALUE_NUMBER) {
            m->id = (gint64)json_value_get_number(id);
        }
        g_free(raw);
    }
    m->result = json_value_get(root, "result");
    m->error = json_value_get(root, "error");
    m->params = json_value_get(root, "params");
    return m;
}

void lsp_message_free(LspMessage* msg) {
    if (msg == NULL) return;
    json_value_free(msg->root);
    g_free(msg->body);
    g_free(msg->method);
    g_free(msg->id_json);
    g_free(msg);
}

/* ------------------------------------------------------------------------- */
/* Results                                                                   */
/* ------------------------------------------------------------------------- */

static gboolean is_null(const JsonValue* v) {
    if (v == NULL) return TRUE;
    if (json_value_get_type(v) != JSON_VALUE_KEYWORD) return FALSE;
    gchar* s = json_value_dup_string(v);
    gboolean null = g_strcmp0(s, "null") == 0 || g_strcmp0(s, "false") == 0;
    g_free(s);
    return null;
}

/* A capability: present and not null / false */
static gboolean provides(const JsonValue* caps, const gchar* key) {
    return caps != NULL && !is_null(json_value_get(caps, key));
}

LspCapabilities* lsp_parse_capabilities(const JsonValue* result) {
    LspCapabilities* c = g_new0(LspCapabilities, 1);
    const JsonValue* caps = result != NULL ? json_value_get(result, "capabilities") : NULL;
    gchar* enc = dup_member(caps, "positionEncoding");
    c->utf8_positions = g_strcmp0(enc, "utf-8") == 0;
    g_free(enc);
    c->definition = provides(caps, "definitionProvider");
    c->declaration = provides(caps, "declarationProvider");
    c->references = provides(caps, "referencesProvider");
    c->hover = provides(caps, "hoverProvider");
    c->document_symbol = provides(caps, "documentSymbolProvider");
    c->workspace_symbol = provides(caps, "workspaceSymbolProvider");
    c->signature_help = provides(caps, "signatureHelpProvider");
    GString* triggers = g_string_new(NULL);
    const JsonValue* sig = caps != NULL ? json_value_get(caps, "signatureHelpProvider") : NULL;
    const JsonValue* tr = sig != NULL ? json_value_get(sig, "triggerCharacters") : NULL;
    for (guint i = 0; tr != NULL && json_value_get_type(tr) == JSON_VALUE_ARRAY && i < json_value_get_length(tr); i++) {
        gchar* t = json_value_dup_string(json_value_get_item(tr, i));
        if (t != NULL) g_string_append(triggers, t);
        g_free(t);
    }
    tr = sig != NULL ? json_value_get(sig, "retriggerCharacters") : NULL;
    for (guint i = 0; tr != NULL && json_value_get_type(tr) == JSON_VALUE_ARRAY && i < json_value_get_length(tr); i++) {
        gchar* t = json_value_dup_string(json_value_get_item(tr, i));
        if (t != NULL && strstr(triggers->str, t) == NULL) g_string_append(triggers, t);
        g_free(t);
    }
    c->signature_triggers = g_string_free(triggers, FALSE);
    const JsonValue* info = result != NULL ? json_value_get(result, "serverInfo") : NULL;
    c->server_name = dup_member(info, "name");
    return c;
}

void lsp_capabilities_free(LspCapabilities* caps) {
    if (caps == NULL) return;
    g_free(caps->signature_triggers);
    g_free(caps->server_name);
    g_free(caps);
}

void lsp_location_free(LspLocation* loc) {
    if (loc == NULL) return;
    g_free(loc->path);
    g_free(loc);
}

static gboolean read_position(const JsonValue* range, gint* line, gint* column) {
    const JsonValue* start = range != NULL ? json_value_get(range, "start") : NULL;
    if (start == NULL) return FALSE;
    const JsonValue* l = json_value_get(start, "line");
    const JsonValue* c = json_value_get(start, "character");
    if (l == NULL || c == NULL) return FALSE;
    *line = (gint)json_value_get_number(l);
    *column = (gint)json_value_get_number(c);
    return TRUE;
}

static void add_location(GPtrArray* out, const JsonValue* item) {
    if (item == NULL || json_value_get_type(item) != JSON_VALUE_OBJECT) return;
    gboolean link = json_value_get(item, "targetUri") != NULL;
    gchar* uri = dup_member(item, link ? "targetUri" : "uri");
    const JsonValue* range = json_value_get(item, link ? "targetSelectionRange" : "range");
    if (range == NULL && link) range = json_value_get(item, "targetRange");
    gint line = 0, col = 0;
    gchar* path = lsp_uri_to_path(uri);
    if (path != NULL && read_position(range, &line, &col)) {
        LspLocation* loc = g_new0(LspLocation, 1);
        loc->path = path;
        loc->line = line;
        loc->column = col;
        g_ptr_array_add(out, loc);
    } else {
        g_free(path);
    }
    g_free(uri);
}

GPtrArray* lsp_parse_locations(const JsonValue* result) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_location_free);
    if (result == NULL) return out;
    if (json_value_get_type(result) == JSON_VALUE_ARRAY) {
        for (guint i = 0; i < json_value_get_length(result); i++) add_location(out, json_value_get_item(result, i));
    } else {
        add_location(out, result);
    }
    return out;
}

/* One MarkedString / MarkupContent as Markdown */
static gchar* marked_string(const JsonValue* v) {
    if (v == NULL) return NULL;
    if (json_value_get_type(v) == JSON_VALUE_STRING) return json_value_dup_string(v);
    if (json_value_get_type(v) != JSON_VALUE_OBJECT) return NULL;
    gchar* value = dup_member(v, "value");
    gchar* language = dup_member(v, "language");
    gchar* out = value;
    if (language != NULL && value != NULL) {
        out = g_strdup_printf("```%s\n%s\n```", language, value);
        g_free(value);
    }
    g_free(language);
    return out;
}

gchar* lsp_parse_hover(const JsonValue* result) {
    if (is_null(result)) return NULL;
    const JsonValue* contents = json_value_get(result, "contents");
    if (contents == NULL) return NULL;
    gchar* out = NULL;
    if (json_value_get_type(contents) == JSON_VALUE_ARRAY) {
        GString* s = g_string_new(NULL);
        for (guint i = 0; i < json_value_get_length(contents); i++) {
            gchar* part = marked_string(json_value_get_item(contents, i));
            if (part != NULL && part[0] != '\0') {
                if (s->len > 0) g_string_append(s, "\n\n");
                g_string_append(s, part);
            }
            g_free(part);
        }
        out = g_string_free(s, FALSE);
    } else {
        out = marked_string(contents);
    }
    if (out != NULL && g_strstrip(out)[0] == '\0') {
        g_free(out);
        out = NULL;
    }
    return out;
}

void lsp_signature_free(LspSignature* sig) {
    if (sig == NULL) return;
    g_free(sig->label);
    g_free(sig->documentation);
    g_free(sig);
}

/* Byte offset in s of a UTF-16 offset */
static gint utf16_to_byte(const gchar* s, gint units) {
    return lsp_column_to_byte(s, -1, units, FALSE);
}

LspSignature* lsp_parse_signature_help(const JsonValue* result) {
    if (is_null(result)) return NULL;
    const JsonValue* sigs = json_value_get(result, "signatures");
    if (sigs == NULL || json_value_get_type(sigs) != JSON_VALUE_ARRAY || json_value_get_length(sigs) == 0) return NULL;
    guint n = json_value_get_length(sigs);
    const JsonValue* av = json_value_get(result, "activeSignature");
    guint active = av != NULL ? (guint)json_value_get_number(av) : 0;
    if (active >= n) active = 0;
    const JsonValue* s = json_value_get_item(sigs, active);
    LspSignature* sig = g_new0(LspSignature, 1);
    sig->label = dup_member(s, "label");
    if (sig->label == NULL) sig->label = g_strdup("");
    const JsonValue* doc = json_value_get(s, "documentation");
    sig->documentation = marked_string(doc);
    sig->count = n;
    sig->param_start = sig->param_end = -1;
    const JsonValue* ap = json_value_get(s, "activeParameter");
    if (ap == NULL || is_null(ap)) ap = json_value_get(result, "activeParameter");
    const JsonValue* params = json_value_get(s, "parameters");
    if (ap != NULL && params != NULL && json_value_get_type(params) == JSON_VALUE_ARRAY) {
        guint index = (guint)json_value_get_number(ap);
        if (index < json_value_get_length(params)) {
            const JsonValue* label = json_value_get(json_value_get_item(params, index), "label");
            if (label != NULL && json_value_get_type(label) == JSON_VALUE_ARRAY && json_value_get_length(label) == 2) {
                sig->param_start = utf16_to_byte(sig->label, (gint)json_value_get_number(json_value_get_item(label, 0)));
                sig->param_end = utf16_to_byte(sig->label, (gint)json_value_get_number(json_value_get_item(label, 1)));
            } else if (label != NULL && json_value_get_type(label) == JSON_VALUE_STRING) {
                gchar* text = json_value_dup_string(label);
                /* Search after the opening parenthesis (the name may contain it) */
                const gchar* from = strchr(sig->label, '(');
                const gchar* hit = text[0] != '\0' ? strstr(from != NULL ? from : sig->label, text) : NULL;
                if (hit != NULL) {
                    sig->param_start = (gint)(hit - sig->label);
                    sig->param_end = sig->param_start + (gint)strlen(text);
                }
                g_free(text);
            }
        }
    }
    return sig;
}

CodeSymbolKind lsp_symbol_kind(gint kind) {
    switch (kind) {
        case 2: case 3: case 4: return CODE_SYM_MODULE;        /* module, namespace, package */
        case 5: return CODE_SYM_CLASS;                          /* class */
        case 6: case 9: return CODE_SYM_METHOD;                 /* method, constructor */
        case 7: case 8: return CODE_SYM_FIELD;                  /* property, field */
        case 10: case 23: case 26: return CODE_SYM_TYPE;        /* enum, struct, type parameter */
        case 11: return CODE_SYM_CLASS;                         /* interface */
        case 12: return CODE_SYM_FUNCTION;                      /* function */
        case 13: return CODE_SYM_VARIABLE;                      /* variable */
        case 14: case 22: return CODE_SYM_CONSTANT;             /* constant, enum member */
        default: return CODE_SYM_VARIABLE;
    }
}

void lsp_symbol_free(LspSymbol* sym) {
    if (sym == NULL) return;
    g_free(sym->name);
    g_free(sym->detail);
    g_free(sym->container);
    g_free(sym->path);
    g_free(sym);
}

static void add_symbols(GPtrArray* out, const JsonValue* list, const gchar* container) {
    for (guint i = 0; list != NULL && json_value_get_type(list) == JSON_VALUE_ARRAY && i < json_value_get_length(list); i++) {
        const JsonValue* item = json_value_get_item(list, i);
        LspSymbol* sym = g_new0(LspSymbol, 1);
        sym->name = dup_member(item, "name");
        sym->detail = dup_member(item, "detail");
        const JsonValue* k = json_value_get(item, "kind");
        sym->kind = lsp_symbol_kind(k != NULL ? (gint)json_value_get_number(k) : 0);
        const JsonValue* location = json_value_get(item, "location");
        gboolean ok;
        if (location != NULL) {
            /* SymbolInformation */
            gchar* uri = dup_member(location, "uri");
            sym->path = lsp_uri_to_path(uri);
            g_free(uri);
            ok = read_position(json_value_get(location, "range"), &sym->line, &sym->column);
            sym->container = dup_member(item, "containerName");
        } else {
            const JsonValue* range = json_value_get(item, "selectionRange");
            if (range == NULL) range = json_value_get(item, "range");
            ok = read_position(range, &sym->line, &sym->column);
            sym->container = g_strdup(container);
        }
        if (!ok || sym->name == NULL) {
            lsp_symbol_free(sym);
            continue;
        }
        g_ptr_array_add(out, sym);
        add_symbols(out, json_value_get(item, "children"), sym->name);
    }
}

GPtrArray* lsp_parse_symbols(const JsonValue* result) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_symbol_free);
    add_symbols(out, result, NULL);
    return out;
}

void lsp_diagnostic_free(LspDiagnostic* diag) {
    if (diag == NULL) return;
    g_free(diag->message);
    g_free(diag->source);
    g_free(diag);
}

GPtrArray* lsp_parse_diagnostics(const JsonValue* params, gchar** path) {
    *path = NULL;
    if (params == NULL) return NULL;
    gchar* uri = dup_member(params, "uri");
    *path = lsp_uri_to_path(uri);
    g_free(uri);
    if (*path == NULL) return NULL;
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_diagnostic_free);
    const JsonValue* list = json_value_get(params, "diagnostics");
    for (guint i = 0; list != NULL && json_value_get_type(list) == JSON_VALUE_ARRAY && i < json_value_get_length(list); i++) {
        const JsonValue* d = json_value_get_item(list, i);
        const JsonValue* range = json_value_get(d, "range");
        LspDiagnostic* diag = g_new0(LspDiagnostic, 1);
        if (!read_position(range, &diag->line, &diag->column)) {
            lsp_diagnostic_free(diag);
            continue;
        }
        const JsonValue* end = range != NULL ? json_value_get(range, "end") : NULL;
        const JsonValue* el = end != NULL ? json_value_get(end, "line") : NULL;
        const JsonValue* ec = end != NULL ? json_value_get(end, "character") : NULL;
        diag->end_line = el != NULL ? (gint)json_value_get_number(el) : diag->line;
        diag->end_column = ec != NULL ? (gint)json_value_get_number(ec) : diag->column;
        const JsonValue* sev = json_value_get(d, "severity");
        diag->severity = sev != NULL ? (LspSeverity)(gint)json_value_get_number(sev) : LSP_SEVERITY_ERROR;
        if (diag->severity < LSP_SEVERITY_ERROR || diag->severity > LSP_SEVERITY_HINT) diag->severity = LSP_SEVERITY_ERROR;
        diag->message = dup_member(d, "message");
        if (diag->message == NULL) diag->message = g_strdup("");
        diag->source = dup_member(d, "source");
        g_ptr_array_add(out, diag);
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Positions                                                                 */
/* ------------------------------------------------------------------------- */

gint lsp_column_to_byte(const gchar* line, gssize len, gint column, gboolean utf8) {
    if (line == NULL || column <= 0) return 0;
    if (len < 0) len = (gssize)strlen(line);
    /* The line ends at its newline */
    const gchar* nl = memchr(line, '\n', (gsize)len);
    if (nl != NULL) len = nl - line;
    if (utf8) return MIN(column, (gint)len);
    const gchar* p = line;
    const gchar* end = line + len;
    gint units = 0;
    while (p < end && units < column) {
        gunichar c = g_utf8_get_char_validated(p, end - p);
        if (c == (gunichar)-1 || c == (gunichar)-2) {
            p++;
            units++;
            continue;
        }
        units += c >= 0x10000 ? 2 : 1;
        p = g_utf8_next_char(p);
    }
    return (gint)(MIN(p, end) - line);
}

gint lsp_byte_to_column(const gchar* line, gssize len, gint byte, gboolean utf8) {
    if (line == NULL || byte <= 0) return 0;
    if (len < 0) len = (gssize)strlen(line);
    if (byte > len) byte = (gint)len;
    if (utf8) return byte;
    const gchar* p = line;
    const gchar* end = line + byte;
    gint units = 0;
    while (p < end) {
        gunichar c = g_utf8_get_char_validated(p, end - p);
        if (c == (gunichar)-1 || c == (gunichar)-2) {
            p++;
            units++;
            continue;
        }
        units += c >= 0x10000 ? 2 : 1;
        p = g_utf8_next_char(p);
    }
    return units;
}
