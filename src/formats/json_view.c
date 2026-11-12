/* JSON pretty-printing for the preview (see json_view.h). */
#include "formats/json_view.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

typedef enum { J_OBJECT, J_ARRAY, J_STRING, J_NUMBER, J_KEYWORD } JsonType;

typedef struct JsonValue JsonNode;
struct JsonValue {
    JsonType type;
    const gchar* text;     /* raw token (strings with their quotes) */
    gsize len;
    const gchar* key;      /* member name as written (quoted or not), or NULL */
    gsize key_len;
    GPtrArray* children;   /* objects and arrays */
};

#define MAX_DEPTH 256

typedef struct {
    const gchar* text;
    gsize len;
    gsize pos;
    GError** error;
} Parser;

static void node_free(gpointer data) {
    JsonNode* node = data;
    if (node->children != NULL) {
        g_ptr_array_free(node->children, TRUE);
    }
    g_free(node);
}

static gboolean fail(Parser* p, const gchar* what) {
    if (p->error != NULL && *p->error == NULL) {
        guint line = 1;
        guint col = 1;
        for (gsize i = 0; i < p->pos && i < p->len; i++) {
            if (p->text[i] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
        }
        g_set_error(p->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("line %1$u, column %2$u: %3$s"),
                    line, col, what);
    }
    return FALSE;
}

/* Skip blanks and comments; stop_at_newline: stop before a line break */
static void skip_space(Parser* p, gboolean stop_at_newline) {
    while (p->pos < p->len) {
        gchar c = p->text[p->pos];
        if (c == '\n' && stop_at_newline) {
            return;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else if (c == '/' && p->pos + 1 < p->len && p->text[p->pos + 1] == '/') {
            while (p->pos < p->len && p->text[p->pos] != '\n') {
                p->pos++;
            }
        } else if (c == '/' && p->pos + 1 < p->len && p->text[p->pos + 1] == '*') {
            const gchar* end = g_strstr_len(p->text + p->pos + 2, (gssize)(p->len - p->pos - 2), "*/");
            p->pos = end != NULL ? (gsize)(end - p->text) + 2 : p->len;
        } else if ((guchar)c == 0xEF && p->pos + 2 < p->len && (guchar)p->text[p->pos + 1] == 0xBB &&
                   (guchar)p->text[p->pos + 2] == 0xBF) {
            p->pos += 3; /* byte order mark */
        } else {
            return;
        }
    }
}

static gboolean scan_string(Parser* p) {
    gchar quote = p->text[p->pos];
    p->pos++;
    while (p->pos < p->len) {
        gchar c = p->text[p->pos];
        if (c == '\\') {
            p->pos += 2;
            continue;
        }
        p->pos++;
        if (c == quote) {
            return TRUE;
        }
        if (c == '\n') {
            break;
        }
    }
    return fail(p, _("unterminated string"));
}

static gboolean is_word_char(gchar c) {
    return g_ascii_isalnum(c) || c == '_' || c == '$' || c == '-' || c == '+' || c == '.';
}

static JsonNode* parse_value(Parser* p, guint depth);

static JsonNode* parse_container(Parser* p, guint depth) {
    gboolean object = (p->text[p->pos] == '{');
    gchar close = object ? '}' : ']';
    JsonNode* node = g_new0(JsonNode, 1);
    node->type = object ? J_OBJECT : J_ARRAY;
    node->children = g_ptr_array_new_with_free_func(node_free);
    p->pos++;
    for (;;) {
        skip_space(p, FALSE);
        if (p->pos >= p->len) {
            fail(p, object ? _("missing }") : _("missing ]"));
            node_free(node);
            return NULL;
        }
        if (p->text[p->pos] == close) {
            p->pos++;
            return node;
        }
        const gchar* key = NULL;
        gsize key_len = 0;
        if (object) {
            gsize start = p->pos;
            gchar c = p->text[p->pos];
            if (c == '"' || c == '\'') {
                if (!scan_string(p)) {
                    node_free(node);
                    return NULL;
                }
            } else {
                while (p->pos < p->len && is_word_char(p->text[p->pos])) {
                    p->pos++;
                }
                if (p->pos == start) {
                    fail(p, _("expected a member name"));
                    node_free(node);
                    return NULL;
                }
            }
            key = p->text + start;
            key_len = p->pos - start;
            skip_space(p, FALSE);
            if (p->pos >= p->len || p->text[p->pos] != ':') {
                fail(p, _("expected :"));
                node_free(node);
                return NULL;
            }
            p->pos++;
        }
        JsonNode* child = parse_value(p, depth + 1);
        if (child == NULL) {
            node_free(node);
            return NULL;
        }
        child->key = key;
        child->key_len = key_len;
        g_ptr_array_add(node->children, child);
        skip_space(p, FALSE);
        if (p->pos < p->len && p->text[p->pos] == ',') {
            p->pos++;
        } else if (p->pos < p->len && p->text[p->pos] != close) {
            fail(p, object ? _("expected , or }") : _("expected , or ]"));
            node_free(node);
            return NULL;
        }
    }
}

static JsonNode* parse_value(Parser* p, guint depth) {
    skip_space(p, FALSE);
    if (p->pos >= p->len) {
        fail(p, _("expected a value"));
        return NULL;
    }
    if (depth > MAX_DEPTH) {
        fail(p, _("nested too deeply"));
        return NULL;
    }
    gchar c = p->text[p->pos];
    if (c == '{' || c == '[') {
        return parse_container(p, depth);
    }
    gsize start = p->pos;
    JsonType type;
    if (c == '"' || c == '\'') {
        if (!scan_string(p)) {
            return NULL;
        }
        type = J_STRING;
    } else if (g_ascii_isdigit(c) || c == '-' || c == '+' || c == '.') {
        while (p->pos < p->len && is_word_char(p->text[p->pos])) {
            p->pos++;
        }
        type = J_NUMBER;
    } else if (g_ascii_isalpha(c)) {
        while (p->pos < p->len && g_ascii_isalpha(p->text[p->pos])) {
            p->pos++;
        }
        gsize n = p->pos - start;
        const gchar* w = p->text + start;
        if (!((n == 4 && (memcmp(w, "true", 4) == 0 || memcmp(w, "null", 4) == 0)) ||
              (n == 5 && memcmp(w, "false", 5) == 0) || (n == 3 && memcmp(w, "NaN", 3) == 0) ||
              (n == 8 && memcmp(w, "Infinity", 8) == 0))) {
            p->pos = start;
            fail(p, _("unexpected word"));
            return NULL;
        }
        type = J_KEYWORD;
    } else {
        fail(p, _("unexpected character"));
        return NULL;
    }
    JsonNode* node = g_new0(JsonNode, 1);
    node->type = type;
    node->text = p->text + start;
    node->len = p->pos - start;
    return node;
}

/* --- Printing --- */

typedef struct {
    PreviewDoc* doc;
    guint lines;
    gboolean stopped;
} Printer;

static gboolean is_scalar(const JsonNode* node) {
    return node->children == NULL;
}

static void newline(Printer* pr, guint indent) {
    pr->lines++;
    if (pr->lines >= JSON_VIEW_MAX_LINES) {
        pr->stopped = TRUE;
    }
    preview_doc_append_len(pr->doc, "\n", 1);
    for (guint i = 0; i < indent; i++) {
        preview_doc_append_len(pr->doc, "  ", 2);
    }
}

static void print_scalar(Printer* pr, const JsonNode* node) {
    PreviewStyle style = node->type == J_STRING ? PREVIEW_STYLE_STRING
                         : node->type == J_NUMBER ? PREVIEW_STYLE_NUMBER
                         : PREVIEW_STYLE_KEYWORD;
    gint start = preview_doc_offset(pr->doc);
    preview_doc_append_len(pr->doc, node->text, (gssize)node->len);
    preview_doc_add_span(pr->doc, start, preview_doc_offset(pr->doc), style);
}

/* Arrays of a few short scalars stay on one line */
static gboolean fits_inline(const JsonNode* node) {
    if (node->type != J_ARRAY || node->children->len == 0) {
        return FALSE;
    }
    gsize width = 0;
    for (guint i = 0; i < node->children->len; i++) {
        const JsonNode* child = g_ptr_array_index(node->children, i);
        if (!is_scalar(child)) {
            return FALSE;
        }
        width += child->len + 2;
    }
    return width <= 72;
}

static void print_node(Printer* pr, const JsonNode* node, guint indent) {
    if (pr->stopped) {
        return;
    }
    if (node->key != NULL) {
        gint start = preview_doc_offset(pr->doc);
        preview_doc_append_len(pr->doc, node->key, (gssize)node->key_len);
        preview_doc_add_span(pr->doc, start, preview_doc_offset(pr->doc), PREVIEW_STYLE_KEY);
        preview_doc_append_len(pr->doc, ": ", 2);
    }
    if (is_scalar(node)) {
        print_scalar(pr, node);
        return;
    }
    gboolean object = node->type == J_OBJECT;
    preview_doc_append_len(pr->doc, object ? "{" : "[", 1);
    if (node->children->len == 0) {
        preview_doc_append_len(pr->doc, object ? "}" : "]", 1);
        return;
    }
    if (fits_inline(node)) {
        for (guint i = 0; i < node->children->len; i++) {
            if (i > 0) {
                preview_doc_append_len(pr->doc, ", ", 2);
            }
            print_scalar(pr, g_ptr_array_index(node->children, i));
        }
        preview_doc_append_len(pr->doc, "]", 1);
        return;
    }
    for (guint i = 0; i < node->children->len && !pr->stopped; i++) {
        newline(pr, indent + 1);
        print_node(pr, g_ptr_array_index(node->children, i), indent + 1);
        if (i + 1 < node->children->len) {
            preview_doc_append_len(pr->doc, ",", 1);
        }
    }
    if (!pr->stopped) {
        newline(pr, indent);
        preview_doc_append_len(pr->doc, object ? "}" : "]", 1);
    }
}

gboolean json_view_append(PreviewDoc* doc, const gchar* text, gsize len, JsonViewInfo* info,
                          GError** error) {
    Parser p = { text, len, 0, error };
    GPtrArray* values = g_ptr_array_new_with_free_func(node_free);
    gboolean ok = TRUE;
    for (;;) {
        skip_space(&p, FALSE);
        if (p.pos >= p.len) {
            break;
        }
        JsonNode* node = parse_value(&p, 0);
        if (node == NULL) {
            ok = FALSE;
            break;
        }
        g_ptr_array_add(values, node);
        /* JSON Lines: the next value starts on another line */
        skip_space(&p, TRUE);
        if (p.pos < p.len && p.text[p.pos] != '\n') {
            ok = fail(&p, _("unexpected text after the value"));
            break;
        }
    }
    if (ok && values->len == 0) {
        ok = fail(&p, _("empty document"));
    }
    if (!ok) {
        g_ptr_array_free(values, TRUE);
        return FALSE;
    }

    JsonViewInfo result = { 0 };
    result.n_values = values->len;
    result.lines = values->len > 1;
    const JsonNode* first = g_ptr_array_index(values, 0);
    result.root = first->type == J_OBJECT ? '{' : first->type == J_ARRAY ? '[' : 0;
    result.n_root_items = first->children != NULL ? first->children->len : 0;
    if (info != NULL) {
        *info = result;
    }

    Printer pr = { doc, 0, FALSE };
    for (guint i = 0; i < values->len && !pr.stopped; i++) {
        if (i > 0) {
            preview_doc_append_len(doc, "\n", 1);
            newline(&pr, 0);
        }
        print_node(&pr, g_ptr_array_index(values, i), 0);
    }
    preview_doc_append_len(doc, "\n", 1);
    if (pr.stopped) {
        gchar* note = g_strdup_printf(_("… (stopped after %u lines)"), JSON_VIEW_MAX_LINES);
        preview_doc_note(doc, note);
        g_free(note);
    }
    g_ptr_array_free(values, TRUE);
    return TRUE;
}

/* --- Reading values --- */

JsonValue* json_value_parse(const gchar* text, gsize len, GError** error) {
    Parser p = { text, len, 0, error };
    JsonNode* node = parse_value(&p, 0);
    if (node == NULL) {
        return NULL;
    }
    skip_space(&p, FALSE);
    if (p.pos < p.len) {
        fail(&p, _("unexpected text after the value"));
        node_free(node);
        return NULL;
    }
    return node;
}

void json_value_free(JsonValue* value) {
    if (value != NULL) {
        node_free(value);
    }
}

JsonValueType json_value_get_type(const JsonValue* value) {
    switch (value->type) {
    case J_OBJECT: return JSON_VALUE_OBJECT;
    case J_ARRAY: return JSON_VALUE_ARRAY;
    case J_STRING: return JSON_VALUE_STRING;
    case J_NUMBER: return JSON_VALUE_NUMBER;
    default: return JSON_VALUE_KEYWORD;
    }
}

/* Unescaped content of a quoted string token */
static gchar* unescape_string(const gchar* text, gsize len) {
    GString* out = g_string_sized_new(len);
    gsize end = len > 0 ? len - 1 : 0; /* closing quote */
    for (gsize i = 1; i < end; i++) {
        gchar c = text[i];
        if (c != '\\' || i + 1 >= end) {
            g_string_append_c(out, c);
            continue;
        }
        c = text[++i];
        switch (c) {
        case 'n': g_string_append_c(out, '\n'); break;
        case 't': g_string_append_c(out, '\t'); break;
        case 'r': g_string_append_c(out, '\r'); break;
        case 'b': g_string_append_c(out, '\b'); break;
        case 'f': g_string_append_c(out, '\f'); break;
        case 'u': {
            if (i + 4 >= end + 1 || i + 4 > len) {
                break;
            }
            gchar hex[5] = { text[i + 1], text[i + 2], text[i + 3], text[i + 4], 0 };
            gunichar cp = (gunichar)g_ascii_strtoull(hex, NULL, 16);
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < len && text[i + 1] == '\\' && text[i + 2] == 'u') {
                gchar lo_hex[5] = { text[i + 3], text[i + 4], text[i + 5], text[i + 6], 0 };
                gunichar lo = (gunichar)g_ascii_strtoull(lo_hex, NULL, 16);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
            }
            if (g_unichar_validate(cp) && cp != 0) {
                g_string_append_unichar(out, cp);
            }
            break;
        }
        default: g_string_append_c(out, c); break;
        }
    }
    return g_string_free(out, FALSE);
}

static gboolean key_equals(const JsonNode* node, const gchar* key) {
    gsize klen = strlen(key);
    if (node->key == NULL) {
        return FALSE;
    }
    if (node->key[0] == '"' || node->key[0] == '\'') {
        return node->key_len == klen + 2 && memcmp(node->key + 1, key, klen) == 0;
    }
    return node->key_len == klen && memcmp(node->key, key, klen) == 0;
}

const JsonValue* json_value_get(const JsonValue* value, const gchar* key) {
    if (value == NULL || value->type != J_OBJECT) {
        return NULL;
    }
    for (guint i = 0; i < value->children->len; i++) {
        const JsonNode* child = g_ptr_array_index(value->children, i);
        if (key_equals(child, key)) {
            return child;
        }
    }
    return NULL;
}

guint json_value_get_length(const JsonValue* value) {
    return (value != NULL && value->children != NULL) ? value->children->len : 0;
}

const JsonValue* json_value_get_item(const JsonValue* value, guint index) {
    if (value == NULL || value->children == NULL || index >= value->children->len) {
        return NULL;
    }
    return g_ptr_array_index(value->children, index);
}

gchar* json_value_dup_key(const JsonValue* member) {
    if (member == NULL || member->key == NULL) {
        return NULL;
    }
    if (member->key[0] == '"' || member->key[0] == '\'') {
        return unescape_string(member->key, member->key_len);
    }
    return g_strndup(member->key, member->key_len);
}

gchar* json_value_dup_string(const JsonValue* value) {
    if (value == NULL || value->children != NULL) {
        return NULL;
    }
    if (value->type == J_STRING) {
        return unescape_string(value->text, value->len);
    }
    return g_strndup(value->text, value->len);
}

gchar* json_value_dup_text(const JsonValue* value) {
    if (value == NULL) {
        return NULL;
    }
    if (value->type == J_STRING) {
        return json_value_dup_string(value);
    }
    if (value->type != J_ARRAY) {
        return NULL;
    }
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < value->children->len; i++) {
        gchar* part = json_value_dup_string(g_ptr_array_index(value->children, i));
        if (part != NULL) {
            g_string_append(out, part);
            g_free(part);
        }
    }
    return g_string_free(out, FALSE);
}

gdouble json_value_get_number(const JsonValue* value) {
    if (value == NULL || value->type != J_NUMBER) {
        return 0;
    }
    gchar* s = g_strndup(value->text, value->len);
    gdouble v = g_ascii_strtod(s, NULL);
    g_free(s);
    return v;
}
