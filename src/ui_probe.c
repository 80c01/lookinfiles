#include "ui_probe.h"

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <signal.h>

static gchar* probe_path = NULL;
static guint probe_count = 0;
static guint probe_idle = 0;

static gboolean on_probe_idle(gpointer data) {
    (void)data;
    probe_idle = 0;
    gchar text[16];
    g_snprintf(text, sizeof(text), "%u\n", ++probe_count);
    g_file_set_contents(probe_path, text, -1, NULL);
    return G_SOURCE_REMOVE;
}

static gboolean on_probe_signal(gpointer data) {
    (void)data;
    if (probe_idle == 0) {
        probe_idle = g_idle_add_full(G_PRIORITY_LOW, on_probe_idle, NULL, NULL);
    }
    return G_SOURCE_CONTINUE;
}

void ui_probe_install_file(const gchar* path) {
    if (path == NULL || path[0] == '\0' || probe_path != NULL) {
        return;
    }
    probe_path = g_strdup(path);
    g_unix_signal_add(SIGUSR1, on_probe_signal, NULL);
}
#else
void ui_probe_install_file(const gchar* path) {
    (void)path;
}
#endif

void ui_probe_install(void) {
    ui_probe_install_file(g_getenv("LOOKIN_UI_PROBE"));
}
