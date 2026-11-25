#ifndef LOCAL_MACHINE_H
#define LOCAL_MACHINE_H

/* The local machine in the Machines view (remote.c): its row, always first
 * and outside the folders, with its disks below (disk_volumes.h); opening
 * its files or a disk in the explorer, mounting, unmounting and ejecting a
 * disk, a terminal on it. It cannot be edited nor deleted. The view is
 * redrawn (APP_EVENT_FILES_CHANGED) when a disk comes or goes. */
#include "core/app_types.h"
#include "core/disk_volumes.h"
#include "explorer/explorer_row.h"

#define LOCAL_MACHINE_ROW "local_machine" /* row types */
#define LOCAL_DISKS_ROW "machine_disks"
#define LOCAL_DISK_ROW "disk" /* path: the DiskVolume id */

/* Append the local machine row, its disks below, to parent; returns it */
ExplorerRow* local_machine_append_row(ExplorerRow* parent, AppData* app);
/* Expand the rows of the local machine below root left expanded */
void local_machine_restore_expanded(AppData* app, ExplorerRow* root);
/* Remember that a row of the local machine was expanded or collapsed;
 * FALSE for other rows */
gboolean local_machine_row_toggled(ExplorerRow* row, gboolean expanded);
/* The local machine row is expanded / expand or collapse all its rows (at
 * the next redraw) */
gboolean local_machine_any_expanded(void);
void local_machine_set_expanded(gboolean expanded);

/* The selected row is the local machine */
gboolean local_machine_selected(AppData* app);
/* The selected row is the local machine or one of its rows */
gboolean local_machine_owns_selected(AppData* app);
/* The disk of the selected row, NULL for another row (borrowed, until the
 * next redraw) */
const DiskVolume* local_machine_selected_disk(AppData* app);
/* The listed disk whose id is id, NULL when none (borrowed) */
const DiskVolume* local_machine_disk(const gchar* id);

/* The current workspace browses the local machine (no remote machine) */
gboolean local_machine_is_current(AppData* app);
/* Make the current workspace local and show its files: the folder already
 * shown when it is local, else the home folder */
void local_machine_open(AppData* app);
/* Make the current workspace local and open a terminal there */
void local_machine_open_terminal(AppData* app);
/* Make the current workspace local (no remote machine) */
void local_machine_make_current(AppData* app);
/* Make the current workspace local and show dir in the files view */
void local_machine_open_dir(AppData* app, const gchar* dir);

/* Keys of the local machine rows: Enter (open the files, the disk, mounted
 * first; expand the Disks row), c (also mount, unmount or eject a disk), t
 * (a terminal, in the disk for a disk) */
void local_machine_activate_selected(AppData* app);
void local_machine_toggle_selected(AppData* app);
void local_machine_terminal_selected(AppData* app);

#endif /* LOCAL_MACHINE_H */
