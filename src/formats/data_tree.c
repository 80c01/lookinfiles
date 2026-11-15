/* Data documents as a tree of keys and values (see data_tree.h). */
#include "formats/data_tree.h"
#include "core/i18n.h"

#include <string.h>

/* Widest key column: longer keys push their value further */
#define KEY_COLUMN_MAX 40
/* Longest value shown, in characters */
#define VALUE_MAX_CHARS 500

struct DataTreeNode {
    DataTreeKind kind;
    gchar* key;
    gchar* path;
    gchar* value;         /* scalars; containers: the summary that replaces the count */
    PreviewStyle style;
    GPtrArray* children;  /* containers */
    gboolean cut;
};

struct DataTree {
    DataTreeNode* root;
};

static void node_free(gpointer data) {
    DataTreeNode* node = data;
    if (node->children != NULL) {
        g_ptr_array_free(node->children, TRUE);
    }
    g_free(node->key);
    g_free(node->path);
    g_free(node->value);
    g_free(node);
}

static DataTreeNode* node_new(DataTreeKind kind, const gchar* key, const gchar* path) {
    DataTreeNode* node = g_new0(DataTreeNode, 1);
    node->kind = kind;
    node->key = g_strdup(key);
    node->path = g_strdup(path);
    if (kind != DATA_TREE_VALUE) {
        node->children = g_ptr_array_new_with_free_func(node_free);
    }
    return node;
}

DataTree* data_tree_new(void) {
    DataTree* tree = g_new0(DataTree, 1);
    tree->root = node_new(DATA_TREE_OBJECT, NULL, NULL);
    return tree;
}

void data_tree_free(DataTree* tree) {
    if (tree == NULL) {
        return;
    }
    node_free(tree->root);
    g_free(tree);
}

DataTreeNode* data_tree_root(DataTree* tree) {
    return tree->root;
}

DataTreeNode* data_tree_add(DataTreeNode* parent, DataTreeKind kind, const gchar* key, const gchar* path) {
    g_return_val_if_fail(parent != NULL && parent->children != NULL, NULL);
    DataTreeNode* node = node_new(kind, key, path);
    g_ptr_array_add(parent->children, node);
    return node;
}

void data_tree_set_value(DataTreeNode* node, const gchar* text, gssize len, PreviewStyle style) {
    if (len < 0) {
        len = (gssize)strlen(text);
    }
    GString* out = g_string_sized_new(MIN((gsize)len, 64));
    guint chars = 0;
    const gchar* end = text + len;
    for (const gchar* p = text; p < end; p = g_utf8_next_char(p)) {
        if (chars >= VALUE_MAX_CHARS) {
            g_string_append(out, "…");
            break;
        }
        chars++;
        if (*p == '\n') {
            g_string_append(out, "↵");
        } else if (*p == '\r') {
            continue;
        } else if (*p == '\t') {
            g_string_append_c(out, ' ');
        } else {
            g_string_append_len(out, p, g_utf8_next_char(p) - p);
        }
    }
    g_free(node->value);
    node->value = g_string_free(out, FALSE);
    node->style = style;
}

void data_tree_set_summary(DataTreeNode* node, const gchar* summary) {
    g_free(node->value);
    node->value = g_strdup(summary);
}

void data_tree_set_cut(DataTreeNode* node) {
    node->cut = TRUE;
}

guint data_tree_n_children(const DataTreeNode* node) {
    return node->children != NULL ? node->children->len : 0;
}

/* --- Fold level --- */

static void count_depths(const DataTreeNode* node, guint depth, GArray* per_depth) {
    for (guint i = 0; i < data_tree_n_children(node); i++) {
        if (per_depth->len <= depth) {
            g_array_set_size(per_depth, depth + 1);
        }
        g_array_index(per_depth, guint64, depth)++;
        count_depths(g_ptr_array_index(node->children, i), depth + 1, per_depth);
    }
}

gint data_tree_level(const DataTree* tree, const PreviewFold* fold) {
    if (fold != NULL && fold->level != PREVIEW_FOLD_AUTO) {
        return fold->level;
    }
    /* per_depth[d]: rows at depth d + 1, shown when the level is above d */
    GArray* per_depth = g_array_new(FALSE, TRUE, sizeof(guint64));
    count_depths(tree->root, 0, per_depth);
    guint64 total = 0;
    for (guint d = 0; d < per_depth->len; d++) {
        total += g_array_index(per_depth, guint64, d);
    }
    gint level = PREVIEW_FOLD_OPEN;
    if (total >= DATA_TREE_MAX_LINES) {
        level = 1;
        guint64 rows = 0;
        for (guint d = 0; d < per_depth->len; d++) {
            rows += g_array_index(per_depth, guint64, d);
            if (rows >= DATA_TREE_MAX_LINES) {
                break;
            }
            level = (gint)d + 1;
        }
    }
    g_array_free(per_depth, TRUE);
    return level;
}

/* --- Printing --- */

typedef struct {
    PreviewDoc* doc;
    const PreviewFold* fold;
    gint level;
    guint key_column;  /* width of the indent, twisty and key column */
    guint lines;
    gboolean stopped;
} Printer;

static gboolean is_folded(const Printer* pr, const DataTreeNode* node, gint depth) {
    return data_tree_n_children(node) > 0 && preview_fold_is_folded(pr->fold, node->path, depth, pr->level);
}

/* Indent and twisty: two columns per level, the twisty in the last two */
static guint row_width(const DataTreeNode* node, gint depth) {
    return 2 * (guint)depth + (node->key != NULL ? preview_text_width(node->key) : 0);
}

/* Widest indent and key of the rows shown (capped) */
static void measure(const Printer* pr, const DataTreeNode* node, gint depth, guint* width, guint* rows) {
    for (guint i = 0; i < data_tree_n_children(node) && *rows < DATA_TREE_MAX_LINES; i++) {
        const DataTreeNode* child = g_ptr_array_index(node->children, i);
        (*rows)++;
        *width = MAX(*width, MIN(row_width(child, depth + 1), KEY_COLUMN_MAX));
        if (!is_folded(pr, child, depth + 1)) {
            measure(pr, child, depth + 1, width, rows);
        }
    }
}

static void append_styled(PreviewDoc* doc, const gchar* text, PreviewStyle style) {
    gint start = preview_doc_offset(doc);
    preview_doc_append_len(doc, text, -1);
    preview_doc_add_span(doc, start, preview_doc_offset(doc), style);
}

/* "3 keys", "2 items", "{}" / "[]" when empty, or the summary set */
static void append_count(PreviewDoc* doc, const DataTreeNode* node) {
    guint n = data_tree_n_children(node);
    gchar* count;
    if (node->value != NULL) {
        count = g_strdup(node->value);
    } else if (n == 0) {
        count = g_strdup(node->kind == DATA_TREE_OBJECT ? "{}" : "[]");
    } else if (node->kind == DATA_TREE_OBJECT) {
        count = g_strdup_printf(ngettext("%u key", "%u keys", n), n);
    } else {
        count = g_strdup_printf(ngettext("%u item", "%u items", n), n);
    }
    append_styled(doc, count, PREVIEW_STYLE_COMMENT);
    if (node->cut && count[0] != '\0') {
        preview_doc_append_len(doc, " ", 1);
    }
    if (node->cut) {
        append_styled(doc, "…", PREVIEW_STYLE_COMMENT);
    }
    g_free(count);
}

static void print_row(Printer* pr, const DataTreeNode* node, gint depth) {
    PreviewDoc* doc = pr->doc;
    gint line_start = preview_doc_offset(doc);
    if (node->path != NULL) {
        preview_doc_add_path(doc, line_start, node->path);
    }
    gboolean container = node->kind != DATA_TREE_VALUE;
    gboolean folded = is_folded(pr, node, depth);
    if (node->key != NULL) {
        for (gint i = 1; i < depth; i++) {
            preview_doc_append_len(doc, "  ", 2);
        }
        if (container && data_tree_n_children(node) > 0) {
            append_styled(doc, folded ? PREVIEW_FOLD_MARK_CLOSED : PREVIEW_FOLD_MARK_OPEN, PREVIEW_STYLE_DIM);
            preview_doc_append_len(doc, " ", 1);
        } else {
            preview_doc_append_len(doc, "  ", 2);
        }
        gint key_start = preview_doc_offset(doc);
        append_styled(doc, node->key, PREVIEW_STYLE_KEY);
        if (depth == 1) {
            preview_doc_add_span(doc, key_start, preview_doc_offset(doc), PREVIEW_STYLE_OUTLINE1);
        }
        guint width = row_width(node, depth);
        for (guint i = width; i < pr->key_column + 2; i++) {
            preview_doc_append_len(doc, " ", 1);
        }
        if (width >= pr->key_column + 2) {
            preview_doc_append_len(doc, "  ", 2);
        }
    }
    if (container) {
        append_count(doc, node);
    } else if (node->value != NULL) {
        append_styled(doc, node->value, node->style);
    }
    gint fold_end = preview_doc_offset(doc);
    preview_doc_append_len(doc, "\n", 1);
    if (++pr->lines >= DATA_TREE_MAX_LINES) {
        pr->stopped = TRUE;
    }
    if (!container || data_tree_n_children(node) == 0) {
        return;
    }
    if (!folded) {
        for (guint i = 0; i < node->children->len && !pr->stopped; i++) {
            print_row(pr, g_ptr_array_index(node->children, i), depth + 1);
        }
        fold_end = preview_doc_offset(doc) - 1; /* the end of the last row, before its line break */
    }
    preview_doc_add_fold(doc, line_start, fold_end, node->path, folded);
}

gboolean data_tree_print(const DataTree* tree, PreviewDoc* doc, const PreviewFold* fold, gint level) {
    Printer pr = { doc, fold, level, 0, 0, FALSE };
    guint rows = 0;
    measure(&pr, tree->root, 0, &pr.key_column, &rows);
    for (guint i = 0; i < tree->root->children->len && !pr.stopped; i++) {
        print_row(&pr, g_ptr_array_index(tree->root->children, i), 1);
    }
    if (tree->root->cut && !pr.stopped) {
        append_styled(doc, "…\n", PREVIEW_STYLE_COMMENT);
    }
    if (pr.stopped) {
        gchar* note = g_strdup_printf(_("… (stopped after %u lines)"), DATA_TREE_MAX_LINES);
        preview_doc_note(doc, note);
        g_free(note);
    }
    return pr.stopped;
}
