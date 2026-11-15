/* JSON pretty-printing for the preview (see json_view.h). */
#include "formats/json_view.h"
#include "formats/data_tree.h"
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
    gboolean cut;          /* container that the end of a cut text left open */
};

#define MAX_DEPTH 256

typedef struct {
    const gchar* text;
    gsize len;
    gsize pos;
    GError** error;
    gboolean cut;          /* the text stops before the end of the file */
    gboolean at_cut;       /* parsing reached the end of the cut text */
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

/* At the end of a cut text: what is open stops here, without an error */
static gboolean at_cut(Parser* p) {
    if (p->cut && p->pos >= p->len) {
        p->at_cut = TRUE;
    }
    return p->at_cut;
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
    if (at_cut(p)) {
        return FALSE;
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
        if (at_cut(p)) {
            node->cut = TRUE;
            return node;
        }
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
                    if (p->at_cut) {
                        node->cut = TRUE;
                        return node;
                    }
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
            if (at_cut(p)) {
                node->cut = TRUE;
                return node;
            }
            if (p->pos >= p->len || p->text[p->pos] != ':') {
                fail(p, _("expected :"));
                node_free(node);
                return NULL;
            }
            p->pos++;
        }
        JsonNode* child = parse_value(p, depth + 1);
        if (child == NULL && p->at_cut) {
            node->cut = TRUE;
            return node;
        }
        if (child == NULL) {
            node_free(node);
            return NULL;
        }
        child->key = key;
        child->key_len = key_len;
        g_ptr_array_add(node->children, child);
        if (child->cut) {
            node->cut = TRUE;
            return node;
        }
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
    if (at_cut(p)) {
        return NULL;
    }
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
        if (at_cut(p)) {
            return NULL; /* the word may go on after the cut */
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
    if (at_cut(p)) {
        return NULL; /* a number may go on after the cut */
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
    const PreviewFold* fold;
    gint level;          /* fold level in effect (PREVIEW_FOLD_AUTO resolved) */
    GString* path;       /* id of the value being printed ("$.a[2]") */
    gint line_start;     /* offset of the start of the current line */
    gboolean outline;    /* members of the outer object go to the tagbar */
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
    pr->line_start = preview_doc_offset(pr->doc);
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
    if (node->type != J_ARRAY || node->children->len == 0 || node->cut) {
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

/* A container written on several lines (the ones that fold) */
static gboolean is_block(const JsonNode* node) {
    return !is_scalar(node) && node->children->len > 0 && !fits_inline(node);
}

static gboolean is_identifier(const gchar* name, gsize len) {
    if (len == 0 || !(g_ascii_isalpha(name[0]) || name[0] == '_' || name[0] == '$')) {
        return FALSE;
    }
    for (gsize i = 1; i < len; i++) {
        if (!(g_ascii_isalnum(name[i]) || name[i] == '_' || name[i] == '$')) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Append the path step of a child: ".name", ["odd name"] or [index] */
static void path_push(GString* path, const JsonNode* child, guint index) {
    if (child->key == NULL) {
        g_string_append_printf(path, "[%u]", index);
        return;
    }
    const gchar* key = child->key;
    gsize len = child->key_len;
    gboolean quoted = len >= 2 && (key[0] == '"' || key[0] == '\'');
    if (quoted && is_identifier(key + 1, len - 2)) {
        g_string_append_c(path, '.');
        g_string_append_len(path, key + 1, (gssize)(len - 2));
    } else if (!quoted && is_identifier(key, len)) {
        g_string_append_c(path, '.');
        g_string_append_len(path, key, (gssize)len);
    } else {
        g_string_append_c(path, '[');
        if (!quoted) {
            g_string_append_c(path, '"');
        }
        g_string_append_len(path, key, (gssize)len);
        if (!quoted) {
            g_string_append_c(path, '"');
        }
        g_string_append_c(path, ']');
    }
}

static void print_folded(Printer* pr, const JsonNode* node) {
    gboolean object = node->type == J_OBJECT;
    preview_doc_append_len(pr->doc, object ? "{…}" : "[…]", -1);
    gint start = preview_doc_offset(pr->doc);
    guint n = node->children->len;
    gchar* count = object ? g_strdup_printf(ngettext("  %u key", "  %u keys", n), n)
                          : g_strdup_printf(ngettext("  %u item", "  %u items", n), n);
    preview_doc_append_len(pr->doc, count, -1);
    preview_doc_add_span(pr->doc, start, preview_doc_offset(pr->doc), PREVIEW_STYLE_COMMENT);
    g_free(count);
}

static void print_node(Printer* pr, const JsonNode* node, guint indent) {
    if (pr->stopped) {
        return;
    }
    gint line_start = pr->line_start;
    preview_doc_add_path(pr->doc, line_start, pr->path->str); /* every value starts its line */
    if (node->key != NULL) {
        gint start = preview_doc_offset(pr->doc);
        preview_doc_append_len(pr->doc, node->key, (gssize)node->key_len);
        gint end = preview_doc_offset(pr->doc);
        preview_doc_add_span(pr->doc, start, end, PREVIEW_STYLE_KEY);
        if (pr->outline && indent == 1) {
            gboolean quoted = node->key[0] == '"' || node->key[0] == '\'';
            preview_doc_add_span(pr->doc, quoted ? start + 1 : start, quoted ? end - 1 : end,
                                 PREVIEW_STYLE_OUTLINE1);
        }
        preview_doc_append_len(pr->doc, ": ", 2);
    }
    if (is_scalar(node)) {
        print_scalar(pr, node);
        return;
    }
    gboolean object = node->type == J_OBJECT;
    if (node->children->len == 0) {
        preview_doc_append_len(pr->doc, object ? "{" : "[", 1);
        if (node->cut) {
            preview_doc_append(pr->doc, PREVIEW_STYLE_COMMENT, "…");
        } else {
            preview_doc_append_len(pr->doc, object ? "}" : "]", 1);
        }
        return;
    }
    if (fits_inline(node)) {
        preview_doc_append_len(pr->doc, "[", 1);
        for (guint i = 0; i < node->children->len; i++) {
            if (i > 0) {
                preview_doc_append_len(pr->doc, ", ", 2);
            }
            print_scalar(pr, g_ptr_array_index(node->children, i));
        }
        preview_doc_append_len(pr->doc, "]", 1);
        return;
    }
    if (preview_fold_is_folded(pr->fold, pr->path->str, (gint)indent, pr->level)) {
        print_folded(pr, node);
        preview_doc_add_fold(pr->doc, line_start, preview_doc_offset(pr->doc), pr->path->str, TRUE);
        return;
    }
    preview_doc_append_len(pr->doc, object ? "{" : "[", 1);
    gsize path_len = pr->path->len;
    for (guint i = 0; i < node->children->len && !pr->stopped; i++) {
        const JsonNode* child = g_ptr_array_index(node->children, i);
        newline(pr, indent + 1);
        path_push(pr->path, child, i);
        print_node(pr, child, indent + 1);
        g_string_truncate(pr->path, path_len);
        if (i + 1 < node->children->len) {
            preview_doc_append_len(pr->doc, ",", 1);
        }
    }
    if (!pr->stopped) {
        newline(pr, indent);
        preview_doc_add_path(pr->doc, pr->line_start, pr->path->str);
        if (node->cut) {
            preview_doc_append(pr->doc, PREVIEW_STYLE_COMMENT, "…"); /* the rest was not read */
        } else {
            preview_doc_append_len(pr->doc, object ? "}" : "]", 1);
        }
        preview_doc_add_fold(pr->doc, line_start, preview_doc_offset(pr->doc), pr->path->str, FALSE);
    }
}

/* Lines added by the blocks at each depth when they are open */
static void count_block_lines(const JsonNode* node, guint depth, GArray* per_depth) {
    if (!is_block(node)) {
        return;
    }
    if (per_depth->len <= depth) {
        g_array_set_size(per_depth, depth + 1);
    }
    g_array_index(per_depth, guint64, depth) += node->children->len + 1;
    for (guint i = 0; i < node->children->len; i++) {
        count_block_lines(g_ptr_array_index(node->children, i), depth + 1, per_depth);
    }
}

/* The level in effect: PREVIEW_FOLD_AUTO opens everything that fits in
 * JSON_VIEW_MAX_LINES, so long documents open folded below some depth */
static gint resolve_level(GPtrArray* values, const PreviewFold* fold) {
    if (fold != NULL && fold->level != PREVIEW_FOLD_AUTO) {
        return fold->level;
    }
    GArray* per_depth = g_array_new(FALSE, TRUE, sizeof(guint64));
    for (guint i = 0; i < values->len; i++) {
        count_block_lines(g_ptr_array_index(values, i), 0, per_depth);
    }
    guint64 base = 2 * (guint64)values->len - 1; /* a line per value, blank lines between */
    guint64 total = base;
    for (guint d = 0; d < per_depth->len; d++) {
        total += g_array_index(per_depth, guint64, d);
    }
    gint level = PREVIEW_FOLD_OPEN;
    if (total >= JSON_VIEW_MAX_LINES) {
        level = 1;
        guint64 lines = base;
        for (guint d = 0; d + 1 < per_depth->len; d++) {
            lines += g_array_index(per_depth, guint64, d);
            if (lines >= JSON_VIEW_MAX_LINES) {
                break;
            }
            level = (gint)d + 1;
        }
    }
    g_array_free(per_depth, TRUE);
    return level;
}

gboolean json_view_append(PreviewDoc* doc, const gchar* text, gsize len, JsonViewInfo* info,
                          GError** error) {
    return json_view_append_folded(doc, text, len, FALSE, NULL, info, error);
}

/* The values of a document (several for JSON Lines), NULL with error set
 * when it is not JSON; *at_end: the cut text ended inside a value */
static GPtrArray* parse_document(const gchar* text, gsize len, gboolean cut, gboolean* at_end, GError** error) {
    Parser p = { text, len, 0, error, cut, FALSE };
    GPtrArray* values = g_ptr_array_new_with_free_func(node_free);
    gboolean ok = TRUE;
    for (;;) {
        skip_space(&p, FALSE);
        if (p.pos >= p.len) {
            break;
        }
        JsonNode* node = parse_value(&p, 0);
        if (node == NULL) {
            ok = p.at_cut; /* a value cut before it shows anything */
            break;
        }
        g_ptr_array_add(values, node);
        if (node->cut) {
            break;
        }
        /* JSON Lines: the next value starts on another line */
        skip_space(&p, TRUE);
        if (p.pos < p.len && p.text[p.pos] != '\n') {
            ok = fail(&p, _("unexpected text after the value"));
            break;
        }
    }
    if (ok && values->len == 0) {
        p.at_cut = FALSE;
        ok = fail(&p, _("empty document"));
    }
    if (!ok) {
        g_ptr_array_free(values, TRUE);
        return NULL;
    }
    *at_end = p.at_cut;
    return values;
}

/* What the summary line and the fold level need */
static JsonViewInfo document_info(GPtrArray* values, gboolean at_end) {
    JsonViewInfo result = { 0 };
    result.n_values = values->len;
    result.lines = values->len > 1;
    const JsonNode* first = g_ptr_array_index(values, 0);
    result.root = first->type == J_OBJECT ? '{' : first->type == J_ARRAY ? '[' : 0;
    result.n_root_items = first->children != NULL ? first->children->len : 0;
    result.cut = at_end;
    return result;
}


gboolean json_view_append_folded(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                                 const PreviewFold* fold, JsonViewInfo* info, GError** error) {
    gboolean at_end = FALSE;
    GPtrArray* values = parse_document(text, len, cut, &at_end, error);
    if (values == NULL) {
        return FALSE;
    }
    JsonViewInfo result = document_info(values, at_end);
    result.level = resolve_level(values, fold);
    if (info != NULL) {
        *info = result;
    }

    if ((fold == NULL || fold->level == PREVIEW_FOLD_AUTO) && result.level != PREVIEW_FOLD_OPEN) {
        preview_doc_note(doc, _("Long document: the deeper levels are folded (Tab folds or opens a "
                                "block, Shift+Tab changes the level)"));
    }
    Printer pr = { doc, 0, FALSE, fold, result.level, g_string_new(NULL), preview_doc_offset(doc),
                   !result.lines && result.root == '{' };
    for (guint i = 0; i < values->len && !pr.stopped; i++) {
        if (i > 0) {
            preview_doc_append_len(doc, "\n", 1);
            newline(&pr, 0);
        }
        g_string_assign(pr.path, "$");
        if (result.lines) {
            g_string_append_printf(pr.path, "[%u]", i);
        }
        print_node(&pr, g_ptr_array_index(values, i), 0);
    }
    preview_doc_append_len(doc, "\n", 1);
    if (pr.stopped) {
        gchar* note = g_strdup_printf(_("… (stopped after %u lines)"), JSON_VIEW_MAX_LINES);
        preview_doc_note(doc, note);
        g_free(note);
    }
    g_string_free(pr.path, TRUE);
    g_ptr_array_free(values, TRUE);
    return TRUE;
}

/* The dimmed summary line ("Object · 12 keys"), the note, then body */
static void append_summary(PreviewDoc* doc, const JsonViewInfo* info, const gchar* note, const PreviewDoc* body) {
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* count = preview_doc_count(info->lines ? info->n_values : info->n_root_items);
    if (info->lines) {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, ngettext("JSON Lines · %s record\n", "JSON Lines · %s records\n", info->n_values), count);
    } else if (info->root == '{') {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, ngettext("Object · %s key\n", "Object · %s keys\n", info->n_root_items), count);
    } else if (info->root == '[') {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, ngettext("Array · %s item\n", "Array · %s items\n", info->n_root_items), count);
    }
    g_free(count);
    if (note != NULL) {
        preview_doc_note(doc, note);
    }
    if (doc->text->len > 0) {
        preview_doc_append_len(doc, "\n", 1);
    }
    preview_doc_append_doc(doc, body); /* with its fold points and paths */
}

gboolean json_view_build(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                         const PreviewFold* fold, const gchar* note, GError** error) {
    g_autoptr(PreviewDoc) body = preview_doc_new();
    JsonViewInfo info;
    if (!json_view_append_folded(body, text, len, cut, fold, &info, error)) {
        return FALSE;
    }
    append_summary(doc, &info, note, body);
    return TRUE;
}

/* --- Tree --- */

static PreviewStyle scalar_style(const JsonNode* node) {
    return node->type == J_STRING ? PREVIEW_STYLE_STRING
           : node->type == J_NUMBER ? PREVIEW_STYLE_NUMBER
           : PREVIEW_STYLE_KEYWORD;
}

static void add_children(DataTreeNode* row, const JsonNode* node, GString* path);

/* The row of node under parent, named key, at path */
static void add_row(DataTreeNode* parent, const JsonNode* node, const gchar* key, GString* path) {
    DataTreeKind kind = node->type == J_OBJECT ? DATA_TREE_OBJECT
                        : node->type == J_ARRAY ? DATA_TREE_ARRAY
                        : DATA_TREE_VALUE;
    DataTreeNode* row = data_tree_add(parent, kind, key, path->str);
    if (kind == DATA_TREE_VALUE) {
        data_tree_set_value(row, node->text, (gssize)node->len, scalar_style(node));
        return;
    }
    if (node->cut) {
        data_tree_set_cut(row);
    }
    add_children(row, node, path);
}

/* Members by name, items by index */
static void add_children(DataTreeNode* row, const JsonNode* node, GString* path) {
    gsize path_len = path->len;
    for (guint i = 0; i < node->children->len; i++) {
        const JsonNode* child = g_ptr_array_index(node->children, i);
        gchar* key = child->key != NULL ? json_value_dup_key(child) : g_strdup_printf("%u", i);
        path_push(path, child, i);
        add_row(row, child, key, path);
        g_string_truncate(path, path_len);
        g_free(key);
    }
}

gboolean json_view_build_tree(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                              const PreviewFold* fold, const gchar* note, GError** error) {
    gboolean at_end = FALSE;
    GPtrArray* values = parse_document(text, len, cut, &at_end, error);
    if (values == NULL) {
        return FALSE;
    }
    JsonViewInfo info = document_info(values, at_end);
    g_autoptr(DataTree) tree = data_tree_new();
    DataTreeNode* root = data_tree_root(tree);
    GString* path = g_string_new("$");
    if (info.lines) {
        for (guint i = 0; i < values->len; i++) {
            g_string_printf(path, "$[%u]", i);
            gchar* key = g_strdup_printf("%u", i);
            add_row(root, g_ptr_array_index(values, i), key, path);
            g_free(key);
        }
    } else {
        const JsonNode* value = g_ptr_array_index(values, 0);
        if (value->children != NULL) {
            add_children(root, value, path); /* the members of the document are the first level */
            if (value->cut) {
                data_tree_set_cut(root);
            }
        } else {
            add_row(root, value, NULL, path);
        }
    }
    g_string_free(path, TRUE);
    g_ptr_array_free(values, TRUE);

    g_autoptr(PreviewDoc) body = preview_doc_new();
    gint level = data_tree_level(tree, fold);
    if ((fold == NULL || fold->level == PREVIEW_FOLD_AUTO) && level != PREVIEW_FOLD_OPEN) {
        preview_doc_note(body, _("Long document: the deeper levels are folded (Tab folds or opens a "
                                 "block, Shift+Tab changes the level)"));
    }
    data_tree_print(tree, body, fold, level);
    append_summary(doc, &info, note, body);
    return TRUE;
}

/* --- Reading values --- */

JsonValue* json_value_parse(const gchar* text, gsize len, GError** error) {
    Parser p = { text, len, 0, error, FALSE, FALSE };
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
