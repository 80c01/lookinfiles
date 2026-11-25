#ifndef MACHINE_WSL_H
#define MACHINE_WSL_H

/* The WSL distributions of this computer in the Machines view (Windows):
 * a WSL row below the local machine (none when no distribution is
 * installed, and on other systems), listed with wsl.exe on a worker thread
 * the first time and again on F5; opening a distribution's files
 * (\\wsl.localhost\<name>\) in the explorer, a terminal in it
 * (`wsl.exe -d <name>`), stopping it (`wsl.exe --terminate`). */
#include "core/app_types.h"
#include "core/wsl_distros.h"
#include "explorer/explorer_row.h"

#define MACHINE_WSL_ROW "machine_wsl" /* row types */
#define WSL_DISTRO_ROW "wsl_distro"   /* path: the distribution's name */

/* Append the WSL row and its distributions below local_row (the local
 * machine), once they are listed; the first call lists them */
void machine_wsl_append(ExplorerRow* local_row, AppData* app);
/* List the distributions again at the next redraw (F5) */
void machine_wsl_forget(void);
/* Expand the WSL row below root unless it was collapsed */
void machine_wsl_restore_expanded(AppData* app, ExplorerRow* root);
/* Remember that the WSL row was expanded or collapsed; FALSE for other rows */
gboolean machine_wsl_row_toggled(ExplorerRow* row, gboolean expanded);
/* Expand or collapse it (next redraw) */
void machine_wsl_set_expanded(gboolean expanded);

/* The selected row is the WSL row or a distribution */
gboolean machine_wsl_owns_selected(AppData* app);
/* The distribution of the selected row, NULL for another row (borrowed) */
const WslDistro* machine_wsl_selected(AppData* app);
/* The listed distribution named name, NULL when none (borrowed) */
const WslDistro* machine_wsl_find(const gchar* name);

/* Enter / c: browse the distribution's files, expand the WSL row; t: a
 * terminal in it; Stop (menu): wsl --terminate */
void machine_wsl_activate_selected(AppData* app);
void machine_wsl_terminal_selected(AppData* app);
void machine_wsl_stop_selected(AppData* app);

#endif /* MACHINE_WSL_H */
