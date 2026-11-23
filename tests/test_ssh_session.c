/* Unit tests for src/core/ssh_session.c: ssh command lines, the master
 * connection and remote commands, run with a fake ssh (a shell script). */
#include "core/ssh_session.h"

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static gchar* tmp_dir;

/* The fake ssh: "-N" creates the ControlPath socket and waits for
 * SIGTERM; otherwise prints "<destination>|<command>|$FOO|" then its input.
 * Host "fail" fails as ssh does. */
static const gchar* FAKE_SSH =
    "#!/bin/sh\n"
    "sock=''; master=0; dest=''; cmd=''\n"
    "while [ $# -gt 0 ]; do\n"
    "  case \"$1\" in\n"
    "    -o) case \"$2\" in ControlPath=*) sock=\"${2#ControlPath=}\";; esac; shift 2; continue;;\n"
    "    -N) master=1;;\n"
    "    -M) echo 'ControlMaster=yes and -M make it ask' >&2; exit 255;;\n"
    "    -t) ;;\n"
    "    *) if [ -z \"$dest\" ]; then dest=\"$1\"; else cmd=\"$1\"; fi;;\n"
    "  esac\n"
    "  shift\n"
    "done\n"
    "case \"$dest\" in *fail) echo 'Permission denied (publickey).' >&2; exit 255;; esac\n"
    "if [ $master = 1 ]; then\n"
    "  trap 'rm -f \"$sock\"; exit 0' TERM\n"
    "  : > \"$sock\"\n"
    "  sleep 30 & wait\n"
    "  exit 0\n"
    "fi\n"
    "printf '%s|%s|%s|' \"$dest\" \"$cmd\" \"$FOO\"\n"
    "cat\n";

static gboolean has_arg(GPtrArray* argv, const gchar* arg) {
    for (guint i = 0; i < argv->len; i++) {
        if (g_strcmp0(argv->pdata[i], arg) == 0) return TRUE;
    }
    return FALSE;
}

static gboolean has_prefix_arg(GPtrArray* argv, const gchar* prefix) {
    for (guint i = 0; i < argv->len; i++) {
        if (argv->pdata[i] != NULL && g_str_has_prefix(argv->pdata[i], prefix)) return TRUE;
    }
    return FALSE;
}

static void test_argv(void) {
    SshTarget t = { "srv", "me", 2222, "/keys/id rsa" };
    gchar* dest = ssh_session_destination(&t);
    g_assert_cmpstr(dest, ==, "me@srv");
    g_free(dest);
    SshTarget bare = { "srv", "", 0, NULL };
    dest = ssh_session_destination(&bare);
    g_assert_cmpstr(dest, ==, "srv");
    g_free(dest);

    GPtrArray* argv = ssh_session_argv(&t, 0);
    g_assert_cmpstr(argv->pdata[argv->len - 1], ==, "me@srv");
    g_assert_true(has_arg(argv, "Port=2222"));
    g_assert_true(has_arg(argv, "IdentityFile=\"/keys/id rsa\""));
    g_assert_true(has_arg(argv, "StrictHostKeyChecking=accept-new"));
    g_assert_true(has_prefix_arg(argv, "ControlPath="));
    g_assert_true(has_arg(argv, "ControlMaster=no"));
    g_assert_true(has_arg(argv, "BatchMode=yes"));
    g_assert_false(has_arg(argv, "-t"));
    g_ptr_array_unref(argv);

    argv = ssh_session_argv(&bare, SSH_ARGV_TTY | SSH_ARGV_NO_MUX);
    g_assert_true(has_arg(argv, "-t"));
    g_assert_false(has_prefix_arg(argv, "Port="));
    g_assert_false(has_prefix_arg(argv, "IdentityFile="));
    g_assert_false(has_prefix_arg(argv, "ControlPath="));
    g_ptr_array_unref(argv);

    argv = ssh_session_argv(&t, SSH_ARGV_MASTER);
    g_assert_false(has_arg(argv, "BatchMode=yes")); /* the master may ask */
    g_assert_false(has_arg(argv, "-M")); /* -M after ControlMaster=yes means "ask" */
    g_assert_true(has_arg(argv, "-N"));
    g_assert_true(has_arg(argv, "ControlMaster=yes"));
    g_ptr_array_unref(argv);

    gchar* q = ssh_session_quote("it's");
    g_assert_cmpstr(q, ==, "'it'\\''s'");
    g_free(q);
}

static guint missing_calls;
static gchar* missing_host;

static void on_missing(const SshTarget* t, gpointer data) {
    (void)data;
    missing_calls++;
    g_free(missing_host);
    missing_host = g_strdup(t->host);
}

/* A command through a master that is not open asks the application for it;
 * the master itself, sshfs and terminals (NO_MUX) do not */
static void test_master_missing(void) {
    ssh_session_on_master_missing(on_missing, NULL);
    SshTarget t = { "lonely", NULL, 0, NULL };
    missing_calls = 0;
    g_ptr_array_unref(ssh_session_argv(&t, 0));
    g_assert_cmpuint(missing_calls, ==, 1);
    g_assert_cmpstr(missing_host, ==, "lonely");
    g_ptr_array_unref(ssh_session_argv(&t, SSH_ARGV_MASTER));
    g_ptr_array_unref(ssh_session_argv(&t, SSH_ARGV_NO_MUX | SSH_ARGV_TTY));
    g_assert_cmpuint(missing_calls, ==, 1);
    gchar* sock = ssh_session_control_path(&t);
    g_assert_true(g_file_set_contents(sock, "", 0, NULL)); /* open now */
    g_ptr_array_unref(ssh_session_argv(&t, 0));
    g_assert_cmpuint(missing_calls, ==, 1);
    g_remove(sock);
    g_free(sock);
    ssh_session_on_master_missing(NULL, NULL);
    g_clear_pointer(&missing_host, g_free);
}

static void test_control_path(void) {
    g_assert_true(ssh_session_can_multiplex());
    SshTarget a = { "srv", "me", 0, NULL };
    SshTarget b = { "srv", "me", 22, NULL };
    SshTarget c = { "srv", "me", 2222, NULL };
    gchar* pa = ssh_session_control_path(&a);
    gchar* pb = ssh_session_control_path(&b);
    gchar* pc = ssh_session_control_path(&c);
    g_assert_cmpstr(pa, ==, pb); /* 0 is port 22 */
    g_assert_cmpstr(pa, !=, pc);
    g_assert_true(g_str_has_prefix(pa, tmp_dir));
    gchar* base = g_path_get_basename(pa);
    gchar* pid = g_strdup_printf("%ld-", (long)getpid());
    g_assert_true(g_str_has_prefix(base, pid));
    g_assert_cmpuint(strlen(pa), <, 100);

    /* A socket of a process gone is removed, ours stays */
    gchar* dir = g_path_get_dirname(pa);
    gchar* stale = g_build_filename(dir, "2147483600-0123456789abcdef", NULL);
    g_assert_true(g_file_set_contents(stale, "", 0, NULL));
    g_assert_true(g_file_set_contents(pa, "", 0, NULL));
    g_assert_cmpuint(ssh_session_cleanup_stale(), ==, 1);
    g_assert_false(g_file_test(stale, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(pa, G_FILE_TEST_EXISTS));
    g_remove(pa);
    g_free(stale);
    g_free(dir);
    g_free(pid);
    g_free(base);
    g_free(pa);
    g_free(pb);
    g_free(pc);
}

typedef struct {
    GMainLoop* loop;
    gint status;
    gchar* out;
    gchar* err;
    gboolean ok;
    gchar* text;
    guint calls;
} Result;

static void on_exec(gint status, const gchar* out, const gchar* err, const GError* error, gpointer data) {
    (void)error;
    Result* r = data;
    r->status = status;
    r->out = g_strdup(out);
    r->err = g_strdup(err);
    g_main_loop_quit(r->loop);
}

static void test_exec(void) {
    ssh_session_set_env("FOO", "bar");
    SshTarget t = { "srv", "me", 0, NULL };
    Result r = { g_main_loop_new(NULL, FALSE), 0, NULL, NULL, FALSE, NULL, 0 };
    /* No master open: fails at once, ssh not run */
    ssh_exec_async(&t, "true", NULL, NULL, on_exec, &r);
    g_main_loop_run(r.loop);
    g_assert_cmpint(r.status, ==, -1);
    g_assert_cmpstr(r.out, ==, "");
    g_clear_pointer(&r.out, g_free);
    g_clear_pointer(&r.err, g_free);

    gchar* sock = ssh_session_control_path(&t); /* the master is open */
    g_assert_true(g_file_set_contents(sock, "", 0, NULL));
    gchar* bad_sock = NULL;
    ssh_exec_async(&t, "cat /proc/stat", "input", NULL, on_exec, &r);
    g_main_loop_run(r.loop);
    g_assert_cmpint(r.status, ==, 0);
    g_assert_cmpstr(r.out, ==, "me@srv|cat /proc/stat|bar|input");
    g_clear_pointer(&r.out, g_free);
    g_clear_pointer(&r.err, g_free);
    ssh_session_set_env("FOO", NULL);

    SshTarget bad = { "fail", NULL, 0, NULL };
    bad_sock = ssh_session_control_path(&bad);
    g_assert_true(g_file_set_contents(bad_sock, "", 0, NULL));
    ssh_exec_async(&bad, "true", NULL, NULL, on_exec, &r);
    g_main_loop_run(r.loop);
    g_assert_cmpint(r.status, ==, 255);
    g_assert_nonnull(strstr(r.err, "Permission denied"));
    g_free(r.out);
    g_free(r.err);

    /* The same, waiting (worker threads): stdin closed at once */
    gint status = -2;
    GBytes* out = NULL;
    GError* error = NULL;
    g_assert_true(ssh_exec_sync(&t, "ls", NULL, NULL, &status, &out, &error));
    g_assert_no_error(error);
    g_assert_cmpint(status, ==, 0);
    gsize n = 0;
    const gchar* data = g_bytes_get_data(out, &n);
    g_assert_cmpmem(data, n, "me@srv|ls||", 11); /* no input */
    g_bytes_unref(out);
    g_assert_false(ssh_exec_sync(&bad, "true", NULL, NULL, &status, &out, &error));
    g_assert_nonnull(strstr(error->message, "Permission denied"));
    g_assert_null(out);
    g_clear_error(&error);

    g_remove(sock);
    g_remove(bad_sock);
    g_free(sock);
    g_free(bad_sock);
    g_main_loop_unref(r.loop);
}

static void on_ready(SshMaster* m, gboolean ok, const gchar* text, gpointer data) {
    (void)m;
    Result* r = data;
    r->ok = ok;
    r->text = g_strdup(text);
    r->calls++;
    g_main_loop_quit(r->loop);
}

static void test_master(void) {
    SshTarget t = { "srv", "me", 0, NULL };
    Result r = { g_main_loop_new(NULL, FALSE), 0, NULL, NULL, FALSE, NULL, 0 };
    SshMaster* m = ssh_master_start(&t, on_ready, &r);
    g_main_loop_run(r.loop);
    g_assert_true(r.ok);
    g_assert_cmpuint(r.calls, ==, 1);
    g_assert_true(ssh_master_is_up(m));
    gchar* sock = ssh_session_control_path(&t);
    g_assert_true(g_file_test(sock, G_FILE_TEST_EXISTS));
    ssh_master_stop(m);
    g_assert_false(g_file_test(sock, G_FILE_TEST_EXISTS));
    g_free(sock);
    /* The process wait ends after the stop: no call */
    for (gint i = 0; i < 50; i++) g_main_context_iteration(NULL, FALSE);
    g_usleep(100000);
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_assert_cmpuint(r.calls, ==, 1);

    SshTarget bad = { "fail", NULL, 0, NULL };
    r.calls = 0;
    g_clear_pointer(&r.text, g_free);
    m = ssh_master_start(&bad, on_ready, &r);
    g_main_loop_run(r.loop);
    g_assert_false(r.ok);
    g_assert_nonnull(strstr(r.text, "Permission denied"));
    g_assert_false(ssh_master_is_up(m));
    ssh_master_stop(m);
    g_free(r.text);

    /* ssh missing */
    ssh_session_set_program("/nonexistent/ssh");
    r.calls = 0;
    r.text = NULL;
    m = ssh_master_start(&t, on_ready, &r);
    g_main_loop_run(r.loop);
    g_assert_false(r.ok);
    g_assert_nonnull(r.text);
    ssh_master_stop(m);
    g_free(r.text);
    g_main_loop_unref(r.loop);
}

static void test_transport(void) {
    SshTarget t = { "srv", "me", 22, NULL };
    const gchar* prev = g_getenv("LOOKINFILES_SSH_LIBSSH");
    gchar* saved = g_strdup(prev);
    g_unsetenv("LOOKINFILES_SSH_LIBSSH");
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_OPENSSH_MUX);
    g_assert_cmpint(ssh_session_transport(NULL), ==, SSH_TRANSPORT_OPENSSH_MUX);
#ifdef HAVE_LIBSSH
    g_setenv("LOOKINFILES_SSH_LIBSSH", "1", TRUE);
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_LIBSSH);
    g_setenv("LOOKINFILES_SSH_LIBSSH", "0", TRUE);
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_OPENSSH_MUX);
    /* The Preferences choice: used when the variable is not set */
    ssh_session_choose_libssh(1);
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_OPENSSH_MUX); /* the variable wins */
    g_unsetenv("LOOKINFILES_SSH_LIBSSH");
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_LIBSSH);
    ssh_session_choose_libssh(0);
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_OPENSSH_MUX);
    ssh_session_choose_libssh(-1); /* the default: the ssh program here */
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_OPENSSH_MUX);
#else
    ssh_session_choose_libssh(1); /* no libssh: no effect */
    g_assert_cmpint(ssh_session_transport(&t), ==, SSH_TRANSPORT_OPENSSH_MUX);
    ssh_session_choose_libssh(-1);
#endif
    if (saved != NULL) {
        g_setenv("LOOKINFILES_SSH_LIBSSH", saved, TRUE);
        g_free(saved);
    } else {
        g_unsetenv("LOOKINFILES_SSH_LIBSSH");
    }
}

static void remove_tree(const gchar* path) {
    GDir* d = g_dir_open(path, 0, NULL);
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        g_dir_close(d);
    }
    g_remove(path);
}

int main(int argc, char** argv) {
    tmp_dir = g_dir_make_tmp("ssh_session_test_XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);
    g_setenv("XDG_RUNTIME_DIR", tmp_dir, TRUE);
    gchar* fake = g_build_filename(tmp_dir, "fake-ssh", NULL);
    g_assert_true(g_file_set_contents(fake, FAKE_SSH, -1, NULL));
    g_chmod(fake, 0700);

    g_test_init(&argc, &argv, NULL);
    ssh_session_set_program(fake);
    g_test_add_func("/ssh_session/argv", test_argv);
    g_test_add_func("/ssh_session/transport", test_transport);
    g_test_add_func("/ssh_session/control_path", test_control_path);
    g_test_add_func("/ssh_session/master_missing", test_master_missing);
    g_test_add_func("/ssh_session/exec", test_exec);
    g_test_add_func("/ssh_session/master", test_master);
    gint rc = g_test_run();

    remove_tree(tmp_dir);
    g_free(fake);
    g_free(tmp_dir);
    return rc;
}
