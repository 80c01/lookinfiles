#ifndef MACHINE_DISKS_H
#define MACHINE_DISKS_H

/* The disks of the remote machines in the Machines view (remote.c): below a
 * connected machine, a Disks row listing them (REMOTE_DISKS_COMMAND run on
 * the machine on a worker thread when the machine is first expanded, kept
 * until it disconnects or F5); opening a disk in the explorer, through a
 * mount of its own when it is outside the machine's remote directory
 * (remote_machine_disk_local()), and a terminal in it. */
#include "core/app_types.h"
#include "core/remote_disks.h"
#include "explorer/explorer_row.h"
#include "explorer/remote_store.h"

#define MACHINE_DISKS_ROW "remote_disks" /* row types; path: the machine id */
#define MACHINE_DISK_ROW "remote_disk"   /* path: the mount folder on the machine */
#define MACHINE_HINT_ROW "remote_hint"   /* "Not connected", "Loading…"; path: the machine id */

/* Append the rows below machine_row (a container) for rm */
void machine_disks_append(ExplorerRow* machine_row, AppData* app, RemoteMachine* rm);
/* Expand the machine and Disks rows below root left expanded */
void machine_disks_restore_expanded(AppData* app, ExplorerRow* root);
/* Remember that a machine or Disks row was expanded or collapsed (and list
 * the disks of a machine expanded the first time); FALSE for other rows */
gboolean machine_disks_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded);
/* A machine row is expanded / expand or collapse them all (next redraw) */
gboolean machine_disks_any_expanded(void);
void machine_disks_set_expanded(AppData* app, gboolean expanded);
/* The row of the machine id is expanded */
gboolean machine_disks_is_expanded(const gchar* id);
/* Drop the disks listed for the machine id (NULL: every machine) */
void machine_disks_forget(const gchar* id);

/* The selected row is a Disks row, a disk or a hint row */
gboolean machine_disks_owns_selected(AppData* app);
/* The machine (id) the selected row of machine_disks_owns_selected() is of,
 * NULL for another row (borrowed) */
const gchar* machine_disks_selected_machine(AppData* app);
/* The disk listed for machine id at target, NULL when none (borrowed) */
const RemoteDisk* machine_disks_find(const gchar* id, const gchar* target);

/* Enter / c: browse the selected disk, expand the Disks row; t: an SSH
 * terminal in the disk. FALSE when the row is a hint row (the caller
 * connects its machine). */
gboolean machine_disks_activate_selected(AppData* app);
void machine_disks_terminal_selected(AppData* app);

#endif /* MACHINE_DISKS_H */
