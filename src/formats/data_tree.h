#ifndef DATA_TREE_H
#define DATA_TREE_H

#include <glib.h>

#include "preview/preview_doc.h"

/* A data document (JSON...) as a tree of keys and values, printed as two
 * columns: the key indented by its depth after a ▾ / ▸ twisty, then the
 * value coloured by its type; objects and arrays show their count and fold.
 * Each container shown on several lines is a fold point named by its path,
 * each line gets the path of its value, and the keys of the first level
 * are tagbar headings. Pure GLib, unit-tested. */

/* Most lines written; the rest is replaced by a note */
#define DATA_TREE_MAX_LINES 10000

typedef enum {
    DATA_TREE_OBJECT,  /* named members: "3 keys" */
    DATA_TREE_ARRAY,   /* items: "2 items" */
    DATA_TREE_VALUE    /* a scalar */
} DataTreeKind;

typedef struct DataTree DataTree;
typedef struct DataTreeNode DataTreeNode;

/* An empty tree: its root is not shown, its children are the first level */
DataTree* data_tree_new(void);
void data_tree_free(DataTree* tree);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(DataTree, data_tree_free)

DataTreeNode* data_tree_root(DataTree* tree);

/* Add a child to parent: key is shown in the key column ("host", "0"; NULL:
 * no key, for a scalar document), path names it (the fold point id and the
 * path of its line). Both are copied. */
DataTreeNode* data_tree_add(DataTreeNode* parent, DataTreeKind kind, const gchar* key, const gchar* path);
/* Text of a scalar (len -1: NUL-terminated) and its style; line breaks and
 * tabs are shown as ↵ and spaces, a very long value is shortened */
void data_tree_set_value(DataTreeNode* node, const gchar* text, gssize len, PreviewStyle style);
/* What a container shows instead of its count ("3 elements"; "": nothing) */
void data_tree_set_summary(DataTreeNode* node, const gchar* summary);
/* The end of a cut text was reached inside this container: "…" after it */
void data_tree_set_cut(DataTreeNode* node);
guint data_tree_n_children(const DataTreeNode* node);

/* The fold level in effect: fold's, or for PREVIEW_FOLD_AUTO (fold NULL
 * too) everything open when it fits in DATA_TREE_MAX_LINES, else the
 * deepest level that fits (the first level is depth 1) */
gint data_tree_level(const DataTree* tree, const PreviewFold* fold);

/* Append the rows to doc, a line break after each, leaving out the children
 * of the containers folded at level (fold toggles them). TRUE if the rows
 * stopped at DATA_TREE_MAX_LINES (with a note). */
gboolean data_tree_print(const DataTree* tree, PreviewDoc* doc, const PreviewFold* fold, gint level);

#endif /* DATA_TREE_H */
