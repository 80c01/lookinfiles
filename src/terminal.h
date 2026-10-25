#ifndef TERMINAL_H
#define TERMINAL_H

#include "app_types.h"

#ifdef HAVE_VTE
void term_spawn_pane(AppData* app, gboolean new_row);
GPtrArray* term_visible_panes(AppData* app);
void on_term_child_exited(VteTerminal* term, gint status, gpointer data);
void on_vte_realize(GtkWidget* term, gpointer data);
#endif

#endif /* TERMINAL_H */
