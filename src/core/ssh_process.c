/* A remote command as a subprocess or a libssh channel (see ssh_process.h). */
#include "core/ssh_process.h"
#ifdef HAVE_LIBSSH
#include "core/ssh_channel.h"
#endif

struct SshProcess {
    GSubprocess* proc;  /* or */
    GIOStream* channel; /* an SshChannel */
    GThread* drain;     /* reads the channel's stderr */
    gboolean ended;     /* waited for, or stopped */
};

SshProcess* ssh_process_new_subprocess(GSubprocess* proc) {
    SshProcess* p = g_new0(SshProcess, 1);
    p->proc = g_object_ref(proc);
    return p;
}

#ifdef HAVE_LIBSSH
/* Unread stderr would fill the channel's window and stall stdout too */
static gpointer drain_stderr(gpointer data) {
    GInputStream* err = data;
    gchar buf[4096];
    while (g_input_stream_read(err, buf, sizeof buf, NULL, NULL) > 0) {
    }
    g_object_unref(err);
    return NULL;
}
#endif

SshProcess* ssh_process_new_channel(GIOStream* channel) {
#ifdef HAVE_LIBSSH
    SshProcess* p = g_new0(SshProcess, 1);
    p->channel = g_object_ref(channel);
    GInputStream* err = g_object_ref(ssh_channel_get_stderr(SSH_CHANNEL(channel)));
    p->drain = g_thread_new("ssh-stderr", drain_stderr, err);
    return p;
#else
    (void)channel;
    return NULL;
#endif
}

GOutputStream* ssh_process_get_stdin(SshProcess* p) {
    return p->proc != NULL ? g_subprocess_get_stdin_pipe(p->proc) : g_io_stream_get_output_stream(p->channel);
}

GInputStream* ssh_process_get_stdout(SshProcess* p) {
    return p->proc != NULL ? g_subprocess_get_stdout_pipe(p->proc) : g_io_stream_get_input_stream(p->channel);
}

void ssh_process_force_exit(SshProcess* p) {
    p->ended = TRUE;
    if (p->proc != NULL) {
        g_subprocess_force_exit(p->proc);
        return;
    }
#ifdef HAVE_LIBSSH
    ssh_channel_force_close(SSH_CHANNEL(p->channel));
#endif
}

gint ssh_process_wait(SshProcess* p) {
    if (p->proc != NULL) {
        g_subprocess_wait(p->proc, NULL, NULL);
        p->ended = TRUE;
        return g_subprocess_get_if_exited(p->proc) ? g_subprocess_get_exit_status(p->proc) : -1;
    }
    gint status = -1;
#ifdef HAVE_LIBSSH
    if (!ssh_channel_wait_exit_sync(SSH_CHANNEL(p->channel), NULL, &status, NULL)) status = -1;
#endif
    p->ended = TRUE;
    return status;
}

void ssh_process_free(SshProcess* p) {
    if (p == NULL) return;
    if (!p->ended) ssh_process_force_exit(p);
    if (p->proc != NULL) {
        g_object_unref(p->proc);
    } else {
#ifdef HAVE_LIBSSH
        ssh_channel_force_close(SSH_CHANNEL(p->channel)); /* the drain's read ends */
#endif
        if (p->drain != NULL) g_thread_join(p->drain);
        g_object_unref(p->channel);
    }
    g_free(p);
}
