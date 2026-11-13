/* Jupyter notebooks held for editing (see ipynb_doc.h). */
#include "jupyter/ipynb_doc.h"
#include "core/i18n.h"

#include <string.h>

#include "formats/json_view.h"
#include "formats/notebook.h"

/* The notebook as an owned JSON tree: members keep their order, numbers and
 * keywords their text as written */
typedef enum { N_OBJECT, N_ARRAY, N_STRING, N_RAW } NodeType;

typedef struct Node Node;
struct Node {
    NodeType type;
    gchar* key;          /* member name, or NULL for an array item */
    gchar* text;         /* N_STRING: unescaped value; N_RAW: number or keyword */
    GPtrArray* children; /* N_OBJECT and N_ARRAY: Node* (owned) */
    guint cell_key;      /* a cell: its key (ipynb_doc_cell_key()) */
};

struct IpynbDoc {
    Node* root;
    guint next_key;     /* key of the next cell read or added */
    gchar* language;
    gchar* extension;
    gchar* kernel;
};

static void node_free(gpointer data) {
    Node* n = data;
    if (n == NULL) return;
    g_free(n->key);
    g_free(n->text);
    if (n->children != NULL) g_ptr_array_unref(n->children);
    g_free(n);
}

static Node* node_new(NodeType type, const gchar* key, const gchar* text) {
    Node* n = g_new0(Node, 1);
    n->type = type;
    n->key = g_strdup(key);
    n->text = g_strdup(text);
    if (type == N_OBJECT || type == N_ARRAY) n->children = g_ptr_array_new_with_free_func(node_free);
    return n;
}

static Node* node_from_json(const JsonValue* v, gchar* key /* owned */) {
    Node* n = g_new0(Node, 1);
    n->key = key;
    switch (json_value_get_type(v)) {
    case JSON_VALUE_OBJECT:
    case JSON_VALUE_ARRAY: {
        gboolean object = json_value_get_type(v) == JSON_VALUE_OBJECT;
        n->type = object ? N_OBJECT : N_ARRAY;
        n->children = g_ptr_array_new_with_free_func(node_free);
        for (guint i = 0; i < json_value_get_length(v); i++) {
            const JsonValue* item = json_value_get_item(v, i);
            g_ptr_array_add(n->children, node_from_json(item, object ? json_value_dup_key(item) : NULL));
        }
        break;
    }
    case JSON_VALUE_STRING:
        n->type = N_STRING;
        n->text = json_value_dup_string(v);
        break;
    default:
        n->type = N_RAW;
        n->text = json_value_dup_string(v);
        break;
    }
    return n;
}

static Node* node_get(const Node* obj, const gchar* key) {
    if (obj == NULL || obj->type != N_OBJECT) return NULL;
    for (guint i = 0; i < obj->children->len; i++) {
        Node* c = g_ptr_array_index(obj->children, i);
        if (g_strcmp0(c->key, key) == 0) return c;
    }
    return NULL;
}

static const gchar* node_get_string(const Node* obj, const gchar* key) {
    Node* n = node_get(obj, key);
    return (n != NULL && n->type == N_STRING) ? n->text : NULL;
}

/* Set member key of obj to value (owned, its key set here): in place of the
 * member of that name, else before the first member sorted after it (Jupyter
 * sorts the keys) */
static void node_set(Node* obj, const gchar* key, Node* value) {
    g_free(value->key);
    value->key = g_strdup(key);
    guint at = obj->children->len;
    for (guint i = 0; i < obj->children->len; i++) {
        Node* c = g_ptr_array_index(obj->children, i);
        if (g_strcmp0(c->key, key) == 0) {
            node_free(g_ptr_array_index(obj->children, i));
            obj->children->pdata[i] = value;
            return;
        }
        if (at == obj->children->len && g_strcmp0(c->key, key) > 0) at = i;
    }
    g_ptr_array_insert(obj->children, (gint)at, value);
}

static void node_remove(Node* obj, const gchar* key) {
    for (guint i = 0; obj != NULL && i < obj->children->len; i++) {
        Node* c = g_ptr_array_index(obj->children, i);
        if (g_strcmp0(c->key, key) == 0) {
            g_ptr_array_remove_index(obj->children, i);
            return;
        }
    }
}

/* A string, or the concatenation of an array of strings (multiline text) */
static gchar* node_text(const Node* n) {
    if (n == NULL) return NULL;
    if (n->type == N_STRING) return g_strdup(n->text);
    if (n->type != N_ARRAY) return NULL;
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < n->children->len; i++) {
        Node* c = g_ptr_array_index(n->children, i);
        if (c->type == N_STRING) g_string_append(out, c->text);
    }
    return g_string_free(out, FALSE);
}

/* Multiline text as Jupyter stores it: an array of lines, each with its
 * line break */
static Node* node_lines(const gchar* text) {
    Node* arr = node_new(N_ARRAY, NULL, NULL);
    const gchar* p = text != NULL ? text : "";
    while (*p != '\0') {
        const gchar* nl = strchr(p, '\n');
        gsize n = nl != NULL ? (gsize)(nl - p) + 1 : strlen(p);
        Node* line = node_new(N_STRING, NULL, NULL);
        line->text = g_strndup(p, n);
        g_ptr_array_add(arr->children, line);
        p += n;
    }
    return arr;
}

/* --- Writing, as Python's json.dumps(indent=1, ensure_ascii=False) --- */

static void write_string(GString* out, const gchar* s) {
    g_string_append_c(out, '"');
    for (const guchar* p = (const guchar*)s; *p != '\0'; p++) {
        switch (*p) {
        case '"': g_string_append(out, "\\\""); break;
        case '\\': g_string_append(out, "\\\\"); break;
        case '\n': g_string_append(out, "\\n"); break;
        case '\r': g_string_append(out, "\\r"); break;
        case '\t': g_string_append(out, "\\t"); break;
        case '\b': g_string_append(out, "\\b"); break;
        case '\f': g_string_append(out, "\\f"); break;
        default:
            if (*p < 0x20) {
                g_string_append_printf(out, "\\u%04x", *p);
            } else {
                g_string_append_c(out, (gchar)*p);
            }
        }
    }
    g_string_append_c(out, '"');
}

static void write_node(GString* out, const Node* n, guint level) {
    if (n->type == N_STRING) {
        write_string(out, n->text);
        return;
    }
    if (n->type == N_RAW) {
        g_string_append(out, n->text);
        return;
    }
    gboolean object = n->type == N_OBJECT;
    if (n->children->len == 0) {
        g_string_append(out, object ? "{}" : "[]");
        return;
    }
    g_string_append_c(out, object ? '{' : '[');
    for (guint i = 0; i < n->children->len; i++) {
        const Node* c = g_ptr_array_index(n->children, i);
        g_string_append(out, i == 0 ? "\n" : ",\n");
        for (guint s = 0; s <= level; s++) g_string_append_c(out, ' ');
        if (object) {
            write_string(out, c->key != NULL ? c->key : "");
            g_string_append(out, ": ");
        }
        write_node(out, c, level + 1);
    }
    g_string_append_c(out, '\n');
    for (guint s = 0; s < level; s++) g_string_append_c(out, ' ');
    g_string_append_c(out, object ? '}' : ']');
}

gchar* ipynb_doc_to_json(const IpynbDoc* doc) {
    GString* out = g_string_new(NULL);
    write_node(out, doc->root, 0);
    g_string_append_c(out, '\n');
    return g_string_free(out, FALSE);
}

/* --- The notebook --- */

static void read_kernel(IpynbDoc* doc) {
    Node* meta = node_get(doc->root, "metadata");
    Node* info = node_get(meta, "language_info");
    Node* spec = node_get(meta, "kernelspec");
    const gchar* lang = node_get_string(info, "name");
    if (lang == NULL) lang = node_get_string(spec, "language");
    doc->language = g_strdup(lang);
    doc->extension = g_strdup(node_get_string(info, "file_extension"));
    doc->kernel = g_strdup(node_get_string(spec, "display_name"));
}

static Node* cells_of(const IpynbDoc* doc) {
    return node_get(doc->root, "cells");
}

static Node* cell_at(const IpynbDoc* doc, guint index) {
    Node* cells = cells_of(doc);
    return (cells != NULL && index < cells->children->len) ? g_ptr_array_index(cells->children, index) : NULL;
}

IpynbDoc* ipynb_doc_parse(const gchar* text, gsize len, GError** error) {
    g_autoptr(JsonValue) json = json_value_parse(text, len, error);
    if (json == NULL) return NULL;
    const JsonValue* format = json_value_get(json, "nbformat");
    const JsonValue* cells = json_value_get(json, "cells");
    if (json_value_get_type(json) != JSON_VALUE_OBJECT || cells == NULL ||
        json_value_get_type(cells) != JSON_VALUE_ARRAY || json_value_get_number(format) != 4) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "%s", _("Not a Jupyter notebook (nbformat 4)"));
        return NULL;
    }
    IpynbDoc* doc = g_new0(IpynbDoc, 1);
    doc->root = node_from_json(json, NULL);
    /* Cells that are not objects cannot be edited: dropped */
    Node* list = cells_of(doc);
    for (guint i = list->children->len; i-- > 0;) {
        if (((Node*)g_ptr_array_index(list->children, i))->type != N_OBJECT) g_ptr_array_remove_index(list->children, i);
    }
    for (guint i = 0; i < list->children->len; i++) {
        ((Node*)g_ptr_array_index(list->children, i))->cell_key = ++doc->next_key;
    }
    read_kernel(doc);
    return doc;
}

IpynbDoc* ipynb_doc_new(void) {
    IpynbDoc* doc = g_new0(IpynbDoc, 1);
    doc->root = node_new(N_OBJECT, NULL, NULL);
    node_set(doc->root, "cells", node_new(N_ARRAY, NULL, NULL));
    node_set(doc->root, "metadata", node_new(N_OBJECT, NULL, NULL));
    node_set(doc->root, "nbformat", node_new(N_RAW, NULL, "4"));
    node_set(doc->root, "nbformat_minor", node_new(N_RAW, NULL, "5"));
    ipynb_doc_insert_cell(doc, 0, IPYNB_CELL_CODE);
    return doc;
}

void ipynb_doc_free(IpynbDoc* doc) {
    if (doc == NULL) return;
    node_free(doc->root);
    g_free(doc->language);
    g_free(doc->extension);
    g_free(doc->kernel);
    g_free(doc);
}

const gchar* ipynb_doc_language(const IpynbDoc* doc) {
    return doc->language;
}

const gchar* ipynb_doc_file_extension(const IpynbDoc* doc) {
    return doc->extension;
}

const gchar* ipynb_doc_kernel(const IpynbDoc* doc) {
    return doc->kernel;
}

guint ipynb_doc_n_cells(const IpynbDoc* doc) {
    Node* cells = cells_of(doc);
    return cells != NULL ? cells->children->len : 0;
}

static const gchar* type_name(IpynbCellType type) {
    switch (type) {
    case IPYNB_CELL_CODE: return "code";
    case IPYNB_CELL_MARKDOWN: return "markdown";
    default: return "raw";
    }
}

IpynbCellType ipynb_doc_cell_type(const IpynbDoc* doc, guint index) {
    const gchar* t = node_get_string(cell_at(doc, index), "cell_type");
    if (g_strcmp0(t, "code") == 0) return IPYNB_CELL_CODE;
    if (g_strcmp0(t, "markdown") == 0) return IPYNB_CELL_MARKDOWN;
    return IPYNB_CELL_RAW;
}

gchar* ipynb_doc_cell_source(const IpynbDoc* doc, guint index) {
    gchar* s = node_text(node_get(cell_at(doc, index), "source"));
    return s != NULL ? s : g_strdup("");
}

/* The data: URI of attachment name of a cell's attachments (data) */
static gchar* node_attachment(const gchar* name, gpointer data) {
    Node* bundle = node_get((const Node*)data, name);
    for (guint i = 0; bundle != NULL && bundle->type == N_OBJECT && i < bundle->children->len; i++) {
        Node* c = g_ptr_array_index(bundle->children, i);
        if (c->key == NULL || !g_str_has_prefix(c->key, "image/")) continue;
        gchar* b64 = node_text(c);
        gchar* uri = b64 != NULL ? notebook_data_uri(c->key, b64) : NULL;
        g_free(b64);
        if (uri != NULL) return uri;
    }
    return NULL;
}

gchar* ipynb_doc_cell_markdown(const IpynbDoc* doc, guint index) {
    gchar* source = ipynb_doc_cell_source(doc, index);
    Node* attachments = node_get(cell_at(doc, index), "attachments");
    if (attachments == NULL) return source;
    gchar* shown = notebook_inline_attachments(source, node_attachment, attachments);
    g_free(source);
    return shown;
}

void ipynb_doc_set_source(IpynbDoc* doc, guint index, const gchar* source) {
    Node* cell = cell_at(doc, index);
    if (cell != NULL) node_set(cell, "source", node_lines(source));
}

/* Give a code cell the members Jupyter expects of it */
static void ensure_code_members(Node* cell) {
    if (node_get(cell, "execution_count") == NULL) node_set(cell, "execution_count", node_new(N_RAW, NULL, "null"));
    Node* outputs = node_get(cell, "outputs");
    if (outputs == NULL || outputs->type != N_ARRAY) node_set(cell, "outputs", node_new(N_ARRAY, NULL, NULL));
}

void ipynb_doc_set_type(IpynbDoc* doc, guint index, IpynbCellType type) {
    Node* cell = cell_at(doc, index);
    if (cell == NULL || ipynb_doc_cell_type(doc, index) == type) return;
    node_set(cell, "cell_type", node_new(N_STRING, NULL, type_name(type)));
    if (type == IPYNB_CELL_CODE) {
        ensure_code_members(cell);
    } else {
        node_remove(cell, "execution_count");
        node_remove(cell, "outputs");
    }
}

gint ipynb_doc_cell_execution_count(const IpynbDoc* doc, guint index) {
    Node* n = node_get(cell_at(doc, index), "execution_count");
    if (n == NULL || n->type != N_RAW || !g_ascii_isdigit(n->text[0])) return -1;
    return (gint)g_ascii_strtoll(n->text, NULL, 10);
}

/* Text cut to max_lines lines (0: all), with a note on what was left out */
static gchar* cut_lines(const gchar* text, guint max_lines) {
    gchar* clean = notebook_strip_ansi(text);
    if (max_lines == 0) return clean;
    guint lines = 0;
    const gchar* p = clean;
    while (*p != '\0' && lines < max_lines) {
        const gchar* nl = strchr(p, '\n');
        p = nl != NULL ? nl + 1 : p + strlen(p);
        lines++;
    }
    if (*p == '\0') return clean;
    guint more = 1;
    for (const gchar* q = p; *q != '\0'; q++) more += (*q == '\n' && q[1] != '\0');
    GString* out = g_string_new_len(clean, p - clean);
    g_string_append_printf(out, ngettext("… (%u more line)", "… (%u more lines)", more), more);
    g_free(clean);
    return g_string_free(out, FALSE);
}

/* Without the last line break (Jupyter's text ends with one) */
static void chomp(gchar* text) {
    gsize n = strlen(text);
    if (n > 0 && text[n - 1] == '\n') text[n - 1] = '\0';
}

static IpynbOutput* output_text(IpynbOutputKind kind, const gchar* text, guint max_lines) {
    IpynbOutput* o = g_new0(IpynbOutput, 1);
    o->kind = kind;
    o->text = cut_lines(text, max_lines);
    chomp(o->text);
    return o;
}

/* An image of a rich output's data, or NULL */
static IpynbOutput* output_image(const Node* data) {
    static const gchar* const IMAGES[] = { "image/png", "image/jpeg", "image/gif" };
    for (gsize i = 0; i < G_N_ELEMENTS(IMAGES); i++) {
        gchar* b64 = node_text(node_get(data, IMAGES[i]));
        if (b64 == NULL) continue;
        gsize len = 0;
        guchar* bytes = g_base64_decode(b64, &len); /* skips Jupyter's line breaks */
        g_free(b64);
        if (len == 0) {
            g_free(bytes);
            continue;
        }
        IpynbOutput* o = g_new0(IpynbOutput, 1);
        o->kind = IPYNB_OUTPUT_IMAGE;
        o->image = g_bytes_new_take(bytes, len);
        o->mime = g_strdup(IMAGES[i]);
        return o;
    }
    gchar* svg = node_text(node_get(data, "image/svg+xml"));
    if (svg == NULL) return NULL;
    IpynbOutput* o = g_new0(IpynbOutput, 1);
    o->kind = IPYNB_OUTPUT_IMAGE;
    gsize len = strlen(svg);
    o->image = g_bytes_new_take(svg, len);
    o->mime = g_strdup("image/svg+xml");
    return o;
}

/* The formula of a text/latex output: math in $...$, $$...$$, \[...\],
 * \(...\) or an equation environment (SymPy, IPython.display.Math), else
 * NULL (a LaTeX table or document is not a formula) */
static gchar* latex_formula(const gchar* latex) {
    static const struct {
        const gchar* open;
        const gchar* close;
        gboolean keep; /* environments stay in the TeX */
    } DELIMS[] = {
        {"$$", "$$", FALSE}, {"\\[", "\\]", FALSE}, {"\\(", "\\)", FALSE}, {"$", "$", FALSE},
        {"\\begin{equation", "}", TRUE}, {"\\begin{align", "}", TRUE}, {"\\begin{gather", "}", TRUE},
    };
    gchar* text = g_strstrip(g_strdup(latex));
    gsize len = strlen(text);
    for (gsize i = 0; i < G_N_ELEMENTS(DELIMS); i++) {
        gsize open = strlen(DELIMS[i].open), close = strlen(DELIMS[i].close);
        if (len < open + close || !g_str_has_prefix(text, DELIMS[i].open) || !g_str_has_suffix(text, DELIMS[i].close)) {
            continue;
        }
        gchar* tex = DELIMS[i].keep ? g_strdup(text) : g_strndup(text + open, len - open - close);
        g_free(text);
        return tex;
    }
    g_free(text);
    return NULL;
}

static void output_free(gpointer data) {
    IpynbOutput* o = data;
    g_free(o->text);
    if (o->image != NULL) g_bytes_unref(o->image);
    g_free(o->mime);
    g_free(o);
}

GPtrArray* ipynb_doc_cell_outputs(const IpynbDoc* doc, guint index, guint max_lines) {
    GPtrArray* list = g_ptr_array_new_with_free_func(output_free);
    Node* outputs = node_get(cell_at(doc, index), "outputs");
    for (guint i = 0; outputs != NULL && outputs->type == N_ARRAY && i < outputs->children->len; i++) {
        Node* out = g_ptr_array_index(outputs->children, i);
        const gchar* type = node_get_string(out, "output_type");
        IpynbOutput* o = NULL;
        if (g_strcmp0(type, "stream") == 0) {
            gchar* text = node_text(node_get(out, "text"));
            gboolean err = g_strcmp0(node_get_string(out, "name"), "stderr") == 0;
            if (text != NULL) o = output_text(err ? IPYNB_OUTPUT_ERROR : IPYNB_OUTPUT_TEXT, text, max_lines);
            g_free(text);
        } else if (g_strcmp0(type, "error") == 0) {
            GString* text = g_string_new(NULL);
            Node* tb = node_get(out, "traceback");
            for (guint t = 0; tb != NULL && tb->type == N_ARRAY && t < tb->children->len; t++) {
                Node* line = g_ptr_array_index(tb->children, t);
                if (line->type != N_STRING) continue;
                if (text->len > 0) g_string_append_c(text, '\n');
                g_string_append(text, line->text);
            }
            if (text->len == 0) {
                g_string_printf(text, "%s: %s", node_get_string(out, "ename") != NULL ? node_get_string(out, "ename") : "Error",
                                node_get_string(out, "evalue") != NULL ? node_get_string(out, "evalue") : "");
            }
            o = output_text(IPYNB_OUTPUT_ERROR, text->str, max_lines);
            g_string_free(text, TRUE);
        } else if (g_strcmp0(type, "execute_result") == 0 || g_strcmp0(type, "display_data") == 0) {
            Node* data = node_get(out, "data");
            o = output_image(data);
            if (o == NULL) {
                gchar* latex = node_text(node_get(data, "text/latex"));
                gchar* tex = latex != NULL ? latex_formula(latex) : NULL;
                if (tex != NULL) {
                    o = g_new0(IpynbOutput, 1);
                    o->kind = IPYNB_OUTPUT_MATH;
                    o->text = tex;
                }
                g_free(latex);
            }
            gchar* plain = o == NULL ? node_text(node_get(data, "text/plain")) : NULL;
            if (plain == NULL && o == NULL) plain = node_text(node_get(data, "text/markdown"));
            if (plain != NULL) {
                o = output_text(IPYNB_OUTPUT_TEXT, plain, max_lines);
            } else if (o == NULL) {
                o = output_text(IPYNB_OUTPUT_TEXT,
                                node_get(data, "text/html") != NULL ? _("(HTML output, not shown)")
                                                                     : _("(output not shown)"),
                                0);
            }
            g_free(plain);
            o->result = g_strcmp0(type, "execute_result") == 0;
        }
        if (o != NULL) g_ptr_array_add(list, o);
    }
    return list;
}

static gboolean clear_cell(Node* cell) {
    if (g_strcmp0(node_get_string(cell, "cell_type"), "code") != 0) return FALSE;
    Node* outputs = node_get(cell, "outputs");
    Node* count = node_get(cell, "execution_count");
    gboolean had = (outputs != NULL && outputs->children != NULL && outputs->children->len > 0) ||
                   (count != NULL && g_strcmp0(count->text, "null") != 0);
    if (had) {
        node_set(cell, "outputs", node_new(N_ARRAY, NULL, NULL));
        node_set(cell, "execution_count", node_new(N_RAW, NULL, "null"));
    }
    return had;
}

gboolean ipynb_doc_clear_outputs(IpynbDoc* doc, guint index) {
    if (index != G_MAXUINT) {
        Node* cell = cell_at(doc, index);
        return cell != NULL && clear_cell(cell);
    }
    gboolean any = FALSE;
    for (guint i = 0; i < ipynb_doc_n_cells(doc); i++) {
        any = clear_cell(cell_at(doc, i)) || any;
    }
    return any;
}

/* A cell id as Jupyter makes them (nbformat 4.5): 8 hexadecimal digits,
 * not used by another cell */
static gchar* new_cell_id(const IpynbDoc* doc) {
    for (;;) {
        gchar* id = g_strdup_printf("%08x", g_random_int());
        gboolean used = FALSE;
        for (guint i = 0; i < ipynb_doc_n_cells(doc) && !used; i++) {
            used = g_strcmp0(node_get_string(cell_at(doc, i), "id"), id) == 0;
        }
        if (!used) return id;
        g_free(id);
    }
}

guint ipynb_doc_insert_cell(IpynbDoc* doc, guint index, IpynbCellType type) {
    Node* cells = cells_of(doc);
    index = MIN(index, cells->children->len);
    Node* cell = node_new(N_OBJECT, NULL, NULL);
    node_set(cell, "cell_type", node_new(N_STRING, NULL, type_name(type)));
    Node* minor = node_get(doc->root, "nbformat_minor");
    if (minor != NULL && minor->type == N_RAW && g_ascii_strtoll(minor->text, NULL, 10) >= 5) {
        gchar* id = new_cell_id(doc);
        node_set(cell, "id", node_new(N_STRING, NULL, id));
        g_free(id);
    }
    node_set(cell, "metadata", node_new(N_OBJECT, NULL, NULL));
    if (type == IPYNB_CELL_CODE) ensure_code_members(cell);
    node_set(cell, "source", node_new(N_ARRAY, NULL, NULL));
    cell->cell_key = ++doc->next_key;
    g_ptr_array_insert(cells->children, (gint)index, cell);
    return index;
}

void ipynb_doc_remove_cell(IpynbDoc* doc, guint index) {
    Node* cells = cells_of(doc);
    if (index < cells->children->len) g_ptr_array_remove_index(cells->children, index);
}

IpynbCell* ipynb_doc_take_cell(IpynbDoc* doc, guint index) {
    Node* cells = cells_of(doc);
    return index < cells->children->len ? (IpynbCell*)g_ptr_array_steal_index(cells->children, index) : NULL;
}

guint ipynb_doc_put_cell(IpynbDoc* doc, guint index, IpynbCell* cell) {
    Node* cells = cells_of(doc);
    index = MIN(index, cells->children->len);
    g_ptr_array_insert(cells->children, (gint)index, cell);
    return index;
}

void ipynb_cell_free(IpynbCell* cell) {
    node_free(cell);
}

/* Add source[from..to) (byte offsets) without its end newlines, if any is left */
static void add_part(GPtrArray* parts, const gchar* source, gsize from, gsize to) {
    while (from < to && (source[from] == '\n' || source[from] == '\r')) from++;
    while (to > from && (source[to - 1] == '\n' || source[to - 1] == '\r')) to--;
    if (to > from) g_ptr_array_add(parts, g_strndup(source + from, to - from));
}

gchar** ipynb_split_source(const gchar* source, glong start, glong end) {
    if (source == NULL) return NULL;
    glong n = g_utf8_strlen(source, -1);
    start = CLAMP(start, 0, n);
    end = CLAMP(end, 0, n);
    if (end < start) {
        glong swap = start;
        start = end;
        end = swap;
    }
    gsize a = (gsize)(g_utf8_offset_to_pointer(source, start) - source);
    gsize b = (gsize)(g_utf8_offset_to_pointer(source, end) - source);
    GPtrArray* parts = g_ptr_array_new();
    add_part(parts, source, 0, a);
    if (b > a) add_part(parts, source, a, b);
    add_part(parts, source, b, strlen(source));
    if (parts->len < 2) {
        g_ptr_array_set_free_func(parts, g_free);
        g_ptr_array_free(parts, TRUE);
        return NULL;
    }
    g_ptr_array_add(parts, NULL);
    return (gchar**)g_ptr_array_free(parts, FALSE);
}

guint ipynb_doc_split_cell(IpynbDoc* doc, guint index, gchar** parts) {
    guint n = parts != NULL ? g_strv_length(parts) : 0;
    if (index >= ipynb_doc_n_cells(doc) || n < 2) return index;
    IpynbCellType type = ipynb_doc_cell_type(doc, index);
    for (guint i = 0; i + 1 < n; i++) {
        ipynb_doc_set_source(doc, ipynb_doc_insert_cell(doc, index + i, type), parts[i]);
    }
    index += n - 1;
    ipynb_doc_set_source(doc, index, parts[n - 1]);
    return index;
}

void ipynb_doc_move_cell(IpynbDoc* doc, guint from, guint to) {
    Node* cells = cells_of(doc);
    guint n = cells->children->len;
    if (n == 0) return;
    from = MIN(from, n - 1);
    to = MIN(to, n - 1);
    if (from == to) return;
    gpointer cell = g_ptr_array_steal_index(cells->children, from);
    g_ptr_array_insert(cells->children, (gint)to, cell);
}

guint ipynb_doc_cell_key(const IpynbDoc* doc, guint index) {
    Node* cell = cell_at(doc, index);
    return cell != NULL ? cell->cell_key : 0;
}

gint ipynb_doc_find_cell(const IpynbDoc* doc, guint key) {
    for (guint i = 0; key != 0 && i < ipynb_doc_n_cells(doc); i++) {
        if (cell_at(doc, i)->cell_key == key) return (gint)i;
    }
    return -1;
}

void ipynb_doc_set_execution_count(IpynbDoc* doc, guint index, gint count) {
    Node* cell = cell_at(doc, index);
    if (cell == NULL || ipynb_doc_cell_type(doc, index) != IPYNB_CELL_CODE) return;
    gchar* text = count >= 0 ? g_strdup_printf("%d", count) : g_strdup("null");
    node_set(cell, "execution_count", node_new(N_RAW, NULL, text));
    g_free(text);
}

/* The JSON object in text as a tree, or NULL with error set */
static Node* node_parse_object(const gchar* text, gsize len, GError** error) {
    g_autoptr(JsonValue) json = json_value_parse(text, len, error);
    if (json == NULL) return NULL;
    if (json_value_get_type(json) != JSON_VALUE_OBJECT) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "not a JSON object");
        return NULL;
    }
    return node_from_json(json, NULL);
}

/* A string member as lines, as nbformat stores multiline text */
static void split_member(Node* obj, const gchar* key) {
    Node* n = node_get(obj, key);
    if (n != NULL && n->type == N_STRING) node_set(obj, key, node_lines(n->text));
}

gboolean ipynb_doc_append_output(IpynbDoc* doc, guint index, const gchar* json, gsize len, GError** error) {
    Node* cell = cell_at(doc, index);
    if (cell == NULL || ipynb_doc_cell_type(doc, index) != IPYNB_CELL_CODE) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "not a code cell");
        return FALSE;
    }
    Node* out = node_parse_object(json, len, error);
    if (out == NULL) return FALSE;
    ensure_code_members(cell);
    Node* outputs = node_get(cell, "outputs");
    const gchar* type = node_get_string(out, "output_type");
    if (g_strcmp0(type, "stream") == 0) {
        /* Jupyter shows a stream written in pieces as one output */
        Node* last = outputs->children->len > 0 ? g_ptr_array_index(outputs->children, outputs->children->len - 1) : NULL;
        if (last != NULL && g_strcmp0(node_get_string(last, "output_type"), "stream") == 0 &&
            g_strcmp0(node_get_string(last, "name"), node_get_string(out, "name")) == 0) {
            gchar* before = node_text(node_get(last, "text"));
            gchar* more = node_text(node_get(out, "text"));
            gchar* text = g_strconcat(before != NULL ? before : "", more != NULL ? more : "", NULL);
            node_set(last, "text", node_lines(text));
            g_free(text);
            g_free(more);
            g_free(before);
            node_free(out);
            return TRUE;
        }
        split_member(out, "text");
    } else if (g_strcmp0(type, "execute_result") == 0 || g_strcmp0(type, "display_data") == 0) {
        Node* data = node_get(out, "data");
        for (guint i = 0; data != NULL && data->type == N_OBJECT && i < data->children->len; i++) {
            Node* c = g_ptr_array_index(data->children, i);
            if (g_str_has_prefix(c->key, "text/") || g_strcmp0(c->key, "application/javascript") == 0 ||
                g_strcmp0(c->key, "image/svg+xml") == 0) {
                split_member(data, c->key);
            }
        }
    }
    g_ptr_array_add(outputs->children, out);
    return TRUE;
}

const gchar* ipynb_doc_kernel_name(const IpynbDoc* doc) {
    return node_get_string(node_get(node_get(doc->root, "metadata"), "kernelspec"), "name");
}

/* Text of member key of obj, "" when missing */
static gchar* member_json(const Node* obj, const gchar* key) {
    GString* out = g_string_new(NULL);
    Node* n = node_get(obj, key);
    if (n != NULL) write_node(out, n, 0);
    return g_string_free(out, FALSE);
}

gboolean ipynb_doc_set_kernel_info(IpynbDoc* doc, const gchar* kernelspec, const gchar* language_info) {
    Node* meta = node_get(doc->root, "metadata");
    if (meta == NULL || meta->type != N_OBJECT) {
        node_set(doc->root, "metadata", node_new(N_OBJECT, NULL, NULL));
        meta = node_get(doc->root, "metadata");
    }
    gboolean changed = FALSE;
    const gchar* keys[] = { "kernelspec", "language_info" };
    const gchar* values[] = { kernelspec, language_info };
    for (gsize i = 0; i < G_N_ELEMENTS(keys); i++) {
        Node* n = values[i] != NULL ? node_parse_object(values[i], strlen(values[i]), NULL) : NULL;
        if (n == NULL) continue;
        gchar* before = member_json(meta, keys[i]);
        node_set(meta, keys[i], n);
        gchar* after = member_json(meta, keys[i]);
        changed = changed || g_strcmp0(before, after) != 0;
        g_free(before);
        g_free(after);
    }
    if (changed) {
        g_clear_pointer(&doc->language, g_free);
        g_clear_pointer(&doc->extension, g_free);
        g_clear_pointer(&doc->kernel, g_free);
        read_kernel(doc);
    }
    return changed;
}
