/* The connection to remote machines: master SSH connections, ssh prompts
 * as dialogs, sshfs mounts through the master (see remote_session.h). */
#include "explorer/remote_session.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/ssh_askpass.h"
#include "core/ssh_session.h"
#include "core/vfs.h"
#include "widgets/dialog.h"

#include <string.h>

/* The master connection of a machine */
typedef struct {
    gchar* id;
    gchar* label;       /* the machine's name, for the messages */
    SshMaster* master;  /* NULL when none */
    gboolean starting;
    gboolean failed;    /* refused or failed: opened again only by the next connect */
    gchar* error;       /* why it failed */
    GPtrArray* waiters; /* GMainLoop* of remote_session_mount() calls waiting for it */
} MasterEntry;

static AppData* session_app;
static GHashTable* masters;    /* machine id -> MasterEntry* */
static GHashTable* connecting; /* ids of the machines being connected */
/* The secret typed for the master connection while connecting, given once
 * more to sshfs asking the same question (its connection is its own) */
static gchar* reuse_prompt;
static gchar* reuse_secret;

static void forget_secret(void) {
    if (reuse_secret != NULL) memset(reuse_secret, 0, strlen(reuse_secret));
    g_clear_pointer(&reuse_secret, g_free);
    g_clear_pointer(&reuse_prompt, g_free);
}

/* ------------------------------------------------------------------------- */
/* ssh prompts                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    SshAskpassRequest* request;
    SshAskpassKind kind;
    GtkWidget* entry; /* the secret typed, NULL for a question */
    gchar* prompt;
} Prompt;

static void on_prompt_response(AppDialog* dlg, gint response, gpointer data) {
    Prompt* p = data;
    if (p->kind == SSH_ASKPASS_CONFIRM) {
        ssh_askpass_reply(p->request, response == APP_RESPONSE_YES ? "yes" : NULL);
    } else {
        const gchar* secret = response == APP_RESPONSE_OK ? gtk_editable_get_text(GTK_EDITABLE(p->entry)) : NULL;
        if (secret != NULL && connecting != NULL && g_hash_table_size(connecting) > 0) {
            forget_secret();
            reuse_prompt = g_strdup(p->prompt);
            reuse_secret = g_strdup(secret);
        }
        ssh_askpass_reply(p->request, secret);
        gtk_editable_set_text(GTK_EDITABLE(p->entry), ""); /* the secret is not kept */
    }
    g_free(p->prompt);
    g_free(p);
    app_dialog_destroy(dlg);
}

/* A prompt of ssh (in English, as ssh writes it): a dialog over the window */
static void on_ask(SshAskpassRequest* request, SshAskpassKind kind, const gchar* prompt, gpointer data) {
    AppData* app = data;
    gchar* text = g_utf8_make_valid(prompt != NULL ? prompt : "", -1);
    g_strstrip(text);
    if (kind == SSH_ASKPASS_NOTICE) {
        app_notice(app, text, FALSE);
        g_free(text);
        return;
    }
    if (kind == SSH_ASKPASS_SECRET && reuse_secret != NULL && g_strcmp0(text, reuse_prompt) == 0) {
        ssh_askpass_reply(request, reuse_secret); /* sshfs, just after the master */
        forget_secret();
        g_free(text);
        return;
    }
    if (app == NULL || app->window == NULL) {
        ssh_askpass_reply(request, NULL);
        g_free(text);
        return;
    }
    gboolean question = kind == SSH_ASKPASS_CONFIRM;
    GtkWidget* dlg = question
                         ? app_dialog_new(_("SSH Confirmation"), app->window, _("_No"), APP_RESPONSE_NO,
                                          _("_Yes"), APP_RESPONSE_YES, NULL)
                         : app_dialog_new(_("SSH Authentication"), app->window, _("_Cancel"),
                                          APP_RESPONSE_CANCEL, _("_OK"), APP_RESPONSE_OK, NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), question ? APP_RESPONSE_YES : APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 440);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE); /* a host key fingerprint can be copied */
    gtk_box_append(GTK_BOX(box), label);

    Prompt* p = g_new0(Prompt, 1);
    p->request = request;
    p->kind = kind;
    p->prompt = g_strdup(text);
    if (!question) {
        p->entry = gtk_password_entry_new();
        gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(p->entry), TRUE);
        g_object_set(p->entry, "activates-default", TRUE, NULL);
        gtk_box_append(GTK_BOX(box), p->entry);
    }
    g_signal_connect(dlg, "response", G_CALLBACK(on_prompt_response), p);
    app_dialog_present(APP_DIALOG(dlg));
    if (p->entry != NULL) gtk_widget_grab_focus(p->entry);
    g_free(text);
}

/* This program's path for SSH_ASKPASS, NULL when unknown. Caller frees. */
static gchar* program_path(const gchar* argv0) {
    gchar* exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe == NULL && argv0 != NULL) {
        exe = g_path_is_absolute(argv0) ? g_strdup(argv0) : g_find_program_in_path(argv0);
    }
    return exe;
}

static void on_master_missing(const SshTarget* target, gpointer data);

void remote_session_init(AppData* app, const gchar* argv0) {
    session_app = app;
    ssh_session_cleanup_stale();
    ssh_session_on_master_missing(on_master_missing, app);
    gchar* exe = program_path(argv0);
    GError* error = NULL;
    if (exe == NULL || !ssh_askpass_serve(exe, on_ask, app, &error)) {
        /* ssh can still use keys and the agent; a password just cannot be asked */
        g_debug("ssh prompts not available: %s", error != NULL ? error->message : "program path unknown");
        g_clear_error(&error);
    }
    g_free(exe);
}

void remote_session_shutdown(void) {
    vfs_shutdown(); /* its SFTP sessions went through the masters */
    ssh_session_on_master_missing(NULL, NULL);
    g_clear_pointer(&masters, g_hash_table_destroy);
    g_clear_pointer(&connecting, g_hash_table_destroy);
    forget_secret();
    ssh_askpass_stop();
}

/* ------------------------------------------------------------------------- */
/* Connecting and mounting                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    GMainLoop* loop;
    gboolean ok;
    gchar* text;
} Wait;

static void wake_waiters(MasterEntry* e) {
    for (guint i = 0; i < e->waiters->len; i++) g_main_loop_quit(e->waiters->pdata[i]);
    g_ptr_array_set_size(e->waiters, 0);
}

static void entry_free(gpointer data) {
    MasterEntry* e = data;
    if (e->master != NULL) ssh_master_stop(e->master); /* no ready() after this */
    wake_waiters(e);
    g_ptr_array_unref(e->waiters);
    g_free(e->error);
    g_free(e->label);
    g_free(e->id);
    g_free(e);
}

static void on_master_ready(SshMaster* m, gboolean ok, const gchar* text, gpointer data) {
    MasterEntry* e = data;
    e->starting = FALSE;
    if (!ok) {
        e->failed = TRUE; /* not opened again in the background: that would ask again and again */
        g_free(e->error);
        e->error = g_strdup(text != NULL ? text : "");
        e->master = NULL;
        ssh_master_stop(m);
        if (e->waiters->len == 0 && session_app != NULL) { /* opened in the background */
            gchar* msg = g_strdup_printf(_("Cannot open the SSH connection to %s: %s"), e->label, e->error);
            app_notice(session_app, msg, FALSE);
            g_free(msg);
        }
    }
    wake_waiters(e);
}

static const gchar* machine_name(const RemoteMachine* rm) {
    return (rm->name != NULL && rm->name[0] != '\0') ? rm->name : rm->host;
}

/* The master connection of rm, being opened unless it is open (or, when not
 * explicit, unless its last opening failed) */
static MasterEntry* open_master(RemoteMachine* rm, gboolean explicit) {
    if (masters == NULL) masters = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, entry_free);
    MasterEntry* e = g_hash_table_lookup(masters, rm->id);
    if (e == NULL) {
        e = g_new0(MasterEntry, 1);
        e->id = g_strdup(rm->id);
        e->waiters = g_ptr_array_new();
        g_hash_table_insert(masters, e->id, e);
    }
    g_free(e->label);
    e->label = g_strdup(machine_name(rm));
    if (e->starting || ssh_master_is_up(e->master) || (e->failed && !explicit)) return e;
    if (e->master != NULL) ssh_master_stop(e->master); /* it dropped */
    e->failed = FALSE;
    g_clear_pointer(&e->error, g_free);
    e->starting = TRUE;
    SshTarget t = remote_machine_target(rm);
    e->master = ssh_master_start(&t, on_master_ready, e); /* ready() comes from the main loop */
    return e;
}

/* A background command (kernel, System Monitor) found no master: open it
 * once for the connected machine it is for */
static void on_master_missing(const SshTarget* t, gpointer data) {
    AppData* app = data;
    for (GList* l = remote_machine_list(app); l != NULL; l = l->next) {
        RemoteMachine* rm = l->data;
        SshTarget mt = remote_machine_target(rm);
        if (!rm->is_connected || g_strcmp0(mt.host, t->host) != 0 || mt.port != t->port ||
            g_strcmp0(mt.user != NULL ? mt.user : "", t->user != NULL ? t->user : "") != 0) {
            continue;
        }
        open_master(rm, FALSE);
        return;
    }
}

void remote_session_open_master(RemoteMachine* rm) {
    if (rm != NULL && rm->id != NULL) open_master(rm, TRUE);
}

static void on_sshfs_done(GObject* source, GAsyncResult* res, gpointer data) {
    Wait* w = data;
    GBytes* err = NULL;
    GError* error = NULL;
    g_subprocess_communicate_finish(G_SUBPROCESS(source), res, NULL, &err, &error);
    GSubprocess* p = G_SUBPROCESS(source);
    w->ok = error == NULL && g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
    gsize n = 0;
    const gchar* bytes = err != NULL ? g_bytes_get_data(err, &n) : NULL;
    if (!w->ok) {
        w->text = n > 0 ? g_utf8_make_valid(bytes, (gssize)n)
                        : g_strdup(error != NULL ? error->message : _("sshfs process exited with an error."));
        g_strstrip(w->text);
    }
    if (err != NULL) g_bytes_unref(err);
    g_clear_error(&error);
    g_main_loop_quit(w->loop);
}

/* sshfs user@host:dir mount_dir, with a connection of its own: the
 * interface waits on the mount without a timeout (file reads on the main
 * thread), so it must never wait behind a master connection that a terminal
 * or a kernel session could stall */
static gboolean run_sshfs(const RemoteMachine* rm, const SshTarget* t, const gchar* mount_dir, gchar** error_text) {
    GPtrArray* argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup("sshfs"));
    gchar* dest = ssh_session_destination(t);
    gboolean home = rm->remote_path == NULL || rm->remote_path[0] == '\0' || g_strcmp0(rm->remote_path, "~") == 0;
    g_ptr_array_add(argv, g_strconcat(dest, ":", home ? "" : rm->remote_path, NULL));
    g_free(dest);
    g_ptr_array_add(argv, g_strdup(mount_dir));
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("reconnect,follow_symlinks,idmap=user"));
    GPtrArray* opts = ssh_session_options(t, SSH_ARGV_NO_MUX); /* passed on to its ssh */
    for (guint i = 0; i < opts->len; i++) {
        g_ptr_array_add(argv, g_strdup("-o"));
        g_ptr_array_add(argv, g_strdup(opts->pdata[i]));
    }
    g_ptr_array_unref(opts);
    g_ptr_array_add(argv, NULL);

    GSubprocessLauncher* l = ssh_session_launcher(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                                  G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GError* error = NULL;
    GSubprocess* p = g_subprocess_launcher_spawnv(l, (const gchar* const*)argv->pdata, &error);
    g_object_unref(l);
    g_ptr_array_unref(argv);
    if (p == NULL) {
        *error_text = g_strdup(error->message);
        g_error_free(error);
        return FALSE;
    }
    Wait w = { g_main_loop_new(NULL, FALSE), FALSE, NULL };
    GBytes* none = g_bytes_new(NULL, 0);
    g_subprocess_communicate_async(p, none, NULL, on_sshfs_done, &w);
    g_bytes_unref(none);
    g_main_loop_run(w.loop); /* sshfs returns once mounted (it goes on in the background) */
    g_main_loop_unref(w.loop);
    g_object_unref(p);
    *error_text = w.text;
    return w.ok;
}

gboolean remote_session_mount(AppData* app, RemoteMachine* rm, const gchar* mount_dir, gchar** error_text) {
    *error_text = NULL;
    if (rm == NULL || rm->id == NULL) return FALSE;
    if (connecting == NULL) connecting = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (g_hash_table_contains(connecting, rm->id)) return FALSE; /* Enter pressed again meanwhile */
    gchar* id = g_strdup(rm->id); /* rm may be edited while waiting */
    g_hash_table_add(connecting, g_strdup(id));
    SshTarget t = remote_machine_target(rm);
    gchar* notice = g_strdup_printf(_("Connecting to %s…"), (rm->name != NULL && rm->name[0] != '\0') ? rm->name : rm->host);
    app_notice(app, notice, FALSE);
    g_free(notice);

    MasterEntry* e = open_master(rm, TRUE);
    if (e->starting) {
        GMainLoop* loop = g_main_loop_new(NULL, FALSE);
        g_ptr_array_add(e->waiters, loop);
        g_main_loop_run(loop);
        g_main_loop_unref(loop);
    }
    e = masters != NULL ? g_hash_table_lookup(masters, id) : NULL; /* gone if the application quit */
    gboolean ok = e != NULL && ssh_master_is_up(e->master);
    if (!ok) *error_text = g_strdup(e != NULL && e->error != NULL ? e->error : "");
    rm = ok ? remote_find_by_id(app, id) : NULL; /* deleted while waiting? */
    if (rm != NULL) {
        t = remote_machine_target(rm);
        ok = run_sshfs(rm, &t, mount_dir, error_text);
    } else {
        ok = FALSE;
    }
    forget_secret();
    app_notice(app, NULL, FALSE);
    g_hash_table_remove(connecting, id);
    g_free(id);
    return ok;
}

void remote_session_close(RemoteMachine* rm) {
    if (rm != NULL && rm->mount_point != NULL) vfs_unmount(rm->mount_point);
    if (masters != NULL && rm != NULL && rm->id != NULL) g_hash_table_remove(masters, rm->id);
}

void remote_session_files_open(RemoteMachine* rm) {
    if (rm == NULL || rm->mount_point == NULL || rm->host == NULL) return;
    SshTarget t = remote_machine_target(rm);
    vfs_mount(rm->mount_point, &t, rm->remote_path);
}
