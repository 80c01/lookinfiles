#include "core/containers.h"

#include <string.h>

/* The shell run in a container: bash when there is one, else sh */
#define CONTAINER_SHELL "command -v bash >/dev/null 2>&1 && exec bash || exec sh"

void container_free(gpointer data) {
    Container* c = data;
    if (c == NULL) {
        return;
    }
    g_free(c->id);
    g_free(c->name);
    g_free(c->image);
    g_free(c->state);
    g_free(c->status);
    g_free(c->created);
    g_free(c->ports);
    g_free(c);
}

const gchar* container_engine_program(ContainerEngine engine) {
    return engine == CONTAINER_ENGINE_PODMAN ? "podman" : "docker";
}

const gchar* container_engine_name(ContainerEngine engine) {
    return engine == CONTAINER_ENGINE_PODMAN ? "Podman" : "Docker";
}

gint container_engine_from_program(const gchar* name) {
    for (gint e = 0; e < CONTAINER_ENGINE_COUNT; e++) {
        if (g_strcmp0(name, container_engine_program((ContainerEngine)e)) == 0) {
            return e;
        }
    }
    return -1;
}

gchar** containers_list_argv(ContainerEngine engine) {
    gchar* argv[] = {(gchar*)container_engine_program(engine), "ps", "-a", "--format", CONTAINERS_FORMAT, NULL};
    return g_strdupv(argv);
}

gchar* containers_list_command(ContainerEngine engine) {
    const gchar* p = container_engine_program(engine);
    gchar* format = g_shell_quote(CONTAINERS_FORMAT);
    gchar* line = g_strdup_printf("command -v %s >/dev/null 2>&1 || exit %d; %s ps -a --format %s 2>&1", p,
                                  CONTAINERS_MISSING, p, format);
    g_free(format);
    return line;
}

static gint compare_containers(gconstpointer pa, gconstpointer pb) {
    const Container* a = *(const Container* const*)pa;
    const Container* b = *(const Container* const*)pb;
    if (a->running != b->running) {
        return a->running ? -1 : 1;
    }
    return g_ascii_strcasecmp(a->name, b->name);
}

GPtrArray* containers_parse(const gchar* output) {
    GPtrArray* out = g_ptr_array_new_with_free_func(container_free);
    if (output == NULL) {
        return out;
    }
    gchar** lines = g_strsplit(output, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = g_strchomp(lines[i]);
        gchar** f = g_strsplit(line, "|", 7);
        if (g_strv_length(f) == 7 && f[0][0] != '\0' && f[1][0] != '\0') {
            Container* c = g_new0(Container, 1);
            c->id = g_strdup(g_strstrip(f[0]));
            c->name = g_strdup(g_strstrip(f[1]));
            c->image = g_strdup(g_strstrip(f[2]));
            c->state = g_ascii_strdown(g_strstrip(f[3]), -1);
            c->status = g_strdup(g_strstrip(f[4]));
            c->created = g_strdup(g_strstrip(f[5]));
            c->ports = g_strdup(g_strstrip(f[6]));
            /* Older engines leave .State empty: "Up ..." in the status */
            c->running = (c->state[0] != '\0') ? strcmp(c->state, "running") == 0
                                               : g_str_has_prefix(c->status, "Up");
            g_ptr_array_add(out, c);
        }
        g_strfreev(f);
    }
    g_strfreev(lines);
    g_ptr_array_sort(out, compare_containers);
    return out;
}

Container* containers_find(GPtrArray* containers, const gchar* id) {
    for (guint i = 0; containers != NULL && id != NULL && i < containers->len; i++) {
        Container* c = g_ptr_array_index(containers, i);
        if (strcmp(c->id, id) == 0) {
            return c;
        }
    }
    return NULL;
}

static const gchar* action_verb(ContainerAction action) {
    switch (action) {
    case CONTAINER_STOP: return "stop";
    case CONTAINER_RESTART: return "restart";
    case CONTAINER_START:
    default: return "start";
    }
}

gchar** container_action_argv(ContainerEngine engine, ContainerAction action, const gchar* id) {
    gchar* argv[] = {(gchar*)container_engine_program(engine), (gchar*)action_verb(action), (gchar*)id, NULL};
    return g_strdupv(argv);
}

gchar* container_action_command(ContainerEngine engine, ContainerAction action, const gchar* id) {
    gchar* q = g_shell_quote(id != NULL ? id : "");
    gchar* line = g_strdup_printf("%s %s %s 2>&1", container_engine_program(engine), action_verb(action), q);
    g_free(q);
    return line;
}

gchar** container_shell_argv(ContainerEngine engine, const gchar* id) {
    gchar* argv[] = {(gchar*)container_engine_program(engine), "exec", "-it", (gchar*)id, "sh", "-c",
                     CONTAINER_SHELL, NULL};
    return g_strdupv(argv);
}

gchar* container_shell_command(ContainerEngine engine, const gchar* id) {
    gchar* q = g_shell_quote(id != NULL ? id : "");
    gchar* shell = g_shell_quote(CONTAINER_SHELL);
    gchar* line = g_strdup_printf("%s exec -it %s sh -c %s", container_engine_program(engine), q, shell);
    g_free(shell);
    g_free(q);
    return line;
}

gchar* containers_error_text(const gchar* output) {
    if (output == NULL) {
        return g_strdup("");
    }
    gchar** lines = g_strsplit(output, "\n", -1);
    gchar* found = NULL;
    for (gint i = 0; lines[i] != NULL && found == NULL; i++) {
        gchar* l = g_strstrip(lines[i]);
        if (l[0] != '\0') {
            found = g_utf8_make_valid(l, -1);
        }
    }
    g_strfreev(lines);
    if (found == NULL) {
        return g_strdup("");
    }
    if (g_utf8_strlen(found, -1) > 200) {
        gchar* cut = g_utf8_substring(found, 0, 200);
        g_free(found);
        found = g_strconcat(cut, "…", NULL);
        g_free(cut);
    }
    return found;
}
