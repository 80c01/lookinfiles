#include "explorer/machine_containers.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/ssh_session.h"
#include "core/vfs.h"
#include "explorer/explorer_tree.h"
#include "explorer/local_machine.h"
#include "explorer/tree_nav.h"
#include "terminal/terminal.h"
#include "theme/palette.h"

#include <string.h>

/* What is known of one engine on one machine */
typedef struct {
    GPtrArray* list;  /* Container*, NULL until listed */
    gchar* error;     /* why it could not list, NULL when it could */
    gboolean absent;  /* the program is not installed there */
    gboolean listed;  /* list, error or absent is the answer */
    gboolean loading;
} EngineState;

/* Per "<machine key>/<program>" (key "" for the local machine): its state,
 * and the Docker / Podman rows collapsed (session only: they start expanded) */
static struct {
    AppData* app;
    GHashTable* states;    /* key -> EngineState */
    GHashTable* collapsed; /* key set */
} mc;

static void state_free(gpointer data) {
    EngineState* s = data;
    g_clear_pointer(&s->list, g_ptr_array_unref);
    g_free(s->error);
    g_free(s);
}

static void ensure_tables(void) {
    if (mc.states == NULL) {
        mc.states = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, state_free);
        mc.collapsed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
}

static gchar* state_key(const gchar* machine, ContainerEngine engine) {
    return g_strconcat(machine != NULL ? machine : "", "/", container_engine_program(engine), NULL);
}

static EngineState* state_get(const gchar* machine, ContainerEngine engine) {
    ensure_tables();
    gchar* key = state_key(machine, engine);
    EngineState* s = g_hash_table_lookup(mc.states, key);
    if (s == NULL) {
        s = g_new0(EngineState, 1);
        g_hash_table_insert(mc.states, key, s);
    } else {
        g_free(key);
    }
    return s;
}

void machine_containers_forget(const gchar* id) {
    ensure_tables();
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, mc.states);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        const gchar* k = key;
        EngineState* s = value;
        gsize n = (id != NULL) ? strlen(id) : 0;
        if (id == NULL || (strncmp(k, id, n) == 0 && k[n] == '/')) {
            s->listed = FALSE; /* the rows stay until the new answer */
        }
    }
}

static void redraw(void) {
    if (mc.app != NULL && mc.app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        app_emit(mc.app, APP_EVENT_FILES_CHANGED); /* rebuilds the Machines view */
    }
}

/* ------------------------------------------------------------------------- */
/* Commands on a worker thread                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    gchar* machine;  /* "" for the local machine, else the machine id */
    gchar* mount;    /* its folder (vfs_exec()), NULL for the local machine */
    ContainerEngine engine;
    gchar** argv;    /* here */
    gchar* command;  /* on the remote machine */
    gchar* name;     /* the container acted on, NULL for a listing */
    ContainerAction action;
    /* answer */
    gboolean ran;
    gint status;
    gchar* output;
    gchar* error;
} Job;

static void job_free(gpointer data) {
    Job* j = data;
    g_free(j->machine);
    g_free(j->mount);
    g_strfreev(j->argv);
    g_free(j->command);
    g_free(j->name);
    g_free(j->output);
    g_free(j->error);
    g_free(j);
}

static void job_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    Job* j = data;
    GBytes* out = NULL;
    GError* error = NULL;
    if (j->mount != NULL) {
        j->ran = vfs_exec(j->mount, j->command, NULL, cancel, &j->status, &out, &error);
    } else {
        gchar* found = g_find_program_in_path(j->argv[0]);
        if (found == NULL) {
            j->ran = TRUE;
            j->status = CONTAINERS_MISSING;
        } else {
            g_free(found);
            GSubprocess* proc = g_subprocess_newv((const gchar* const*)j->argv,
                                                  G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
                                                  &error);
            if (proc != NULL && g_subprocess_communicate(proc, NULL, cancel, &out, NULL, &error)) {
                j->ran = TRUE;
                j->status = g_subprocess_get_if_exited(proc) ? g_subprocess_get_exit_status(proc) : -1;
            }
            g_clear_object(&proc);
        }
    }
    if (out != NULL) {
        gsize len = 0;
        const gchar* bytes = g_bytes_get_data(out, &len);
        j->output = g_utf8_make_valid(bytes != NULL ? bytes : "", (gssize)len);
        g_bytes_unref(out);
    }
    if (error != NULL) {
        j->error = g_strdup(error->message);
        g_error_free(error);
    }
    g_task_return_boolean(task, TRUE);
}

/* A remote machine still connected, NULL for the local machine or one gone */
static RemoteMachine* job_machine(const Job* j) {
    if (j->machine[0] == '\0' || mc.app == NULL) {
        return NULL;
    }
    RemoteMachine* rm = remote_find_by_id(mc.app, j->machine);
    return (rm != NULL && rm->is_connected) ? rm : NULL;
}

static void on_listed(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    Job* j = g_task_get_task_data(G_TASK(res));
    if (j->machine[0] != '\0' && job_machine(j) == NULL) {
        state_get(j->machine, j->engine)->loading = FALSE;
        return; /* disconnected meanwhile */
    }
    EngineState* s = state_get(j->machine, j->engine);
    s->loading = FALSE;
    s->listed = TRUE;
    gboolean was_shown = (s->list != NULL || s->error != NULL);
    g_clear_pointer(&s->list, g_ptr_array_unref);
    g_clear_pointer(&s->error, g_free);
    s->absent = FALSE;
    if (!j->ran) {
        s->error = g_strdup(j->error != NULL ? j->error : "");
    } else if (j->status == CONTAINERS_MISSING) {
        s->absent = TRUE;
    } else if (j->status != 0) {
        s->error = containers_error_text(j->output);
    } else {
        s->list = containers_parse(j->output);
    }
    if (was_shown || !s->absent) {
        redraw();
    }
}

static Job* job_new(const gchar* machine, RemoteMachine* rm, ContainerEngine engine) {
    Job* j = g_new0(Job, 1);
    j->machine = g_strdup(machine);
    j->mount = (rm != NULL) ? g_strdup(rm->mount_point) : NULL;
    j->engine = engine;
    return j;
}

static void run_job(Job* j, GAsyncReadyCallback done) {
    GTask* task = g_task_new(NULL, NULL, done, NULL);
    g_task_set_task_data(task, j, job_free);
    g_task_run_in_thread(task, job_thread);
    g_object_unref(task);
}

/* List the containers of both engines on the machine (rm NULL: here) unless done */
static void maybe_list(const gchar* machine, RemoteMachine* rm) {
    if (rm != NULL && (!rm->is_connected || rm->mount_point == NULL)) {
        return;
    }
    for (gint e = 0; e < CONTAINER_ENGINE_COUNT; e++) {
        EngineState* s = state_get(machine, (ContainerEngine)e);
        if (s->listed || s->loading) {
            continue;
        }
        s->loading = TRUE;
        Job* j = job_new(machine, rm, (ContainerEngine)e);
        if (rm != NULL) {
            j->command = containers_list_command((ContainerEngine)e);
        } else {
            j->argv = containers_list_argv((ContainerEngine)e);
        }
        run_job(j, on_listed);
    }
}

/* ------------------------------------------------------------------------- */
/* Rows                                                                      */
/* ------------------------------------------------------------------------- */

static void append_container_row(ExplorerRow* parent, const Container* c) {
    ExplorerRow* row = explorer_row_append(parent, CONTAINER_ROW, c->name, c->name, c->id);
    explorer_row_set_glyph(row, c->running ? "container-running" : "container");
    gchar* text = g_strdup_printf("%s · %s", c->image, c->running ? C_("container", "running") : C_("container", "stopped"));
    gchar* badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>",
                                           palette_color(c->running ? PAL_SUCCESS : PAL_FG_SUBTLE), text);
    explorer_row_set_badge(row, badge);
    g_free(badge);
    g_free(text);
}

void machine_containers_append(ExplorerRow* machine_row, AppData* app, RemoteMachine* rm, gboolean expanded) {
    if (machine_row == NULL) {
        return;
    }
    mc.app = app;
    const gchar* machine = (rm != NULL) ? rm->id : "";
    if (rm != NULL && !rm->is_connected) {
        return;
    }
    if (expanded) {
        maybe_list(machine, rm);
    }
    for (gint e = 0; e < CONTAINER_ENGINE_COUNT; e++) {
        ContainerEngine engine = (ContainerEngine)e;
        EngineState* s = state_get(machine, engine);
        if (s->list == NULL && s->error == NULL) {
            continue; /* not listed yet, or not installed */
        }
        const gchar* name = container_engine_name(engine);
        const gchar* program = container_engine_program(engine);
        ExplorerRow* group = explorer_row_append_container(machine_row, CONTAINERS_ROW, name, name, program);
        explorer_row_set_glyph(group, "container");
        if (s->error != NULL) {
            /* TRANSLATORS: %1$s is Docker or Podman, %2$s what it said */
            gchar* text = g_strdup_printf(_("%1$s does not answer: %2$s"), name, s->error);
            explorer_row_append(group, CONTAINER_HINT_ROW, text, text, program);
            g_free(text);
            continue;
        }
        gchar* count = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT),
                                       s->list->len);
        explorer_row_set_badge(group, count);
        g_free(count);
        if (s->list->len == 0) {
            explorer_row_append(group, CONTAINER_HINT_ROW, _("No containers"), _("No containers"), program);
        }
        for (guint i = 0; i < s->list->len; i++) {
            append_container_row(group, g_ptr_array_index(s->list, i));
        }
    }
}

/* The machine key of a machine row: "" here, else the machine id */
static const gchar* machine_key(ExplorerRow* machine_row) {
    return explorer_row_is(machine_row, LOCAL_MACHINE_ROW) ? "" : machine_row->path;
}

static void restore_machine(AppData* app, ExplorerRow* machine) {
    if (!explorer_tree_is_expanded(app->tree, machine)) {
        return;
    }
    for (guint j = 0; j < explorer_row_n_children(machine); j++) {
        ExplorerRow* group = explorer_row_child(machine, j);
        if (!explorer_row_is(group, CONTAINERS_ROW)) {
            continue;
        }
        gchar* key = g_strconcat(machine_key(machine), "/", group->path, NULL);
        if (!g_hash_table_contains(mc.collapsed, key)) {
            explorer_tree_set_expanded(app->tree, group, TRUE);
        }
        g_free(key);
    }
}

void machine_containers_restore_expanded(AppData* app, ExplorerRow* root) {
    if (app == NULL || app->tree == NULL || root == NULL) {
        return;
    }
    ensure_tables();
    for (guint i = 0; i < explorer_row_n_children(root); i++) {
        ExplorerRow* row = explorer_row_child(root, i);
        if (explorer_row_is(row, LOCAL_MACHINE_ROW) || explorer_row_is(row, "remote")) {
            restore_machine(app, row);
        } else if (explorer_row_is(row, "remote_folder") && explorer_tree_is_expanded(app->tree, row)) {
            for (guint j = 0; j < explorer_row_n_children(row); j++) {
                ExplorerRow* machine = explorer_row_child(row, j);
                if (explorer_row_is(machine, "remote")) {
                    restore_machine(app, machine);
                }
            }
        }
    }
}

gboolean machine_containers_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded) {
    ensure_tables();
    mc.app = app;
    if (expanded && explorer_row_is(row, LOCAL_MACHINE_ROW)) {
        maybe_list("", NULL);
        return FALSE;
    }
    if (expanded && explorer_row_is(row, "remote")) {
        maybe_list(row->path, remote_find_by_id(app, row->path));
        return FALSE;
    }
    if (!explorer_row_is(row, CONTAINERS_ROW)) {
        return FALSE;
    }
    ExplorerRow* machine = explorer_row_get_parent(row);
    if (machine == NULL) {
        return TRUE;
    }
    gchar* key = g_strconcat(machine_key(machine), "/", row->path, NULL);
    if (expanded) {
        g_hash_table_remove(mc.collapsed, key);
        g_free(key);
    } else {
        g_hash_table_add(mc.collapsed, key);
    }
    return TRUE;
}

void machine_containers_set_expanded(gboolean expanded) {
    ensure_tables();
    if (expanded) {
        g_hash_table_remove_all(mc.collapsed);
        return;
    }
    GHashTableIter it;
    gpointer key;
    g_hash_table_iter_init(&it, mc.states);
    while (g_hash_table_iter_next(&it, &key, NULL)) {
        g_hash_table_add(mc.collapsed, g_strdup(key));
    }
}

/* ------------------------------------------------------------------------- */
/* Selection and actions                                                     */
/* ------------------------------------------------------------------------- */

gboolean machine_containers_owns_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, CONTAINERS_ROW) || explorer_row_is(row, CONTAINER_ROW) ||
           explorer_row_is(row, CONTAINER_HINT_ROW);
}

/* The Docker / Podman row of the selected row, NULL for another row */
static ExplorerRow* selected_group(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    if (explorer_row_is(row, CONTAINERS_ROW)) {
        return row;
    }
    if (explorer_row_is(row, CONTAINER_ROW) || explorer_row_is(row, CONTAINER_HINT_ROW)) {
        ExplorerRow* group = explorer_row_get_parent(row);
        return explorer_row_is(group, CONTAINERS_ROW) ? group : NULL;
    }
    return NULL;
}

ExplorerRow* machine_containers_selected_machine_row(AppData* app) {
    ExplorerRow* group = selected_group(app);
    return (group != NULL) ? explorer_row_get_parent(group) : NULL;
}

/* The selected container with its engine, machine key and remote machine
 * (NULL for the local one) */
static Container* selected_full(AppData* app, ContainerEngine* engine, const gchar** machine, RemoteMachine** rm) {
    ExplorerRow* row = tree_nav_selected(app);
    ExplorerRow* group = selected_group(app);
    ExplorerRow* mrow = (group != NULL) ? explorer_row_get_parent(group) : NULL;
    if (!explorer_row_is(row, CONTAINER_ROW) || mrow == NULL) {
        return NULL;
    }
    gint e = container_engine_from_program(group->path);
    if (e < 0) {
        return NULL;
    }
    const gchar* key = machine_key(mrow);
    EngineState* s = state_get(key, (ContainerEngine)e);
    if (engine != NULL) *engine = (ContainerEngine)e;
    if (machine != NULL) *machine = key;
    if (rm != NULL) *rm = (key[0] != '\0') ? remote_find_by_id(app, key) : NULL;
    return containers_find(s->list, row->path);
}

const Container* machine_containers_selected(AppData* app, const gchar** engine, gchar** machine) {
    ContainerEngine e = CONTAINER_ENGINE_DOCKER;
    RemoteMachine* rm = NULL;
    if (machine != NULL) *machine = NULL;
    Container* c = (app != NULL) ? selected_full(app, &e, NULL, &rm) : NULL;
    if (c == NULL) {
        return NULL;
    }
    if (engine != NULL) *engine = container_engine_name(e);
    if (machine != NULL && rm != NULL) *machine = g_strdup(rm->name != NULL ? rm->name : rm->host);
    return c;
}

void machine_containers_terminal_selected(AppData* app) {
    ContainerEngine engine;
    RemoteMachine* rm = NULL;
    const gchar* machine = NULL;
    Container* c = selected_full(app, &engine, &machine, &rm);
    if (c == NULL) {
        return;
    }
    if (!c->running) {
        gchar* msg = g_strdup_printf(_("%s is stopped: start it first (c)"), c->name);
        app_notice(app, msg, FALSE);
        g_free(msg);
        return;
    }
    if (machine[0] == '\0') {
        gchar** argv = container_shell_argv(engine, c->id);
        terminal_open_command(app, (const gchar* const*)argv, NULL);
        g_strfreev(argv);
        return;
    }
    if (rm == NULL || !rm->is_connected) {
        return;
    }
    /* ssh -t to the machine, docker exec -it there (never through the master
     * connection, as the machine's own terminals) */
    SshTarget t = remote_machine_target(rm);
    GPtrArray* args = ssh_session_argv(&t, SSH_ARGV_TTY | SSH_ARGV_NO_MUX);
    g_ptr_array_add(args, container_shell_command(engine, c->id));
    g_ptr_array_add(args, NULL);
    terminal_open_command(app, (const gchar* const*)args->pdata, NULL);
    g_ptr_array_free(args, TRUE);
}

static void on_action_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    Job* j = g_task_get_task_data(G_TASK(res));
    if (mc.app != NULL) {
        if (!j->ran || j->status != 0) {
            gchar* why = j->ran ? containers_error_text(j->output) : g_strdup(j->error != NULL ? j->error : "");
            /* TRANSLATORS: %1$s is a container name, %2$s what Docker or Podman said */
            gchar* msg = g_strdup_printf(_("%1$s: %2$s"), j->name, why);
            app_notice(mc.app, msg, FALSE);
            g_free(msg);
            g_free(why);
        } else {
            const gchar* format = (j->action == CONTAINER_STOP)      ? C_("container", "%s stopped")
                                  : (j->action == CONTAINER_RESTART) ? _("%s restarted")
                                                                     : _("%s started");
            gchar* msg = g_strdup_printf(format, j->name);
            app_notice(mc.app, msg, FALSE);
            g_free(msg);
        }
    }
    /* List again, the machine's rows as they are until the answer */
    RemoteMachine* rm = job_machine(j);
    EngineState* s = state_get(j->machine, j->engine);
    s->listed = FALSE;
    if (j->machine[0] == '\0' || rm != NULL) {
        maybe_list(j->machine, rm);
    }
}

void machine_containers_action_selected(AppData* app, ContainerAction action) {
    ContainerEngine engine;
    RemoteMachine* rm = NULL;
    const gchar* machine = NULL;
    Container* c = selected_full(app, &engine, &machine, &rm);
    if (c == NULL || (machine[0] != '\0' && (rm == NULL || !rm->is_connected))) {
        return;
    }
    Job* j = job_new(machine, machine[0] != '\0' ? rm : NULL, engine);
    j->name = g_strdup(c->name);
    j->action = action;
    if (j->mount != NULL) {
        j->command = container_action_command(engine, action, c->id);
    } else {
        j->argv = container_action_argv(engine, action, c->id);
    }
    const gchar* format = (action == CONTAINER_STOP)      ? _("Stopping %s…")
                          : (action == CONTAINER_RESTART) ? _("Restarting %s…")
                                                          : _("Starting %s…");
    gchar* msg = g_strdup_printf(format, c->name);
    app_notice(app, msg, FALSE);
    g_free(msg);
    run_job(j, on_action_done);
}

void machine_containers_toggle_selected(AppData* app) {
    ExplorerRow* group = selected_group(app);
    ContainerEngine engine;
    Container* c = selected_full(app, &engine, NULL, NULL);
    if (c != NULL) {
        machine_containers_action_selected(app, c->running ? CONTAINER_STOP : CONTAINER_START);
    } else if (group != NULL && explorer_row_is(tree_nav_selected(app), CONTAINERS_ROW)) {
        explorer_tree_toggle_expanded(app->tree, group);
    }
}

void machine_containers_activate_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    if (explorer_row_is(row, CONTAINERS_ROW)) {
        explorer_tree_toggle_expanded(app->tree, row);
    } else if (explorer_row_is(row, CONTAINER_ROW)) {
        machine_containers_terminal_selected(app);
    }
}
