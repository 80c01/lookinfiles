#ifndef TERMINAL_PRIVATE_H
#define TERMINAL_PRIVATE_H

/* Internal header of the terminal panel: terminal.c and term_layout.c (the
 * public API is terminal.h) */
#include "terminal/terminal.h"

#ifdef HAVE_VTE
/* Box of the shown workspace's panes, NULL before its first workspace */
GtkWidget* term_current_box(AppData* app);
/* Every terminal of the shown workspace's panes, in order */
GPtrArray* term_all_panes(AppData* app);
/* The terminal notebook holding vte as a page, or NULL */
GtkNotebook* term_notebook_of(GtkWidget* vte);
/* Put vte in nb at pos (-1: append) as the current tab; label NULL: a new one */
void term_notebook_add(GtkNotebook* nb, GtkWidget* vte, GtkWidget* label, gint pos);
/* New empty terminal notebook (a pane of the panel) */
GtkWidget* term_notebook_new(AppData* app);
/* New terminal widget running a shell (not placed yet) */
GtkWidget* term_new_pane(AppData* app, const gchar* cwd);
#endif

#endif /* TERMINAL_PRIVATE_H */
