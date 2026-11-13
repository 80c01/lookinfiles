#ifndef SYSMON_TAB_H
#define SYSMON_TAB_H

/* The System Monitor tab: a dashboard of the machine in a page of the
 * preview notebook (after the Git tab and the result pages), opened by a
 * click on the CPU chart of the top bar (sysmon_indicator.h), which closes
 * it when shown. Cards with a chart of the last two minutes, updated every
 * second: CPU (and each core), memory and swap, load average, network,
 * disk I/O; the mounted filesystems (every 5 seconds) and the processes
 * (sysmon_procs.h). Read only. The statistics are read in a worker thread
 * (sysstat.h); the processes and filesystems only while the page is on
 * screen. The navigation keys of the global mode move in the process list;
 * the close button, a middle click on the tab, Ctrl+W (Vim Ctrl+W c) close
 * it. Each workspace has its own tab. Linux only (sysstat_available()). */
#include "core/app_types.h"

/* Open the tab when needed and show it; the focus goes into it */
void sysmon_tab_show(AppData* app);

/* TRUE when the tab is open in the shown workspace */
gboolean sysmon_tab_is_open(AppData* app);

/* Close the tab if it is the current page of the preview notebook (Ctrl+W,
 * the top bar chart). Returns TRUE if it was closed. */
gboolean sysmon_tab_close_shown(AppData* app);

/* Workspace switch (workspace.c): the tab of the workspace left is put
 * aside, that of the workspace shown comes back */
void sysmon_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void sysmon_tab_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its tab goes */
void sysmon_tab_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* SYSMON_TAB_H */
