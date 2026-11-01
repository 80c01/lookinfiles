#ifndef TERMINAL_H
#define TERMINAL_H

#include "app_types.h"

/* Group name of the terminal notebooks (their tabs move between them only) */
#define TERM_TABS_GROUP "term-tabs"

#ifdef HAVE_VTE
/* New terminal: a tab beside the focused one, or (new_row) a pane below it */
void term_spawn_pane(AppData* app, gboolean new_row);
/* The terminal each pane shows (its current tab) */
GPtrArray* term_visible_panes(AppData* app);
/* Focus in a terminal: show and focus the next (step 1) or previous (-1)
 * tab of its pane. FALSE if the focus is not in a pane with two tabs. */
gboolean terminal_step_tab(AppData* app, gint step);
void on_term_child_exited(VteTerminal* term, gint status, gpointer data);
void on_vte_realize(GtkWidget* term, gpointer data);

/* Pane tree of the current workspace's terminals, as text (layout_store.h);
 * "" without terminals. Caller frees. */
gchar* term_layout_capture(AppData* app);
/* The same for a workspace's terminal box (VirtualLayout term_box) */
gchar* term_layout_capture_box(GtkWidget* term_box);
/* Terminals of the current workspace (every tab of every pane) */
guint term_pane_count(AppData* app);
/* Rebuild the panes as described: running terminals are kept, in order;
 * missing panes get a new shell; terminals beyond the tree are closed */
void term_layout_apply(AppData* app, const gchar* text);
/* Terminal tabs dragged into the preview notebook belong to the workspace:
 * they leave it with the workspace (hidden), come back with it (shown), or
 * close with it (removed) */
void terminal_workspace_hidden(AppData* app, VirtualLayout* ws);
void terminal_workspace_shown(AppData* app, VirtualLayout* ws);
void terminal_workspace_removed(AppData* app, VirtualLayout* ws);
/* New shell as the last tab of nb, an editor notebook or the preview
 * notebook (a terminal tab saved there with the session); returns it */
GtkWidget* terminal_add_tab(AppData* app, GtkNotebook* nb);
/* Move vte, a terminal tab of a notebook, to the end of nb (keeping its tab label) */
void terminal_move_tab(GtkWidget* vte, GtkNotebook* nb);
/* Colours and font of every terminal again, after a theme change */
void terminal_refresh_theme(void);
#endif

/* Show the terminal panel with a new terminal tab (or, new_row, a pane
 * below the focused one), its shell in cwd (NULL: the explorer's directory). Without
 * VTE, toggles the panel's notice. */
void terminal_open(AppData* app, gboolean new_row, const gchar* cwd);

#endif /* TERMINAL_H */
