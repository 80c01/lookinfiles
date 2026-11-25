#ifndef MACHINE_CONTAINERS_H
#define MACHINE_CONTAINERS_H

/* The Docker and Podman containers of each machine in the Machines view
 * (remote.c): a Docker and a Podman row below the local machine and below
 * each connected remote machine, when the program is installed there,
 * listed on a worker thread when the machine is expanded (here, or over
 * vfs_exec() on a remote machine) and again on F5 or after an action;
 * starting, stopping, restarting a container and a terminal in it
 * (`docker exec -it`, through ssh on a remote machine). */
#include "core/app_types.h"
#include "core/containers.h"
#include "explorer/explorer_row.h"
#include "explorer/remote_store.h"

#define CONTAINERS_ROW "containers"          /* row types; path: the program ("docker") */
#define CONTAINER_ROW "container"            /* path: the container id */
#define CONTAINER_HINT_ROW "container_hint"  /* "No containers", an error; path: the program */

/* Append the Docker and Podman rows below machine_row: the local machine
 * (rm NULL) or a connected remote machine. expanded: machine_row is shown
 * expanded, so its containers are listed if they are not yet. */
void machine_containers_append(ExplorerRow* machine_row, AppData* app, RemoteMachine* rm, gboolean expanded);
/* Expand the Docker and Podman rows of the expanded machines below root,
 * unless they were collapsed */
void machine_containers_restore_expanded(AppData* app, ExplorerRow* root);
/* A machine row was expanded: list its containers (FALSE: the row is
 * also the other modules'); a Docker or Podman row was expanded or
 * collapsed: remembered (TRUE) */
gboolean machine_containers_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded);
/* Expand or collapse every Docker and Podman row (next redraw) */
void machine_containers_set_expanded(gboolean expanded);
/* List the containers of the machine id again ("" the local machine,
 * NULL every machine) at the next redraw; at disconnect, forget them */
void machine_containers_forget(const gchar* id);

/* The selected row is a Docker or Podman row, a container or a hint */
gboolean machine_containers_owns_selected(AppData* app);
/* The selected container, NULL for another row (borrowed); *engine (may
 * be NULL) gets "Docker" or "Podman", *machine (may be NULL; caller frees)
 * the name of its remote machine, NULL for the local one */
const Container* machine_containers_selected(AppData* app, const gchar** engine, gchar** machine);
/* The machine row of the selected row of machine_containers_owns_selected() */
ExplorerRow* machine_containers_selected_machine_row(AppData* app);

/* Enter: a terminal in the container (expand / collapse a Docker or
 * Podman row); c: start it, or stop it when it runs; t: a terminal */
void machine_containers_activate_selected(AppData* app);
void machine_containers_toggle_selected(AppData* app);
void machine_containers_terminal_selected(AppData* app);
/* Start, stop or restart the selected container (menu) */
void machine_containers_action_selected(AppData* app, ContainerAction action);

#endif /* MACHINE_CONTAINERS_H */
