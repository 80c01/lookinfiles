/* Debug log of the libssh links (see ssh_trace.h). */
#include "core/ssh_trace.h"

#include <glib/gstdio.h>
#include <libssh/callbacks.h> /* ssh_set_log_callback() */
#include <libssh/libssh.h>
#include <stdio.h>

static GMutex log_mu;
static FILE* log_file;

static void write_line(const gchar* text) {
    g_mutex_lock(&log_mu);
    if (log_file != NULL) {
        GDateTime* now = g_date_time_new_now_local();
        gchar* stamp = g_date_time_format(now, "%H:%M:%S.%f");
        fprintf(log_file, "%s [%p] %s\n", stamp, (void*)g_thread_self(), text);
        fflush(log_file);
        g_free(stamp);
        g_date_time_unref(now);
    }
    g_mutex_unlock(&log_mu);
}

/* libssh repeats the same message on every call of a busy loop: one line,
 * then how many times it came again, written when another message comes */
static GPrivate last_message = G_PRIVATE_INIT(g_free);
static GPrivate repeats; /* GUINT_TO_POINTER(count) */

static void on_libssh_log(int priority, const char* function, const char* buffer, void* userdata) {
    (void)userdata;
    gchar* line = g_strdup_printf("libssh(%d) %s: %s", priority, function != NULL ? function : "",
                                  buffer != NULL ? buffer : "");
    if (g_strcmp0(line, g_private_get(&last_message)) == 0) {
        g_private_set(&repeats, GUINT_TO_POINTER(GPOINTER_TO_UINT(g_private_get(&repeats)) + 1));
        g_free(line);
        return;
    }
    guint count = GPOINTER_TO_UINT(g_private_get(&repeats));
    if (count > 0) {
        gchar* note = g_strdup_printf("libssh: (previous message %u more times)", count);
        write_line(note);
        g_free(note);
        g_private_set(&repeats, NULL);
    }
    write_line(line);
    g_private_replace(&last_message, line);
}

void ssh_trace_setup(void) {
    static gsize once = 0;
    if (!g_once_init_enter(&once)) return;
    const gchar* path = g_getenv("LOOKINFILES_SSH_LOG");
    if (path != NULL && path[0] != '\0') {
        log_file = g_fopen(path, "a");
        if (log_file != NULL) write_line("---- ssh log started");
    }
    g_once_init_leave(&once, 1);
}

gboolean ssh_trace_enabled(void) {
    return log_file != NULL;
}

void ssh_trace(const gchar* format, ...) {
    if (log_file == NULL) return;
    va_list args;
    va_start(args, format);
    gchar* text = g_strdup_vprintf(format, args);
    va_end(args);
    write_line(text);
    g_free(text);
}

void ssh_trace_thread(void) {
    if (log_file == NULL) return;
    ssh_set_log_callback(on_libssh_log);
    ssh_set_log_level(SSH_LOG_DEBUG);
}

void ssh_trace_session(ssh_session session) {
    if (log_file == NULL || session == NULL) return;
    char *host = NULL, *user = NULL, *proxy = NULL;
    unsigned int port = 0;
    ssh_options_get(session, SSH_OPTIONS_HOST, &host);
    ssh_options_get(session, SSH_OPTIONS_USER, &user);
    ssh_options_get(session, SSH_OPTIONS_PROXYCOMMAND, &proxy);
    ssh_options_get_port(session, &port);
    ssh_trace("connect %s@%s port %u proxy=%s (libssh %s)", user != NULL ? user : "?", host != NULL ? host : "?",
              port, proxy != NULL ? proxy : "none", ssh_version(0));
    ssh_string_free_char(host);
    ssh_string_free_char(user);
    ssh_string_free_char(proxy);
}

gboolean ssh_trace_sample(guint* counter) {
    if (log_file == NULL) return FALSE;
    guint n = (guint)g_atomic_int_add((gint*)counter, 1) + 1;
    return n <= 300 || n % 1000 == 0;
}
