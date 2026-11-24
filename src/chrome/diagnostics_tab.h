#ifndef DIAGNOSTICS_TAB_H
#define DIAGNOSTICS_TAB_H

/* The Diagnostics tab: what this computer has of the libraries and programs
 * LookinFiles uses (diagnostics.h), in a page of the preview notebook after
 * the Git tab and the result pages. Each check shows what was found or, when
 * it is missing, the command that installs it on this system (its package
 * manager), with a Copy button. Sections: the libraries built in (installed
 * then built again), the programs, the compilers and build tools (with
 * their versions), the language servers (the missing compilers and servers
 * of languages the folder shown does not use folded). The checks run
 * in a worker thread; Check Again runs them again. A dropdown of the header
 * (shown when there are remote machines) checks a remote machine instead:
 * one command over its master connection (ssh_exec_async()), its install
 * commands for its own system; it must be connected. The workspace's
 * machine is checked first. Opened from the + menu
 * of the tab bars and the user guide's lookinfiles:diagnostics links. The
 * close button, a middle click on the tab, Ctrl+W (Vim Ctrl+W c) close it.
 * Each workspace has its own tab. */
#include "core/app_types.h"

/* Open the tab when needed and show it; the focus goes into it. id (may be
 * NULL) is a check (diagnostics.h ids: "ctags", "latex", "lsp-python"...)
 * to select once the checks are done. */
void diagnostics_tab_show(AppData* app, const gchar* id);

/* TRUE when the tab is open in the shown workspace */
gboolean diagnostics_tab_is_open(AppData* app);

/* Close the tab if it is the current page of the preview notebook (Ctrl+W).
 * Returns TRUE if it was closed. */
gboolean diagnostics_tab_close_shown(AppData* app);

/* Workspace switch (workspace.c): the tab of the workspace left is put
 * aside, that of the workspace shown comes back */
void diagnostics_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void diagnostics_tab_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its tab goes */
void diagnostics_tab_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* DIAGNOSTICS_TAB_H */
