/* The connection to remote machines: master SSH connections, ssh prompts
 * as dialogs, files over SFTP through the master (see remote_session.h). */
#include "explorer/remote_session.h"
#include "explorer/remote_store.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/ssh_askpass.h"
#ifdef HAVE_LIBSSH
#include "core/ssh_link.h"
#include "core/ssh_relay.h"
#endif
#include "core/ssh_session.h"
#include "core/vfs.h"
#include "widgets/dialog.h"

#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif


/* The master connection of a machine */
typedef struct {
    gchar* id;
    gchar* label;       /* the machine's name, for the messages */
    SshMaster* master;  /* NULL when none */
#ifdef HAVE_LIBSSH
    SshLink* link;      /* NULL when none */
#endif
    gboolean starting;
    gboolean failed;    /* refused or failed: opened again only by the next connect */
    gchar* error;       /* why it failed */
    GPtrArray* waiters; /* GMainLoop* of remote_session_connect() calls waiting for it */
} MasterEntry;

static AppData* session_app;
static GHashTable* masters;    /* machine id -> MasterEntry* */
static GHashTable* connecting; /* ids of the machines being connected */

#ifdef HAVE_LIBSSH
static gboolean use_libssh(void) {
    return ssh_link_enabled();
}
#endif

/* ------------------------------------------------------------------------- */
/* ssh prompts                                                               */
/* ------------------------------------------------------------------------- */

typedef void (*PromptReplyFunc)(gpointer request, const gchar* answer);

typedef struct {
    gpointer request;
    PromptReplyFunc reply;
    gboolean confirm;
    GtkWidget* entry; /* the secret typed, NULL for a question */
} Prompt;

static void on_prompt_response(AppDialog* dlg, gint response, gpointer data) {
    Prompt* p = data;
    if (p->confirm) {
        p->reply(p->request, response == APP_RESPONSE_YES ? "yes" : NULL);
    } else {
        const gchar* secret = response == APP_RESPONSE_OK ? gtk_editable_get_text(GTK_EDITABLE(p->entry)) : NULL;
        p->reply(p->request, secret);
        gtk_editable_set_text(GTK_EDITABLE(p->entry), ""); /* the secret is not kept */
    }
    g_free(p);
    app_dialog_destroy(dlg);
}

/* Shared prompt dialog body for ssh_askpass (on_ask) and ssh_link (on_link_ask) */
static void show_prompt_dialog(AppData* app, gboolean notice, gboolean confirm, const gchar* prompt,
                               gpointer request, PromptReplyFunc reply) {
    gchar* text = g_utf8_make_valid(prompt != NULL ? prompt : "", -1);
    g_strstrip(text);
    if (notice) {
        app_notice(app, text, FALSE);
        g_free(text);
        return;
    }
    if (app == NULL || app->window == NULL) {
        reply(request, NULL);
        g_free(text);
        return;
    }
    GtkWidget* dlg = confirm
                         ? app_dialog_new(_("SSH Confirmation"), app->window, _("_No"), APP_RESPONSE_NO,
                                          _("_Yes"), APP_RESPONSE_YES, NULL)
                         : app_dialog_new(_("SSH Authentication"), app->window, _("_Cancel"),
                                          APP_RESPONSE_CANCEL, _("_OK"), APP_RESPONSE_OK, NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), confirm ? APP_RESPONSE_YES : APP_RESPONSE_OK);
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
    p->reply = reply;
    p->confirm = confirm;
    if (!confirm) {
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

static void reply_askpass(gpointer request, const gchar* answer) {
    ssh_askpass_reply(request, answer);
}

/* A prompt of ssh (in English, as ssh writes it): a dialog over the window */
static void on_ask(SshAskpassRequest* request, SshAskpassKind kind, const gchar* prompt, gpointer data) {
    show_prompt_dialog(data, kind == SSH_ASKPASS_NOTICE, kind == SSH_ASKPASS_CONFIRM, prompt,
                       request, reply_askpass);
}

#ifdef HAVE_LIBSSH
static void reply_link(gpointer request, const gchar* answer) {
    ssh_link_reply(request, answer);
}

static void on_link_ask(SshLinkAsk* ask, SshLinkAskKind kind, const gchar* prompt, gpointer data) {
    show_prompt_dialog(data, kind == SSH_LINK_ASK_NOTICE, kind == SSH_LINK_ASK_CONFIRM, prompt,
                       ask, reply_link);
}
#endif

/* This program's path for SSH_ASKPASS and the SSH channel relay, NULL when
 * unknown. Caller frees. */
static gchar* program_path(const gchar* argv0) {
    gchar* exe = NULL;
#ifdef G_OS_WIN32
    wchar_t wpath[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, wpath, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        exe = g_utf16_to_utf8((const gunichar2*)wpath, n, NULL, NULL, NULL);
    }
#else
    exe = g_file_read_link("/proc/self/exe", NULL);
#endif
    if (exe == NULL && argv0 != NULL) {
        if (g_path_is_absolute(argv0)) {
            exe = g_strdup(argv0);
        } else if (strchr(argv0, G_DIR_SEPARATOR) != NULL) {
            exe = g_canonicalize_filename(argv0, NULL);
        } else {
            exe = g_find_program_in_path(argv0);
        }
    }
    return exe;
}

static void on_master_missing(const SshTarget* target, gpointer data);

void remote_session_init(AppData* app, const gchar* argv0) {
    session_app = app;
    ssh_session_cleanup_stale();
    ssh_session_on_master_missing(on_master_missing, app);
    gchar* exe = program_path(argv0);
#ifdef HAVE_LIBSSH
    ssh_relay_set_program(exe);
#endif
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
#ifdef HAVE_LIBSSH
    ssh_relay_stop();
    ssh_relay_set_program(NULL);
#endif
    ssh_askpass_stop();
}

/* ------------------------------------------------------------------------- */
/* Connecting                                                                */
/* ------------------------------------------------------------------------- */

static void wake_waiters(MasterEntry* e) {
    for (guint i = 0; i < e->waiters->len; i++) g_main_loop_quit(e->waiters->pdata[i]);
    g_ptr_array_set_size(e->waiters, 0);
}

static gboolean entry_is_up(const MasterEntry* e) {
    if (e == NULL) return FALSE;
#ifdef HAVE_LIBSSH
    if (use_libssh()) return ssh_link_is_up(e->link);
#endif
    return ssh_master_is_up(e->master);
}

static void entry_free(gpointer data) {
    MasterEntry* e = data;
    if (e->master != NULL) ssh_master_stop(e->master); /* no ready() after this */
#ifdef HAVE_LIBSSH
    if (e->link != NULL) {
        ssh_link_close(e->link); /* leaves the registry at once; no ready() after this */
        ssh_link_unref(e->link);
    }
#endif
    wake_waiters(e);
    g_ptr_array_unref(e->waiters);
    g_free(e->error);
    g_free(e->label);
    g_free(e->id);
    g_free(e);
}

static void entry_finish_ready(MasterEntry* e, gboolean ok, const gchar* text) {
    e->starting = FALSE;
    if (!ok) {
        e->failed = TRUE; /* not opened again in the background: that would ask again and again */
        g_free(e->error);
        e->error = g_strdup(text != NULL ? text : "");
        if (e->waiters->len == 0 && session_app != NULL) { /* opened in the background */
            gchar* msg = g_strdup_printf(_("Cannot open the SSH connection to %s: %s"), e->label, e->error);
            app_notice(session_app, msg, FALSE);
            g_free(msg);
        }
    }
    wake_waiters(e);
}

static void on_master_ready(SshMaster* m, gboolean ok, const gchar* text, gpointer data) {
    MasterEntry* e = data;
    if (!ok) {
        e->master = NULL;
        ssh_master_stop(m);
    }
    entry_finish_ready(e, ok, text);
}

#ifdef HAVE_LIBSSH
static void maybe_stop_relay(void) {
    if (masters != NULL) {
        GHashTableIter it;
        gpointer value;
        g_hash_table_iter_init(&it, masters);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            MasterEntry* e = value;
            if (e->link != NULL) return;
        }
    }
    ssh_relay_stop();
}

static void on_link_ready(SshLink* link, gboolean ok, const gchar* text, gpointer data) {
    MasterEntry* e = data;
    if (!ok) {
        e->link = NULL;
        ssh_link_close(link);
        ssh_link_unref(link);
        maybe_stop_relay();
    }
    entry_finish_ready(e, ok, text);
}
#endif

static const gchar* machine_name(const RemoteMachine* rm) {
    return (rm->name != NULL && rm->name[0] != '\0') ? rm->name : rm->host;
}

/* The master connection (or libssh link) of rm, being opened unless it is
 * open (or, when not explicit, unless its last opening failed) */
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
    if (e->starting || entry_is_up(e) || (e->failed && !explicit)) return e;
    e->failed = FALSE;
    g_clear_pointer(&e->error, g_free);
    e->starting = TRUE;
    SshTarget t = remote_machine_target(rm);
#ifdef HAVE_LIBSSH
    if (use_libssh()) {
        if (e->link != NULL) {
            ssh_link_close(e->link); /* it dropped */
            ssh_link_unref(e->link);
        }
        GError* rerr = NULL;
        if (!ssh_relay_serve(&rerr)) {
            g_debug("ssh relay not available: %s", rerr != NULL ? rerr->message : "");
            g_clear_error(&rerr);
        }
        e->link = ssh_link_connect(&t, on_link_ask, session_app, on_link_ready, e);
        return e;
    }
#endif
    if (e->master != NULL) ssh_master_stop(e->master); /* it dropped */
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
    /* Without multiplexing or libssh there is no shared connection: the files'
     * session opens again by itself (vfs.h) */
    if (rm != NULL && rm->id != NULL && ssh_session_shares_connection()) open_master(rm, TRUE);
}

/* Without multiplexing: the SFTP session opened by a worker (it logs in,
 * the password dialog shown by the main loop, which runs meanwhile) */
typedef struct {
    gchar* path;
    gchar* error; /* NULL: connected */
    gboolean done;
    GMainLoop* loop;
} Probe;

static void probe_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)task;
    (void)source;
    (void)cancel;
    Probe* p = data;
    VfsEntry e;
    GError* err = NULL;
    if (!vfs_stat(p->path, &e, NULL, &err)) {
        /* The server's answer about the folder (missing...) still says it is connected */
        if (vfs_error_is_connection(err)) p->error = g_strdup(err->message);
        g_error_free(err);
    }
}

static void on_probe_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)res;
    Probe* p = data;
    p->done = TRUE;
    g_main_loop_quit(p->loop);
}

/* The files of rm at mount_dir over a session of their own; TRUE when it
 * opened, else FALSE with *error_text */
static gboolean connect_files(RemoteMachine* rm, const gchar* mount_dir, gchar** error_text) {
    SshTarget t = remote_machine_target(rm);
    vfs_mount(mount_dir, &t, rm->remote_path);
    Probe p = { .path = g_strdup(mount_dir), .loop = g_main_loop_new(NULL, FALSE) };
    GTask* task = g_task_new(NULL, NULL, on_probe_done, &p);
    g_task_set_task_data(task, &p, NULL);
    g_task_run_in_thread(task, probe_thread);
    g_object_unref(task);
    while (!p.done) g_main_loop_run(p.loop); /* the vfs call is bounded */
    g_main_loop_unref(p.loop);
    g_free(p.path);
    if (p.error == NULL) return TRUE;
    vfs_unmount(mount_dir);
    *error_text = p.error;
    return FALSE;
}

gboolean remote_session_connect(AppData* app, RemoteMachine* rm, const gchar* mount_dir, gchar** error_text) {
    *error_text = NULL;
    if (rm == NULL || rm->id == NULL) return FALSE;
    if (connecting == NULL) connecting = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (g_hash_table_contains(connecting, rm->id)) return FALSE; /* Enter pressed again meanwhile */
    gchar* id = g_strdup(rm->id); /* rm may be edited while waiting */
    g_hash_table_add(connecting, g_strdup(id));
    gchar* notice = g_strdup_printf(_("Connecting to %s…"), (rm->name != NULL && rm->name[0] != '\0') ? rm->name : rm->host);
    app_notice(app, notice, FALSE);
    g_free(notice);

    gboolean ok;
    if (ssh_session_shares_connection()) {
        MasterEntry* e = open_master(rm, TRUE);
        if (e->starting) {
            GMainLoop* loop = g_main_loop_new(NULL, FALSE);
            g_ptr_array_add(e->waiters, loop);
            g_main_loop_run(loop);
            g_main_loop_unref(loop);
        }
        e = masters != NULL ? g_hash_table_lookup(masters, id) : NULL; /* gone if the application quit */
        ok = entry_is_up(e);
        if (!ok) *error_text = g_strdup(e != NULL && e->error != NULL ? e->error : "");
    } else {
        /* No master to share (Windows): a check of its own would ask for the
         * password once more, and the main thread must never wait for a
         * login (vfs.h), so the files' session is the connection */
        ok = connect_files(rm, mount_dir, error_text);
    }
    if (ok && remote_find_by_id(app, id) == NULL) ok = FALSE; /* deleted while waiting */
    app_notice(app, NULL, FALSE);
    g_hash_table_remove(connecting, id);
    g_free(id);
    return ok;
}

void remote_session_close(RemoteMachine* rm) {
    const gchar* local;
    for (guint i = 0; remote_machine_mount_nth(rm, i, &local, NULL); i++) vfs_unmount(local);
    if (masters != NULL && rm != NULL && rm->id != NULL) g_hash_table_remove(masters, rm->id);
#ifdef HAVE_LIBSSH
    maybe_stop_relay();
#endif
}

void remote_session_files_open(RemoteMachine* rm) {
    if (rm == NULL || rm->mount_point == NULL || rm->host == NULL) return;
    SshTarget t = remote_machine_target(rm);
    const gchar *local, *root;
    for (guint i = 0; remote_machine_mount_nth(rm, i, &local, &root); i++) vfs_mount(local, &t, root);
}
