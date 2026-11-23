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

static void on_libssh_log(int priority, const char* function, const char* buffer, void* userdata) {
    (void)userdata;
    gchar* line = g_strdup_printf("libssh(%d) %s: %s", priority, function != NULL ? function : "",
                                  buffer != NULL ? buffer : "");
    write_line(line);
    g_free(line);
}

void ssh_trace_setup(void) {
    static gsize once = 0;
    if (!g_once_init_enter(&once)) return;
    const gchar* path = g_getenv("LOOKINFILES_SSH_LOG");
    if (path != NULL && path[0] != '\0') {
        log_file = g_fopen(path, "a");
        if (log_file != NULL) {
            ssh_set_log_callback(on_libssh_log);
            ssh_set_log_level(SSH_LOG_DEBUG);
            write_line("---- ssh log started");
        }
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
