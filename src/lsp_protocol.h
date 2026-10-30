#ifndef LSP_PROTOCOL_H
#define LSP_PROTOCOL_H

/* Language Server Protocol messages (TASK_CODE_NAV.md, phase 2): JSON-RPC
 * framing, request parameters, and the results code navigation uses
 * (locations, hover, signature help, symbols, server capabilities).
 * Positions: LSP columns count UTF-16 code units unless the server chose
 * UTF-8 ("positionEncoding"); the conversions take the line text. Pure
 * GLib, unit-tested. */
#include <glib.h>

#include "code_symbol.h"
#include "json_view.h"

/* --- Framing --- */

/* "Content-Length: N\r\n\r\n" + body */
gchar* lsp_frame(const gchar* body, gsize* out_len);

typedef struct LspReader LspReader;
LspReader* lsp_reader_new(void);
void lsp_reader_free(LspReader* reader);
void lsp_reader_feed(LspReader* reader, const gchar* data, gsize len);
/* Next complete message body (caller frees), NULL when more data is needed */
gchar* lsp_reader_next(LspReader* reader, gsize* len);

/* --- JSON writing --- */

/* Append s as a JSON string literal (quotes included) */
void lsp_json_append_string(GString* out, const gchar* s);
/* file:// URI of an absolute path, and back (NULL when not a file URI) */
gchar* lsp_path_to_uri(const gchar* path);
gchar* lsp_uri_to_path(const gchar* uri);

/* LSP languageId of a file ("c", "cpp", "python", "typescriptreact"...) */
const gchar* lsp_language_id(const gchar* path);

/* Complete messages (caller frees) */
gchar* lsp_request(gint64 id, const gchar* method, const gchar* params_json);
gchar* lsp_notification(const gchar* method, const gchar* params_json);
/* Reply to a server request; id_json is the request's id as JSON */
gchar* lsp_reply(const gchar* id_json, const gchar* result_json);

/* Parameters (JSON objects, caller frees) */
gchar* lsp_params_initialize(gint pid, const gchar* root_path);
gchar* lsp_params_did_open(const gchar* path, const gchar* language_id, gint version, const gchar* text);
gchar* lsp_params_did_change(const gchar* path, gint version, const gchar* text);
gchar* lsp_params_did_close(const gchar* path);
/* textDocument + position (definition, declaration, hover, signatureHelp,
 * and references when include_declaration >= 0) */
gchar* lsp_params_position(const gchar* path, gint line, gint column, gint include_declaration);
gchar* lsp_params_document(const gchar* path);
gchar* lsp_params_workspace_symbol(const gchar* query);
gchar* lsp_params_cancel(gint64 id);

/* --- Incoming messages --- */

typedef struct {
    gchar* body;              /* owns the text the JSON tree points into */
    JsonValue* root;
    gchar* method;            /* requests and notifications, NULL for responses */
    gint64 id;                /* numeric id of a response, -1 otherwise */
    gchar* id_json;           /* id of a server request as JSON (to reply), or NULL */
    const JsonValue* result;  /* responses */
    const JsonValue* error;   /* error responses */
    const JsonValue* params;  /* requests and notifications */
} LspMessage;

/* Parse a message body (taken); NULL when it is not a JSON-RPC message */
LspMessage* lsp_message_parse(gchar* body, gsize len);
void lsp_message_free(LspMessage* msg);

/* --- Results --- */

typedef struct {
    gboolean utf8_positions;   /* positionEncoding "utf-8" */
    gboolean definition;
    gboolean declaration;
    gboolean references;
    gboolean hover;
    gboolean document_symbol;
    gboolean workspace_symbol;
    gboolean signature_help;
    gchar* signature_triggers; /* trigger characters, e.g. "(," */
    gchar* server_name;        /* serverInfo.name, or NULL */
} LspCapabilities;

/* From the initialize result */
LspCapabilities* lsp_parse_capabilities(const JsonValue* result);
void lsp_capabilities_free(LspCapabilities* caps);

typedef struct {
    gchar* path;
    gint line;      /* 0-based */
    gint column;    /* in the server's encoding: convert with lsp_column_to_byte() */
} LspLocation;

void lsp_location_free(LspLocation* loc);
/* Location, Location[] or LocationLink[] (null: empty); LspLocation* */
GPtrArray* lsp_parse_locations(const JsonValue* result);

/* Hover contents as Markdown (plain text is returned as is), or NULL */
gchar* lsp_parse_hover(const JsonValue* result);

typedef struct {
    gchar* label;          /* "int add(int a, int b)" */
    gchar* documentation;  /* Markdown, or NULL */
    gint param_start;      /* byte range of the active parameter in label, -1 when unknown */
    gint param_end;
    guint count;           /* number of signatures (overloads) */
} LspSignature;

void lsp_signature_free(LspSignature* sig);
/* The active signature of a SignatureHelp, or NULL */
LspSignature* lsp_parse_signature_help(const JsonValue* result);

typedef struct {
    gchar* name;
    gchar* detail;      /* type / signature, or NULL */
    gchar* container;   /* enclosing symbol or NULL */
    gchar* path;        /* workspace symbols; NULL for document symbols */
    CodeSymbolKind kind;
    gint line;
    gint column;        /* server encoding */
} LspSymbol;

void lsp_symbol_free(LspSymbol* sym);
/* DocumentSymbol[] (flattened, children after their parent) or
 * SymbolInformation[]; LspSymbol* */
GPtrArray* lsp_parse_symbols(const JsonValue* result);
/* CodeSymbolKind of an LSP SymbolKind number */
CodeSymbolKind lsp_symbol_kind(gint kind);

/* --- Positions --- */

/* Byte index in line of an LSP column, and back (utf8: the server counts
 * bytes); clamped to the line */
gint lsp_column_to_byte(const gchar* line, gssize len, gint column, gboolean utf8);
gint lsp_byte_to_column(const gchar* line, gssize len, gint byte, gboolean utf8);

#endif /* LSP_PROTOCOL_H */
