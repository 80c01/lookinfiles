#ifndef LOCAL_MACHINE_H
#define LOCAL_MACHINE_H

/* The local machine in the Machines view (remote.c): its row, always first
 * and outside the folders, opening its files in the explorer and a terminal
 * on it. It cannot be edited nor deleted. */
#include "core/app_types.h"
#include "explorer/explorer_row.h"

#define LOCAL_MACHINE_ROW "local_machine" /* row type */

/* Append the local machine row to parent */
void local_machine_append_row(ExplorerRow* parent, AppData* app);
/* The selected row is the local machine */
gboolean local_machine_selected(AppData* app);
/* The current workspace browses the local machine (no remote machine) */
gboolean local_machine_is_current(AppData* app);
/* Make the current workspace local and show its files: the folder already
 * shown when it is local, else the home folder */
void local_machine_open(AppData* app);
/* Make the current workspace local and open a terminal there */
void local_machine_open_terminal(AppData* app);

#endif /* LOCAL_MACHINE_H */
