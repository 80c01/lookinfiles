#ifndef TERMINAL_H
#define TERMINAL_H

#include "app_types.h"

#ifdef HAVE_VTE
void term_spawn_pane(AppData* app, gboolean new_row);
GPtrArray* term_visible_panes(AppData* app);
void on_term_child_exited(VteTerminal* term, gint status, gpointer data);
void on_vte_realize(GtkWidget* term, gpointer data);

/* Pane tree of the current workspace's terminals, as text (layout_store.h);
 * "" without terminals. Caller frees. */
gchar* term_layout_capture(AppData* app);
guint term_pane_count(AppData* app);
/* Rebuild the panes as described: running terminals are kept, in order;
 * missing panes get a new shell; terminals beyond the tree are closed */
void term_layout_apply(AppData* app, const gchar* text);
/* Colours and font of every terminal again, after a theme change */
void terminal_refresh_theme(void);
#endif

#endif /* TERMINAL_H */
