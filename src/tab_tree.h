#ifndef TAB_TREE_H
#define TAB_TREE_H

/* The editor tabs of a workspace as text, saved with the session: which
 * file is in which notebook, how the notebooks are split, the current page of
 * each notebook and the view last focused.
 *
 * A notebook is "[<tab>,<tab>...]", each tab the index of its file in the
 * session's file list, followed by "*" for the current page and "!" for the
 * view last focused; "H:<frac>(<a>,<b>)" splits side by side and
 * "V:<frac>(<a>,<b>)" one above the other, <frac> being the share of the
 * first child, e.g. "H:0.500([0*,2],V:0.400([1!],[0]))". A file may be in
 * several notebooks (split views of one file).
 *
 * Pure data, no widgets: unit-tested in tests/. */
#include <glib.h>

#define TAB_TREE_MAX_TABS 256

typedef struct TabTreeNode {
    gboolean is_notebook;
    GArray* tabs;    /* notebook: guint file indices, in page order */
    gint current;    /* notebook: position in tabs of the current page, -1 if none */
    gint focused;    /* notebook: position in tabs of the view last focused, -1 if none */
    gboolean vertical;
    gdouble frac;
    struct TabTreeNode* first;
    struct TabTreeNode* second;
} TabTreeNode;

/* An empty notebook */
TabTreeNode* tab_tree_notebook_new(void);
/* Add a tab (file index) to a notebook; returns its position */
guint tab_tree_notebook_add(TabTreeNode* notebook, guint file);
/* A split of first and second (taking ownership); one of them NULL gives the other */
TabTreeNode* tab_tree_split_new(gboolean vertical, gdouble frac, TabTreeNode* first, TabTreeNode* second);

/* NULL if text is empty or invalid */
TabTreeNode* tab_tree_parse(const gchar* text);
/* "" for NULL */
gchar* tab_tree_to_string(const TabTreeNode* node);
void tab_tree_free(TabTreeNode* node);

#endif /* TAB_TREE_H */
