#ifndef TASK_PAGE_H
#define TASK_PAGE_H

/* The Build page of the preview notebook (after the Git and result tabs):
 * the task run last (F7, task_run.h), its messages above (errors, warnings,
 * notes: one row each, in output order; Enter or a double click opens the
 * place in an editor tab) and its output below, in a split. Its header says
 * what runs, where, the counts and the time it took, with Run Again, Stop
 * and Close buttons. One page per workspace; it moves like an editor tab
 * (drag, tab menu). */
#include "core/app_types.h"
#include "tasks/task_parse.h"

/* What the header buttons do (task_run.c sets them) */
typedef void (*TaskPageAction)(AppData* app);
void task_page_set_actions(TaskPageAction run_again, TaskPageAction stop);

/* The page of the current workspace, made when missing and shown in front
 * of its notebook (not focused), emptied for the task label running in dir.
 * Returns the page (a new reference: the run keeps writing to it even when
 * its workspace is left or the page closed). */
GtkWidget* task_page_begin(AppData* app, const gchar* label, const gchar* dir);

/* Output lines read (each ends with '\n'): kept and shown in batches */
void task_page_append_output(GtkWidget* page, const gchar* text);

/* A message read in the output */
void task_page_add_message(GtkWidget* page, const TaskMessage* msg);

/* The run ended: success, exit status (-1: did not start or was stopped),
 * stopped by the user, its time in microseconds and the counts */
void task_page_finish(GtkWidget* page, gboolean success, gint status, gboolean stopped, gint64 usec,
                      guint errors, guint warnings);

/* F8 / Shift+F8: the next / previous error or warning of the Build page of
 * the current workspace, opened in an editor tab; FALSE when the page is
 * closed or has none */
gboolean task_page_step(AppData* app, gboolean forward);

/* Each workspace has its own Build page: set aside in ws when ws is hidden,
 * put back when ws is shown, dropped with ws */
void task_page_workspace_hidden(AppData* app, VirtualLayout* ws);
void task_page_workspace_shown(AppData* app, VirtualLayout* ws);
void task_page_workspace_removed(AppData* app, VirtualLayout* ws);

/* The Build page, opened empty where it goes when closed, not shown nor
 * focused (saved layouts open it); NULL without a preview.
 * task_page_close() closes it when open. */
GtkWidget* task_page_open(AppData* app);
void task_page_close(AppData* app);

/* Close the Build tab if it has the focus or is the current page of
 * app->notebook (Ctrl+W). Returns TRUE when closed. */
gboolean task_page_close_shown(AppData* app);

#endif /* TASK_PAGE_H */
