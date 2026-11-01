#ifndef EXPLORER_TREE_H
#define EXPLORER_TREE_H

/* Explorer tree view: a GtkListView over a GtkTreeListModel of ExplorerRow.
 *
 * Signals:
 *   "selection-changed"  void (ExplorerTree*, gpointer)            selected row changed
 *   "row-toggled"        void (ExplorerTree*, ExplorerRow*, gboolean expanded, gpointer)
 *                        a row was expanded or collapsed (by the user or by
 *                        explorer_tree_set_expanded()); the files view loads
 *                        directory contents here
 *   "row-activated"      void (ExplorerTree*, ExplorerRow*, gpointer)   double click
 *
 * Row positions count visible rows only (children of collapsed rows are not
 * visible). Returned rows are borrowed: they live as long as they stay in the tree. */
#include <gtk/gtk.h>
#include "explorer_row.h"

#define EXPLORER_TYPE_TREE (explorer_tree_get_type())
G_DECLARE_FINAL_TYPE(ExplorerTree, explorer_tree, EXPLORER, TREE, GObject)

ExplorerTree* explorer_tree_new(void);

/* The GtkListView, to pack in a scrolled window */
GtkWidget* explorer_tree_get_widget(ExplorerTree* tree);

/* Rows, expansion, selection and scroll position of the tree, kept aside
 * (one per workspace) */
typedef struct _ExplorerTreeState ExplorerTreeState;

/* Show state's content in the tree (a new empty one if state is NULL; takes
 * ownership of state) and return the content shown until now. The selection
 * moves with the content without emitting "selection-changed". */
ExplorerTreeState* explorer_tree_swap_state(ExplorerTree* tree, ExplorerTreeState* state);
void explorer_tree_state_free(ExplorerTreeState* state);

/* Invisible root row: append top-level rows to it */
ExplorerRow* explorer_tree_get_root(ExplorerTree* tree);

/* Remove every row */
void explorer_tree_clear(ExplorerTree* tree);

ExplorerRow* explorer_tree_get_selected(ExplorerTree* tree);

/* Select a visible row and scroll it into view. Selecting the selected row
 * again emits "selection-changed" too. FALSE if the row is not visible. */
gboolean explorer_tree_select(ExplorerTree* tree, ExplorerRow* row);
void explorer_tree_unselect_all(ExplorerTree* tree);

gboolean explorer_tree_is_expanded(ExplorerTree* tree, ExplorerRow* row);
void explorer_tree_set_expanded(ExplorerTree* tree, ExplorerRow* row, gboolean expanded);
void explorer_tree_toggle_expanded(ExplorerTree* tree, ExplorerRow* row);
/* Expand every ancestor of row so it becomes visible */
void explorer_tree_expand_to(ExplorerTree* tree, ExplorerRow* row);
void explorer_tree_collapse_all(ExplorerTree* tree);

guint explorer_tree_n_visible(ExplorerTree* tree);
ExplorerRow* explorer_tree_visible_row(ExplorerTree* tree, guint position);
/* Position among visible rows, -1 if the row is hidden */
gint explorer_tree_position(ExplorerTree* tree, ExplorerRow* row);

/* Scroll so the row sits at `align` of the page (0 top, 0.5 center, 1 bottom) */
void explorer_tree_scroll_to(ExplorerTree* tree, ExplorerRow* row, gdouble align);

/* Number of rows fitting in the visible page (at least 1) */
gint explorer_tree_page_rows(ExplorerTree* tree);

/* Row under widget coordinates (x, y) of the list view, NULL if none */
ExplorerRow* explorer_tree_row_at(ExplorerTree* tree, gdouble x, gdouble y);

#endif /* EXPLORER_TREE_H */
