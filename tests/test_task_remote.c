/* Unit tests for src/tasks/task_remote.c: the command line of a remote task
 * and the paths it writes. */
#include "tasks/task_remote.h"
#include "core/ssh_session.h"
#include "core/vfs.h"

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* The "ssh" run is this test program again, which runs the command line
 * with the local shell (the master connection's socket is a plain file, so
 * ssh_session.c takes it as open) */
static const SshTarget TARGET = {.host = "task-remote-test-host"};

static gboolean is_ssh_call(int argc, char** argv) {
    for (int i = 1; i < argc - 1; i++) {
        if (strstr(argv[i], TARGET.host) != NULL) return TRUE;
    }
    return FALSE;
}

static void test_command_line(void) {
    const gchar* const argv[] = {"make", "-j4", "it's", NULL};
    TaskDef* def = task_def_new("make -j4", argv, "/mnt/m");
    gchar* line = task_remote_command_line(def);
    g_assert_cmpstr(line, ==, "exec 2>&1; echo \"@@lookinfiles-task $$\"; exec 'make' '-j4' 'it'\\''s'");
    g_free(line);

    const gchar* const env[] = {"CC=clang", NULL};
    def->env = g_strdupv((gchar**)env);
    def->shell = TRUE;
    def->detail = g_strdup("./run.sh && echo ok");
    line = task_remote_command_line(def);
    g_assert_cmpstr(line, ==, "exec 2>&1; echo \"@@lookinfiles-task $$\"; exec env 'CC=clang' sh -c './run.sh && echo ok'");
    g_free(line);
    task_def_free(def);
}

static void test_pid(void) {
    g_assert_cmpint(task_remote_parse_pid("@@lookinfiles-task 4242"), ==, 4242);
    g_assert_cmpint(task_remote_parse_pid("@@lookinfiles-task 0"), ==, 0);
    g_assert_cmpint(task_remote_parse_pid("make: Entering directory"), ==, 0);
    g_assert_cmpint(task_remote_parse_pid(NULL), ==, 0);
}

static void test_map_path(void) {
    gchar* p = task_remote_map_path("/home/u", "/run/l/remotes/m1", "/home/u/proj/src/a.c");
    gchar* want = g_build_filename("/run/l/remotes/m1", "proj/src/a.c", NULL);
    g_assert_cmpstr(p, ==, want);
    g_free(want);
    g_free(p);
    /* Not a prefix of a longer name, nor outside the folder */
    g_assert_null(task_remote_map_path("/home/u", "/run/l/remotes/m1", "/home/user2/a.c"));
    g_assert_null(task_remote_map_path("/home/u", "/run/l/remotes/m1", "/usr/include/stdio.h"));
    p = task_remote_map_path("/home/u/", "/run/l/remotes/m1", "/home/u");
    g_assert_cmpstr(p, ==, "/run/l/remotes/m1");
    g_free(p);
    /* A machine seen from its root */
    p = task_remote_map_path("/", "/run/l/remotes/m2", "/etc/hosts");
    want = g_build_filename("/run/l/remotes/m2", "etc/hosts", NULL);
    g_assert_cmpstr(p, ==, want);
    g_free(want);
    g_free(p);
    /* Back slashes (the paths read on Windows) */
    p = task_remote_map_path("/home/u", "/run/l/remotes/m1", "\\home\\u\\x.c");
    want = g_build_filename("/run/l/remotes/m1", "x.c", NULL);
    g_assert_cmpstr(p, ==, want);
    g_free(want);
    g_free(p);
}

typedef struct {
    gchar* ready;
    GString* out;
    gint status;
    gchar* error;
    gboolean done;
    TaskRemote* t;
} Seen;

static void on_ready(gpointer data, const gchar* dir) {
    Seen* s = data;
    s->ready = g_strdup(dir);
}

static void on_output(gpointer data, const gchar* bytes, gsize len) {
    Seen* s = data;
    g_string_append_len(s->out, bytes, (gssize)len);
}

static void on_done(gpointer data, gint status, const gchar* error) {
    Seen* s = data;
    s->status = status;
    s->error = g_strdup(error);
    s->done = TRUE;
}

static const TaskRemoteFuncs FUNCS = {on_ready, on_output, on_done};

static gboolean on_timeout(gpointer data) {
    (void)data;
    g_error("the remote task did not end");
    return G_SOURCE_REMOVE;
}

/* Run argv in /virtual/t/sub (the folder sub of remote), until done; when
 * stop, stopped once it printed something */
static Seen* run(const gchar* const* argv, gboolean stop) {
    Seen* s = g_new0(Seen, 1);
    s->out = g_string_new(NULL);
    TaskDef* def = task_def_new("t", argv, "/virtual/t/sub");
    s->t = task_remote_start(def, &FUNCS, s);
    task_def_free(def);
    guint timeout = g_timeout_add_seconds(20, on_timeout, NULL);
    gboolean stopped = FALSE;
    while (!s->done) {
        g_main_context_iteration(NULL, TRUE);
        if (stop && !stopped && s->out->len > 0) {
            task_remote_stop(s->t, FALSE);
            stopped = TRUE;
        }
    }
    g_source_remove(timeout);
    return s;
}

static void seen_free(Seen* s) {
    task_remote_free(s->t);
    g_free(s->ready);
    g_string_free(s->out, TRUE);
    g_free(s->error);
    g_free(s);
}

static gchar* remote;

static void test_run(void) {
    const gchar* const argv[] = {"sh", "-c", "echo building; printf 'src/a.c:3:4: error: boom\\n' >&2; exit 3", NULL};
    Seen* s = run(argv, FALSE);
    g_assert_null(s->error);
    gchar* sub = g_build_filename(remote, "sub", NULL);
    g_assert_cmpstr(s->ready, ==, sub);
    /* Errors merged, the process number not shown */
    g_assert_cmpstr(s->out->str, ==, "building\nsrc/a.c:3:4: error: boom\n");
    g_assert_cmpint(s->status, ==, 3);
    gchar* file = g_build_filename(sub, "src", "a.c", NULL);
    gchar* here = task_remote_local_path(s->t, file);
    g_assert_cmpstr(here, ==, "/virtual/t/sub/src/a.c");
    g_free(here);
    g_free(file);
    g_free(sub);
    seen_free(s);
}

static void test_stop(void) {
    const gchar* const argv[] = {"sh", "-c", "echo started; exec sleep 30", NULL};
    gint64 start = g_get_monotonic_time();
    Seen* s = run(argv, TRUE);
    g_assert_cmpstr(s->out->str, ==, "started\n");
    g_assert_cmpint(s->status, !=, 0);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 10 * G_USEC_PER_SEC);
    seen_free(s);
}

static void test_not_remote(void) {
    const gchar* const argv[] = {"true", NULL};
    TaskDef* def = task_def_new("t", argv, "/not/a/mount");
    Seen* s = g_new0(Seen, 1);
    s->out = g_string_new(NULL);
    s->t = task_remote_start(def, &FUNCS, s);
    task_def_free(def);
    while (!s->done) g_main_context_iteration(NULL, TRUE);
    g_assert_nonnull(s->error);
    g_assert_null(s->ready);
    seen_free(s);
}

int main(int argc, char** argv) {
    if (is_ssh_call(argc, argv)) {
        execl("/bin/sh", "sh", "-c", argv[argc - 1], (char*)NULL);
        return 127;
    }
    g_test_init(&argc, &argv, NULL);
    gchar* program = g_canonicalize_filename(argv[0], NULL);
    ssh_session_set_program(program);
    g_setenv("LOOKINFILES_SSH_LIBSSH", "0", TRUE); /* the ssh program */
    gchar* runtime = g_dir_make_tmp("task-remote-run-XXXXXX", NULL);
    g_setenv("XDG_RUNTIME_DIR", runtime, TRUE); /* the sockets' folder */
    gchar* tmp = g_dir_make_tmp("task-remote-XXXXXX", NULL);
    /* As pwd -P says it (links resolved) */
    gchar* resolved = realpath(tmp, NULL);
    remote = g_strdup(resolved);
    free(resolved);
    gchar* sub = g_build_filename(remote, "sub", NULL);
    g_mkdir(sub, 0700);
    gchar* sock = ssh_session_control_path(&TARGET);
    g_assert_true(g_file_set_contents(sock, "", 0, NULL));
    vfs_mount("/virtual/t", &TARGET, remote);
    g_test_add_func("/task_remote/run", test_run);
    g_test_add_func("/task_remote/stop", test_stop);
    g_test_add_func("/task_remote/not_remote", test_not_remote);
    g_test_add_func("/task_remote/command_line", test_command_line);
    g_test_add_func("/task_remote/pid", test_pid);
    g_test_add_func("/task_remote/map_path", test_map_path);
    int status = g_test_run();
    vfs_unmount("/virtual/t");
    vfs_shutdown();
    const gchar* rm_argv[] = {"rm", "-rf", tmp, runtime, NULL};
    g_spawn_sync(NULL, (gchar**)rm_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
    g_free(sock);
    g_free(sub);
    g_free(remote);
    g_free(tmp);
    g_free(runtime);
    g_free(program);
    return status;
}
