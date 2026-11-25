#ifndef CONTAINERS_H
#define CONTAINERS_H

/* The containers of a machine for the Machines view, with Docker or
 * Podman (the same command line): the commands that list them, start,
 * stop or restart one and open a shell in one, as an argv to run here or a
 * shell line to run on a remote machine (vfs_exec()), and the reading of
 * the list (`ps -a --format` with CONTAINERS_FORMAT). Pure (no I/O),
 * unit-tested. */
#include <glib.h>

typedef enum {
    CONTAINER_ENGINE_DOCKER = 0,
    CONTAINER_ENGINE_PODMAN,
    CONTAINER_ENGINE_COUNT
} ContainerEngine;

typedef enum {
    CONTAINER_START = 0,
    CONTAINER_STOP,
    CONTAINER_RESTART,
} ContainerAction;

typedef struct {
    gchar* id;      /* short id */
    gchar* name;
    gchar* image;
    gchar* state;   /* "running", "exited", "paused"... ("" when unknown) */
    gchar* status;  /* "Up 3 hours (healthy)", "Exited (0) 2 days ago" */
    gchar* created; /* as the engine writes it */
    gchar* ports;   /* "0.0.0.0:8080->80/tcp", "" for none */
    gboolean running;
} Container;

/* Fields of `ps --format`, separated by '|' (never in these values) */
#define CONTAINERS_FORMAT "{{.ID}}|{{.Names}}|{{.Image}}|{{.State}}|{{.Status}}|{{.CreatedAt}}|{{.Ports}}"
/* Exit status of containers_list_command() when the program is missing */
#define CONTAINERS_MISSING 127

void container_free(gpointer container);

/* "docker", "podman" */
const gchar* container_engine_program(ContainerEngine engine);
/* "Docker", "Podman" */
const gchar* container_engine_name(ContainerEngine engine);
/* The engine whose program is name ("docker"), -1 for none */
gint container_engine_from_program(const gchar* name);

/* `<program> ps -a --format CONTAINERS_FORMAT` (g_strfreev) */
gchar** containers_list_argv(ContainerEngine engine);
/* The same as a shell line for a remote machine: exits CONTAINERS_MISSING
 * when the program is not installed there, errors on the output. Caller frees. */
gchar* containers_list_command(ContainerEngine engine);

/* The containers in the output of the list command, running ones first,
 * then by name: Container, container_free. Never NULL. */
GPtrArray* containers_parse(const gchar* output);

/* The container of containers whose id is id, NULL when none (borrowed) */
Container* containers_find(GPtrArray* containers, const gchar* id);

/* `<program> start|stop|restart <id>`, argv (g_strfreev) or shell line (g_free) */
gchar** container_action_argv(ContainerEngine engine, ContainerAction action, const gchar* id);
gchar* container_action_command(ContainerEngine engine, ContainerAction action, const gchar* id);

/* `<program> exec -it <id> sh -c '<bash, else sh>'`: argv (g_strfreev) or
 * shell line for a remote terminal (g_free) */
gchar** container_shell_argv(ContainerEngine engine, const gchar* id);
gchar* container_shell_command(ContainerEngine engine, const gchar* id);

/* What a failed command said, on one line (its first line that is not
 * empty, at most 200 characters), "" for nothing. Caller frees. */
gchar* containers_error_text(const gchar* output);

#endif /* CONTAINERS_H */
