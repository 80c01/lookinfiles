#ifndef SYSMON_PROCS_H
#define SYSMON_PROCS_H

/* The process list of the System Monitor tab: a GtkColumnView (name, PID,
 * user, CPU %, memory, threads, state) sorted by a click on a column header,
 * by CPU at first. Read only: the rows are kept from one update to the
 * next (by PID) and changed in place, the selection and the scroll position
 * stay. */
#include "core/app_types.h"
#include "core/sysstat.h"

/* The list in its scrolled window; the navigation keys of the global mode
 * move in it (focus_route_attach_list()) */
GtkWidget* sysmon_procs_new(AppData* app);
/* The column view inside, to focus */
GtkWidget* sysmon_procs_view(GtkWidget* procs);
/* Show these processes (SysProc*, read only) */
void sysmon_procs_update(GtkWidget* procs, GPtrArray* list);

#endif /* SYSMON_PROCS_H */
