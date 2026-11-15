/* YAML preview (see yaml_view.h). */
#include "formats/yaml_view.h"
#include "formats/text_outline.h"
#include "formats/data_tree.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

#define NO_BLOCK G_MAXUINT
#define MAX_DEPTH 128

typedef enum { NODE_MAP, NODE_SEQ } NodeKind;

/* A mapping or sequence still open */
typedef struct {
    NodeKind kind;
    gint indent;          /* column of its keys or dashes */
    gchar* path;
    gint depth;           /* 0: the root of a document */
    guint block;          /* its block in the outline, NO_BLOCK for a root */
    guint count;          /* keys or items so far */
    DataTreeNode* row;    /* its row in the tree, if one is built */
} Node;

typedef struct {
    const gchar* text;
    gsize len;
    GError** error;
    gboolean cut;
    gboolean at_cut;
    TextOutline* outline;
    GPtrArray* open;      /* Node*, innermost last */
    /* A key or item whose value starts on the next lines */
    gboolean pending;
    gint pending_indent;
    gboolean pending_same_seq; /* a key: "key:\n- item" at the same column */
    gchar* pending_path;
    gchar* pending_key;   /* its key or item number, for the tree */
    gint pending_depth;
    guint pending_line;
    gsize heading_start;  /* the key, tagged for the tagbar if a block follows */
    gsize heading_end;
    gint heading_level;
    /* A plain scalar that goes on, on lines more indented than its owner */
    gint plain_indent;    /* -1: none */
    gchar* plain_path;
    /* A block scalar (| or >): lines more indented than its owner, or blank */
    gint block_indent;    /* -1: none */
    gchar* block_path;
    gsize resume;         /* the lines starting before it were read with a multi-line value */
    gboolean scalar_doc;  /* the document is a scalar: its lines are text */
    gboolean root_seen;   /* the document has its root */
    gboolean doc_started;
    gboolean many_docs;
    guint doc;
    guint n_documents;
    gchar root;
    guint n_root;
    gboolean any_token;
    /* The tree (yaml_view_build_tree()), NULL for the text */
    DataTree* tree;
    DataTreeNode* value_row; /* the row of the value being read */
    GString* value_text;
    PreviewStyle value_style;
    gboolean block_folded;   /* the block scalar read is folded (>) */
} Parser;

static void node_free(gpointer data) {
    Node* node = data;
    g_free(node->path);
    g_free(node);
}

static gboolean fail_at(Parser* p, gsize pos, const gchar* what) {
    if (p->error != NULL && *p->error == NULL) {
        guint line = 1;
        guint col = 1;
        for (gsize i = 0; i < pos && i < p->len; i++) {
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

static void add_token(Parser* p, gsize start, gsize end, PreviewStyle style) {
    text_outline_add_token(p->outline, start, end, style);
    p->any_token |= end > start;
}

/* End of the line holding pos (before its line break) */
static gsize line_end(const Parser* p, gsize pos) {
    const gchar* nl = memchr(p->text + pos, '\n', p->len - pos);
    gsize end = nl != NULL ? (gsize)(nl - p->text) : p->len;
    if (end > pos && p->text[end - 1] == '\r') end--;
    return end;
}

static gboolean is_blank(gchar c) {
    return c == ' ' || c == '\t';
}

static gsize skip_blanks(const Parser* p, gsize pos, gsize end) {
    while (pos < end && is_blank(p->text[pos])) pos++;
    return pos;
}

/* "- " (or a dash ending the line): a sequence item */
static gboolean is_item(const Parser* p, gsize pos, gsize end) {
    return pos < end && p->text[pos] == '-' && (pos + 1 == end || is_blank(p->text[pos + 1]));
}

/* "? key" or ": value": a complex key, not read */
static gboolean is_complex_key(const Parser* p, gsize pos, gsize end) {
    return (p->text[pos] == '?' || p->text[pos] == ':') && (pos + 1 == end || is_blank(p->text[pos + 1]));
}

/* After a value: blanks, then a comment or the end of the line */
static gboolean end_of_line(Parser* p, gsize pos) {
    gsize end = line_end(p, pos);
    pos = skip_blanks(p, pos, end);
    if (pos < end && p->text[pos] == '#') {
        add_token(p, pos, end, PREVIEW_STYLE_COMMENT);
    } else if (pos < end) {
        return fail_at(p, pos, _("expected the end of the line"));
    }
    return TRUE;
}

/* A key at pos ("name", "my key", "'quoted'"): its end and the position
 * after its colon; FALSE if the line holds no key */
static gboolean scan_key(const Parser* p, gsize pos, gsize* key_end, gsize* after) {
    gsize end = line_end(p, pos);
    gchar c = pos < end ? p->text[pos] : 0;
    gsize colon;
    if (c == '"' || c == '\'') {
        gsize i = pos + 1;
        while (i < end && !(p->text[i] == c && !(c == '\'' && i + 1 < end && p->text[i + 1] == '\''))) {
            i += (c == '"' && p->text[i] == '\\') || (c == '\'' && p->text[i] == '\'') ? 2 : 1;
        }
        if (i >= end) {
            return FALSE;
        }
        *key_end = i + 1;
        colon = skip_blanks(p, i + 1, end);
        if (colon >= end || p->text[colon] != ':') {
            return FALSE;
        }
    } else {
        if (c == 0 || strchr("?[]{},&*!|>%@`#", c) != NULL) {
            return FALSE;
        }
        for (colon = pos; colon < end; colon++) {
            if (p->text[colon] == '#' && colon > pos && is_blank(p->text[colon - 1])) {
                return FALSE; /* a comment before any colon */
            }
            if (p->text[colon] == ':' && (colon + 1 == end || is_blank(p->text[colon + 1]))) {
                break;
            }
        }
        if (colon >= end) {
            return FALSE;
        }
        *key_end = colon;
        while (*key_end > pos && is_blank(p->text[*key_end - 1])) (*key_end)--;
    }
    if (colon + 1 < end && !is_blank(p->text[colon + 1])) {
        return FALSE;
    }
    *after = colon + 1;
    return TRUE;
}

static gchar* key_path(const gchar* parent, const gchar* key, gsize len) {
    return parent[0] != '\0' ? g_strdup_printf("%s.%.*s", parent, (gint)len, key) : g_strndup(key, len);
}

static Node* current(Parser* p) {
    return p->open->len > 0 ? g_ptr_array_index(p->open, p->open->len - 1) : NULL;
}

/* --- Tree --- */

/* The row the entries of the current node go under */
static DataTreeNode* parent_row(Parser* p) {
    Node* node = current(p);
    return node != NULL ? node->row : NULL;
}

/* The value read so far goes to its row */
static void flush_value(Parser* p) {
    if (p->value_row != NULL) {
        data_tree_set_value(p->value_row, p->value_text->str, (gssize)p->value_text->len, p->value_style);
        p->value_row = NULL;
    }
}

/* A scalar row under parent; the value read next fills it */
static void start_value(Parser* p, DataTreeNode* parent, const gchar* key, const gchar* path) {
    if (p->tree == NULL || parent == NULL) {
        return;
    }
    flush_value(p);
    p->value_row = data_tree_add(parent, DATA_TREE_VALUE, key, path[0] != '\0' ? path : NULL);
    g_string_truncate(p->value_text, 0);
    p->value_style = PREVIEW_STYLE_PLAIN;
}

/* Text of the value being read (its first part sets the style) */
static void add_value_text(Parser* p, gsize start, gsize end, PreviewStyle style, const gchar* separator) {
    if (p->value_row == NULL) {
        return;
    }
    if (p->value_text->len > 0) {
        g_string_append(p->value_text, separator);
    } else {
        p->value_style = style;
    }
    g_string_append_len(p->value_text, p->text + start, (gssize)(end - start));
}

/* "[2]" when the file has several documents, else "" */
static gchar* document_prefix(const Parser* p) {
    return p->many_docs ? g_strdup_printf("[%u]", p->doc) : g_strdup("");
}

/* The row of a document's root: the tree's root, or one per document */
static DataTreeNode* document_row(Parser* p, DataTreeKind kind, const gchar* path) {
    if (p->tree == NULL) {
        return NULL;
    }
    if (!p->many_docs) {
        return data_tree_root(p->tree);
    }
    gchar* key = g_strdup_printf("%u", p->doc);
    DataTreeNode* row = data_tree_add(data_tree_root(p->tree), kind, key, path);
    g_free(key);
    return row;
}

static gboolean push(Parser* p, NodeKind kind, gint indent, const gchar* path, gint depth, guint first,
                     const gchar* key) {
    if (p->open->len >= MAX_DEPTH) {
        return fail_at(p, text_outline_line_start(p->outline, first), _("nested too deeply"));
    }
    Node* node = g_new0(Node, 1);
    node->kind = kind;
    node->indent = indent;
    node->path = g_strdup(path);
    node->depth = depth;
    node->block = depth > 0 ? text_outline_add_block(p->outline, first, first, path, depth,
                                                     kind == NODE_MAP ? TEXT_OUTLINE_KEYS : TEXT_OUTLINE_ITEMS, 0,
                                                     FALSE)
                            : NO_BLOCK;
    DataTreeKind row_kind = kind == NODE_MAP ? DATA_TREE_OBJECT : DATA_TREE_ARRAY;
    node->row = depth == 0 ? document_row(p, row_kind, path)
                : parent_row(p) != NULL ? data_tree_add(parent_row(p), row_kind, key, path)
                : NULL;
    g_ptr_array_add(p->open, node);
    return TRUE;
}

static void pop(Parser* p) {
    Node* node = current(p);
    if (node->block != NO_BLOCK) {
        text_outline_block(p->outline, node->block)->count = node->count;
    } else if (p->doc == 0) {
        p->root = node->kind == NODE_MAP ? 'm' : 's';
        p->n_root = node->count;
    }
    g_ptr_array_remove_index(p->open, p->open->len - 1);
}

static void clear_pending(Parser* p) {
    p->pending = FALSE;
    g_clear_pointer(&p->pending_path, g_free);
    g_clear_pointer(&p->pending_key, g_free);
    p->plain_indent = -1;
    p->block_indent = -1;
}

/* A key or item whose value never came: an empty row */
static void pending_empty(Parser* p) {
    if (p->pending) {
        start_value(p, parent_row(p), p->pending_key, p->pending_path);
        flush_value(p);
    }
}

/* The end of a document: everything open closes */
static void close_all(Parser* p) {
    pending_empty(p);
    flush_value(p);
    while (current(p) != NULL) {
        pop(p);
    }
    clear_pending(p);
    p->root_seen = FALSE;
    p->scalar_doc = FALSE;
}

/* The open blocks reach line */
static void extend(Parser* p, guint line) {
    for (guint i = 0; i < p->open->len; i++) {
        const Node* node = g_ptr_array_index(p->open, i);
        if (node->block != NO_BLOCK) {
            TextOutlineBlock* block = text_outline_block(p->outline, node->block);
            block->last = MAX(block->last, line);
        }
    }
}

static gboolean is_word(const gchar* s, gsize n, const gchar* const* words) {
    for (; *words != NULL; words++) {
        if (strlen(*words) == n && memcmp(*words, s, n) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static PreviewStyle scalar_style(const gchar* s, gsize n) {
    static const gchar* const KEYWORDS[] = {
        "null", "Null", "NULL", "~", "true", "True", "TRUE", "false", "False", "FALSE", "yes", "Yes", "YES",
        "no", "No", "NO", "on", "On", "ON", "off", "Off", "OFF", NULL,
    };
    static const gchar* const NUMBERS[] = {
        ".inf", ".Inf", ".INF", "-.inf", "+.inf", "-.Inf", "+.Inf", ".nan", ".NaN", ".NAN", NULL,
    };
    if (is_word(s, n, KEYWORDS)) {
        return PREVIEW_STYLE_KEYWORD;
    }
    if (is_word(s, n, NUMBERS)) {
        return PREVIEW_STYLE_NUMBER;
    }
    gboolean number = n > 0 && (g_ascii_isdigit(s[0]) || ((s[0] == '-' || s[0] == '+' || s[0] == '.') && n > 1 &&
                                                         (g_ascii_isdigit(s[1]) || s[1] == '.')));
    for (gsize i = 0; number && i < n; i++) {
        number = g_ascii_isalnum(s[i]) || strchr("_+-.:", s[i]) != NULL;
    }
    return number ? PREVIEW_STYLE_NUMBER : PREVIEW_STYLE_STRING;
}

/* Anchors (&a) and tags (!t, !!str) before a value, coloured; the position after them */
static gsize skip_properties(Parser* p, gsize pos) {
    gsize end = line_end(p, pos);
    while (pos < end && (p->text[pos] == '&' || p->text[pos] == '!')) {
        gsize start = pos;
        while (pos < end && !is_blank(p->text[pos])) pos++;
        add_token(p, start, pos, PREVIEW_STYLE_KEYWORD);
        pos = skip_blanks(p, pos, end);
    }
    return pos;
}

/* A quoted string from pos, on one line or several; *close: after it */
static gboolean scan_quoted(Parser* p, gsize pos, gsize* close) {
    gchar quote = p->text[pos];
    gsize i = pos + 1;
    while (i < p->len) {
        gchar c = p->text[i];
        if (quote == '"' && c == '\\') {
            i += 2;
        } else if (c == quote && quote == '\'' && i + 1 < p->len && p->text[i + 1] == '\'') {
            i += 2;
        } else if (c == quote) {
            *close = i + 1;
            return TRUE;
        } else {
            i++;
        }
    }
    return FALSE;
}

/* A flow collection from pos: [a, {b: c}] on one line or several, its
 * scalars coloured; *close: after it, *items: its items */
static gboolean scan_flow(Parser* p, gsize pos, gsize* close, guint* items) {
    gint depth = 0;
    gsize i = pos;
    gboolean item = FALSE; /* the current item of the outer collection has something */
    *items = 0;
    while (i < p->len) {
        gchar c = p->text[i];
        if (c == '[' || c == '{') {
            item |= depth == 1;
            depth++;
            i++;
        } else if (c == ']' || c == '}') {
            depth--;
            i++;
            if (depth == 0) {
                *items += item;
                *close = i;
                return TRUE;
            }
        } else if (c == ',') {
            if (depth == 1) {
                *items += item;
                item = FALSE;
            }
            i++;
        } else if (c == '"' || c == '\'') {
            gsize end;
            if (!scan_quoted(p, i, &end)) {
                return FALSE;
            }
            add_token(p, i, end, PREVIEW_STYLE_STRING);
            item |= depth == 1;
            i = end;
        } else if (c == '#' && i > pos && (is_blank(p->text[i - 1]) || p->text[i - 1] == '\n')) {
            gsize end = line_end(p, i);
            add_token(p, i, end, PREVIEW_STYLE_COMMENT);
            i = end;
        } else if (is_blank(c) || c == '\n' || c == '\r') {
            i++;
        } else {
            gsize start = i;
            while (i < p->len && strchr(",[]{}\n\r", p->text[i]) == NULL &&
                   !(p->text[i] == ':' && (i + 1 >= p->len || strchr(" \t\n\r,]}", p->text[i + 1]) != NULL))) {
                i++;
            }
            gsize end = i;
            while (end > start && is_blank(p->text[end - 1])) end--;
            gboolean key = i < p->len && p->text[i] == ':';
            add_token(p, start, end, key ? PREVIEW_STYLE_KEY : scalar_style(p->text + start, end - start));
            i += key;
            item |= depth == 1;
        }
    }
    return FALSE;
}

/* The value at pos, owned by a key or item at column owner; it may go on
 * to the next lines */
static gboolean parse_value(Parser* p, guint line, gsize pos, const gchar* path, gint owner, gint depth) {
    gsize end = line_end(p, pos);
    gchar c = p->text[pos];
    if (c == '|' || c == '>') {
        gsize start = pos++;
        while (pos < end && (p->text[pos] == '-' || p->text[pos] == '+' || g_ascii_isdigit(p->text[pos]))) pos++;
        add_token(p, start, pos, PREVIEW_STYLE_KEYWORD);
        p->block_folded = c == '>';
        p->value_style = PREVIEW_STYLE_STRING;
        p->block_indent = owner;
        g_free(p->block_path);
        p->block_path = g_strdup(path);
        return end_of_line(p, pos);
    }
    if (c == '"' || c == '\'' || c == '[' || c == '{') {
        gsize close = 0;
        guint items = 0;
        gboolean flow = c == '[' || c == '{';
        gboolean ok = flow ? scan_flow(p, pos, &close, &items) : scan_quoted(p, pos, &close);
        if (!ok) {
            if (!p->cut) {
                return fail_at(p, pos, flow ? (c == '[' ? _("missing ]") : _("missing }")) : _("unterminated string"));
            }
            p->at_cut = TRUE;
            close = p->len;
        }
        if (!flow) {
            add_token(p, pos, close, PREVIEW_STYLE_STRING);
        }
        add_value_text(p, pos, close, flow ? PREVIEW_STYLE_PLAIN : PREVIEW_STYLE_STRING, "");
        guint last = text_outline_line_at(p->outline, close > 0 ? close - 1 : 0);
        for (guint l = line + 1; l <= last; l++) {
            text_outline_set_path(p->outline, text_outline_line_start(p->outline, l), path);
        }
        if (flow && last > line) {
            text_outline_add_block(p->outline, line, last, path, depth,
                                   c == '{' ? TEXT_OUTLINE_KEYS : TEXT_OUTLINE_ITEMS, items, TRUE);
        }
        if (last > line) {
            p->resume = text_outline_line_start(p->outline, last + 1);
            extend(p, last);
        }
        return p->at_cut || end_of_line(p, close);
    }
    if (c == '*') {
        gsize start = pos;
        while (pos < end && !is_blank(p->text[pos])) pos++;
        add_token(p, start, pos, PREVIEW_STYLE_KEYWORD);
        add_value_text(p, start, pos, PREVIEW_STYLE_KEYWORD, "");
        return end_of_line(p, pos);
    }
    /* A plain scalar, up to a comment */
    gsize stop = pos;
    while (stop < end && !(p->text[stop] == '#' && stop > pos && is_blank(p->text[stop - 1]))) stop++;
    gsize text_end = stop;
    while (text_end > pos && is_blank(p->text[text_end - 1])) text_end--;
    add_token(p, pos, text_end, scalar_style(p->text + pos, text_end - pos));
    add_value_text(p, pos, text_end, scalar_style(p->text + pos, text_end - pos), " ");
    add_token(p, stop, end, PREVIEW_STYLE_COMMENT);
    p->plain_indent = owner;
    g_free(p->plain_path);
    p->plain_path = g_strdup(path);
    return TRUE;
}

/* A key or item whose value is on the next lines */
static void expect_block(Parser* p, gint indent, gboolean same_seq, const gchar* path, gint depth, guint line,
                         const gchar* key) {
    p->pending = TRUE;
    g_free(p->pending_key);
    p->pending_key = g_strdup(key);
    p->pending_indent = indent;
    p->pending_same_seq = same_seq;
    g_free(p->pending_path);
    p->pending_path = g_strdup(path);
    p->pending_depth = depth;
    p->pending_line = line;
}

/* A document that is a scalar: one row */
static void start_scalar_document(Parser* p, const gchar* prefix) {
    if (p->tree == NULL) {
        return;
    }
    gchar* key = p->many_docs ? g_strdup_printf("%u", p->doc) : NULL;
    start_value(p, data_tree_root(p->tree), key, prefix);
    g_free(key);
}

/* The entry at pos (column indent) of line: an item, or a key */
static gboolean parse_entry(Parser* p, guint line, gsize pos, gint indent) {
    gsize end = line_end(p, pos);
    gboolean item = is_item(p, pos, end);
    gsize key_end = 0;
    gsize after = 0;
    if (p->pending) {
        gboolean below = indent > p->pending_indent || (indent == p->pending_indent && item && p->pending_same_seq);
        gsize rest = below ? skip_properties(p, pos) : pos;
        if (rest > pos && (rest >= end || p->text[rest] == '#')) {
            add_token(p, rest, end, PREVIEW_STYLE_COMMENT); /* &anchor or !tag alone: the value comes next */
            return TRUE;
        }
        if (below && indent > p->pending_indent && !item && !scan_key(p, rest, &key_end, &after)) {
            /* key:\n  value: a scalar or flow value on the next line */
            gchar* path = g_strdup(p->pending_path);
            gint owner = p->pending_indent;
            text_outline_set_path(p->outline, pos, path);
            start_value(p, parent_row(p), p->pending_key, path);
            clear_pending(p);
            gboolean ok = parse_value(p, line, rest, path, owner, p->pending_depth);
            g_free(path);
            return ok;
        }
        if (below) {
            if (!push(p, item ? NODE_SEQ : NODE_MAP, indent, p->pending_path, p->pending_depth, p->pending_line,
                      p->pending_key)) {
                return FALSE;
            }
            if (p->heading_level > 0) {
                text_outline_add_heading(p->outline, p->heading_start, p->heading_end, p->heading_level);
            }
        } else {
            pending_empty(p);
        }
        g_clear_pointer(&p->pending_path, g_free);
        g_clear_pointer(&p->pending_key, g_free);
        p->pending = FALSE;
    }
    p->heading_level = 0;
    /* Out of the nodes this line is not in */
    while (current(p) != NULL && (current(p)->indent > indent ||
                                  (current(p)->indent == indent && (current(p)->kind == NODE_SEQ) != item))) {
        pop(p);
    }
    Node* node = current(p);
    gboolean key = !item && scan_key(p, pos, &key_end, &after);
    if (node == NULL) {
        if (p->root_seen) {
            return fail_at(p, pos, _("bad indentation"));
        }
        p->root_seen = TRUE;
        gchar* prefix = document_prefix(p);
        if (!item && !key && is_complex_key(p, pos, end)) {
            g_free(prefix);
            return fail_at(p, pos, _("complex keys are not supported"));
        }
        if (!item && !key) {
            /* A scalar document: its lines are text */
            p->scalar_doc = TRUE;
            start_scalar_document(p, prefix);
            gboolean ok = parse_value(p, line, skip_properties(p, pos), prefix, -1, 1);
            g_free(prefix);
            return ok;
        }
        gboolean ok = push(p, item ? NODE_SEQ : NODE_MAP, indent, prefix, 0, line, NULL);
        g_free(prefix);
        if (!ok) {
            return FALSE;
        }
        node = current(p);
    }
    if (node->indent != indent) {
        return fail_at(p, pos, _("bad indentation"));
    }
    if (node->kind == NODE_SEQ) {
        gchar* index = g_strdup_printf("%u", node->count);
        gchar* path = g_strdup_printf("%s[%u]", node->path, node->count++);
        gsize rest = skip_properties(p, skip_blanks(p, pos + 1, end));
        gsize unused = 0;
        if (!is_item(p, rest, end) && !scan_key(p, rest, &unused, &unused)) {
            text_outline_set_path(p->outline, pos, path); /* "- key: v" gets the key's path */
        }
        gint column = indent + (gint)(rest - pos);
        gboolean ok = TRUE;
        if (rest >= end || p->text[rest] == '#') {
            add_token(p, rest, end, PREVIEW_STYLE_COMMENT);
            expect_block(p, indent, FALSE, path, node->depth + 1, line, index);
        } else if (is_item(p, rest, end) || scan_key(p, rest, &key_end, &after)) {
            /* - - a, - key: value: a node starting on the item's line */
            ok = push(p, is_item(p, rest, end) ? NODE_SEQ : NODE_MAP, column, path, node->depth + 1, line, index) &&
                 parse_entry(p, line, rest, column);
        } else {
            start_value(p, node->row, index, path);
            ok = parse_value(p, line, rest, path, indent, node->depth + 1);
        }
        g_free(index);
        g_free(path);
        return ok;
    }
    if (!key && is_complex_key(p, pos, end)) {
        return fail_at(p, pos, _("complex keys are not supported"));
    }
    if (!key) {
        return fail_at(p, pos, _("expected a key"));
    }
    add_token(p, pos, key_end, PREVIEW_STYLE_KEY);
    gchar* path = key_path(node->path, p->text + pos, key_end - pos);
    node->count++;
    text_outline_set_path(p->outline, pos, path);
    if (node->depth == 0) {
        text_outline_add_heading(p->outline, pos, key_end, 1);
    }
    gsize rest = skip_properties(p, skip_blanks(p, after, end));
    gboolean ok = TRUE;
    gchar* name = g_strndup(p->text + pos, key_end - pos);
    if (rest >= end || p->text[rest] == '#') {
        add_token(p, rest, end, PREVIEW_STYLE_COMMENT);
        expect_block(p, indent, TRUE, path, node->depth + 1, line, name);
        if (node->depth > 0) {
            p->heading_start = pos;
            p->heading_end = key_end;
            p->heading_level = MIN(node->depth + 1, 6);
        }
    } else {
        start_value(p, node->row, name, path);
        ok = parse_value(p, line, rest, path, indent, node->depth + 1);
    }
    g_free(name);
    g_free(path);
    return ok;
}

/* Several documents: their paths start with their number */
static gboolean has_many_documents(const Parser* p) {
    guint markers = 0;
    gboolean content_before = FALSE;
    for (guint l = 0; l < text_outline_n_lines(p->outline); l++) {
        gsize start = text_outline_line_start(p->outline, l);
        gsize end = line_end(p, start);
        gboolean marker = end - start >= 3 && memcmp(p->text + start, "---", 3) == 0 &&
                          (end - start == 3 || is_blank(p->text[start + 3]));
        markers += marker;
        gsize first = skip_blanks(p, start, end);
        if (!marker && markers == 0 && first < end && p->text[first] != '#' && p->text[first] != '%') {
            content_before = TRUE;
        }
    }
    return markers + content_before > 1;
}

static gboolean parse_document(Parser* p) {
    p->many_docs = has_many_documents(p);
    guint n_lines = text_outline_n_lines(p->outline);
    for (guint line = 0; line < n_lines && !p->at_cut; line++) {
        gsize start = text_outline_line_start(p->outline, line);
        if (start < p->resume) {
            continue; /* read with a multi-line value */
        }
        gsize end = line_end(p, start);
        if (line == 0 && end - start >= 3 && memcmp(p->text, "\xEF\xBB\xBF", 3) == 0) {
            start += 3; /* byte order mark */
        }
        gsize pos = start;
        while (pos < end && p->text[pos] == ' ') pos++;
        gint indent = (gint)(pos - start);
        if (pos < end && p->text[pos] == '\t') {
            gsize next = skip_blanks(p, pos, end);
            if (next < end && p->block_indent < 0) {
                return fail_at(p, pos, _("tabs are not allowed in indentation"));
            }
        }
        if (skip_blanks(p, pos, end) >= end) {
            continue; /* a blank line */
        }
        if (p->block_indent >= 0 && indent > p->block_indent) {
            add_token(p, pos, end, PREVIEW_STYLE_STRING); /* in a block scalar */
            add_value_text(p, pos, end, PREVIEW_STYLE_STRING, p->block_folded ? " " : "\n");
            text_outline_set_path(p->outline, start, p->block_path);
            extend(p, line);
            continue;
        }
        p->block_indent = -1;
        if (p->text[pos] == '#') {
            add_token(p, pos, end, PREVIEW_STYLE_COMMENT);
            continue;
        }
        gboolean marker = indent == 0 && end - pos >= 3 && (memcmp(p->text + pos, "---", 3) == 0 ||
                                                            memcmp(p->text + pos, "...", 3) == 0) &&
                          (end - pos == 3 || is_blank(p->text[pos + 3]));
        if (marker) {
            gboolean starts = p->text[pos] == '-';
            add_token(p, pos, pos + 3, PREVIEW_STYLE_KEYWORD);
            close_all(p);
            if (starts) {
                p->doc += p->doc_started;
                p->doc_started = TRUE;
                p->n_documents = p->doc + 1;
                gsize rest = skip_properties(p, skip_blanks(p, pos + 3, end));
                if (rest < end && p->text[rest] != '#') {
                    /* --- text, --- | : a scalar document */
                    p->root_seen = TRUE;
                    p->scalar_doc = TRUE;
                    gchar* prefix = document_prefix(p);
                    start_scalar_document(p, prefix);
                    gboolean ok = parse_value(p, line, rest, prefix, -1, 1);
                    g_free(prefix);
                    if (!ok) {
                        return FALSE;
                    }
                } else {
                    add_token(p, rest, end, PREVIEW_STYLE_COMMENT);
                }
            }
            continue;
        }
        if (indent == 0 && p->text[pos] == '%' && !p->doc_started) {
            add_token(p, pos, end, PREVIEW_STYLE_KEYWORD); /* %YAML 1.2, %TAG */
            continue;
        }
        if (!p->doc_started) {
            p->doc_started = TRUE;
            p->n_documents = 1;
        }
        if (p->scalar_doc || (p->plain_indent >= 0 && indent > p->plain_indent)) {
            /* A plain scalar going on: no key in it */
            gsize key_end = 0;
            gsize after = 0;
            if (!p->scalar_doc && scan_key(p, pos, &key_end, &after)) {
                return fail_at(p, pos, _("bad indentation"));
            }
            gsize stop = pos;
            while (stop < end && !(p->text[stop] == '#' && stop > pos && is_blank(p->text[stop - 1]))) stop++;
            gsize text_end = stop;
            while (text_end > pos && is_blank(p->text[text_end - 1])) text_end--;
            add_token(p, pos, text_end, PREVIEW_STYLE_STRING);
            add_value_text(p, pos, text_end, PREVIEW_STYLE_STRING, " ");
            add_token(p, stop, end, PREVIEW_STYLE_COMMENT);
            const gchar* path = p->scalar_doc ? NULL : p->plain_path;
            if (path != NULL && path[0] != '\0') {
                text_outline_set_path(p->outline, start, path);
            }
            extend(p, line);
            continue;
        }
        p->plain_indent = -1;
        if (!parse_entry(p, line, pos, indent)) {
            return FALSE;
        }
        extend(p, line);
    }
    close_all(p);
    if (!p->any_token) {
        return fail_at(p, 0, _("empty document"));
    }
    return TRUE;
}

/* Parse text into p (the tree too if tree is not NULL); free with parser_clear() */
static gboolean parse_text(Parser* p, const gchar* text, gsize len, gboolean cut, DataTree* tree, GError** error) {
    if (cut) {
        /* The last line is incomplete */
        while (len > 0 && text[len - 1] != '\n') len--;
    }
    memset(p, 0, sizeof *p);
    p->text = text;
    p->len = len;
    p->error = error;
    p->cut = cut;
    p->outline = text_outline_new(text, len);
    p->open = g_ptr_array_new_with_free_func(node_free);
    p->plain_indent = -1;
    p->block_indent = -1;
    p->tree = tree;
    p->value_text = g_string_new(NULL);
    return parse_document(p);
}

static void parser_clear(Parser* p) {
    text_outline_free(p->outline);
    g_ptr_array_unref(p->open);
    g_free(p->pending_path);
    g_free(p->pending_key);
    g_free(p->plain_path);
    g_free(p->block_path);
    g_string_free(p->value_text, TRUE);
}

static void note_long(PreviewDoc* doc, const PreviewFold* fold, gint level) {
    if ((fold == NULL || fold->level == PREVIEW_FOLD_AUTO) && level != PREVIEW_FOLD_OPEN) {
        preview_doc_note(doc, _("Long document: the deeper levels are folded (Tab folds or opens a "
                                "block, Shift+Tab changes the level)"));
    }
}

gboolean yaml_view_append(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                          const PreviewFold* fold, YamlViewInfo* info, GError** error) {
    Parser p;
    gboolean ok = parse_text(&p, text, len, cut, NULL, error);
    if (ok) {
        YamlViewInfo result = { MAX(p.n_documents, 1), p.root, p.n_root, text_outline_level(p.outline, fold),
                                p.at_cut };
        if (info != NULL) {
            *info = result;
        }
        note_long(doc, fold, result.level);
        text_outline_print(p.outline, doc, fold, result.level);
        if (cut) {
            preview_doc_append(doc, PREVIEW_STYLE_COMMENT, "…\n"); /* the rest was not read */
        }
    }
    parser_clear(&p);
    return ok;
}

/* The dimmed summary line ("YAML · 5 keys"), the note, then body */
static void append_summary(PreviewDoc* doc, const YamlViewInfo* info, const gchar* note, const PreviewDoc* body) {
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* count = preview_doc_count(info->n_documents > 1 ? info->n_documents : info->n_root);
    if (info->n_documents > 1) {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM,
                            ngettext("YAML · %s document\n", "YAML · %s documents\n", info->n_documents), count);
    } else if (info->root == 'm') {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, ngettext("YAML · %s key\n", "YAML · %s keys\n", info->n_root),
                            count);
    } else if (info->root == 's') {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, ngettext("YAML · %s item\n", "YAML · %s items\n", info->n_root),
                            count);
    } else {
        preview_doc_append(doc, PREVIEW_STYLE_DIM, "YAML\n");
    }
    g_free(count);
    if (note != NULL) {
        preview_doc_note(doc, note);
    }
    preview_doc_append_len(doc, "\n", 1);
    preview_doc_append_doc(doc, body); /* with its fold points and paths */
}

gboolean yaml_view_build(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                         const PreviewFold* fold, const gchar* note, GError** error) {
    g_autoptr(PreviewDoc) body = preview_doc_new();
    YamlViewInfo info;
    if (!yaml_view_append(body, text, len, cut, fold, &info, error)) {
        return FALSE;
    }
    append_summary(doc, &info, note, body);
    return TRUE;
}

gboolean yaml_view_build_tree(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                              const PreviewFold* fold, const gchar* note, GError** error) {
    g_autoptr(DataTree) tree = data_tree_new();
    Parser p;
    gboolean ok = parse_text(&p, text, len, cut, tree, error);
    YamlViewInfo info = { MAX(p.n_documents, 1), p.root, p.n_root, 0, p.at_cut };
    parser_clear(&p);
    if (!ok) {
        return FALSE;
    }
    if (cut) {
        data_tree_set_cut(data_tree_root(tree));
    }
    g_autoptr(PreviewDoc) body = preview_doc_new();
    info.level = data_tree_level(tree, fold);
    note_long(body, fold, info.level);
    data_tree_print(tree, body, fold, info.level);
    append_summary(doc, &info, note, body);
    return TRUE;
}
