/* Unit tests for src/core/diagnostics.c: os-release parsing, install
 * commands, the remote command run by the local sh, and a made-up output. */
#include "core/diagnostics.h"

#include <string.h>

static void test_family(void) {
    g_assert_cmpint(diagnostics_family(NULL), ==, DIAG_FAMILY_UNKNOWN);
    g_assert_cmpint(diagnostics_family(""), ==, DIAG_FAMILY_UNKNOWN);
    g_assert_cmpint(diagnostics_family("NAME=\"Ubuntu\"\nID=ubuntu\nID_LIKE=debian\n"), ==, DIAG_FAMILY_APT);
    g_assert_cmpint(diagnostics_family("ID=fedora\n"), ==, DIAG_FAMILY_DNF);
    g_assert_cmpint(diagnostics_family("ID=\"rocky\"\nID_LIKE=\"rhel centos fedora\"\n"), ==, DIAG_FAMILY_DNF);
    g_assert_cmpint(diagnostics_family("ID=arch\n"), ==, DIAG_FAMILY_PACMAN);
    g_assert_cmpint(diagnostics_family("ID=alpine\n"), ==, DIAG_FAMILY_APK);
    g_assert_cmpint(diagnostics_family("ID=\"opensuse-tumbleweed\"\nID_LIKE=\"opensuse suse\"\n"), ==,
                    DIAG_FAMILY_ZYPPER);
    /* An unknown id falls back on ID_LIKE, in order */
    g_assert_cmpint(diagnostics_family("ID=elementary\nID_LIKE='ubuntu debian'\n"), ==, DIAG_FAMILY_APT);
    g_assert_cmpint(diagnostics_family("ID=garuda\nID_LIKE=arch\n"), ==, DIAG_FAMILY_PACMAN);
    g_assert_cmpint(diagnostics_family("ID=nixos\n"), ==, DIAG_FAMILY_UNKNOWN);
    /* IDENTIFIER=... is not ID */
    g_assert_cmpint(diagnostics_family("IDENTIFIER=ubuntu\n"), ==, DIAG_FAMILY_UNKNOWN);
}

static void test_os_name(void) {
    gchar* name = diagnostics_os_name("NAME=\"Debian GNU/Linux\"\nPRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\n");
    g_assert_cmpstr(name, ==, "Debian GNU/Linux 13 (trixie)");
    g_free(name);
    name = diagnostics_os_name("NAME=Alpine\n");
    g_assert_cmpstr(name, ==, "Alpine");
    g_free(name);
    g_assert_null(diagnostics_os_name(NULL));
}

static void test_items(void) {
    GPtrArray* local = diagnostics_items(FALSE);
    GPtrArray* remote = diagnostics_items(TRUE);
    g_assert_nonnull(diagnostics_find(local, "vte"));
    g_assert_nonnull(diagnostics_find(local, "libssh"));
    g_assert_nonnull(diagnostics_find(local, "ctags"));
    g_assert_nonnull(diagnostics_find(local, "jupyter"));
    g_assert_null(diagnostics_find(local, "find"));
    g_assert_null(diagnostics_find(remote, "vte"));
    g_assert_null(diagnostics_find(remote, "libssh"));
    g_assert_null(diagnostics_find(remote, "latex"));
    g_assert_nonnull(diagnostics_find(remote, "find"));
    g_assert_nonnull(diagnostics_find(remote, "python"));
    g_assert_null(diagnostics_find(local, "nothing"));
    g_ptr_array_unref(local);
    g_ptr_array_unref(remote);
}

static void test_lang_server(void) {
    GPtrArray* items = diagnostics_items(TRUE);
    guint n = items->len;
    diagnostics_add_lang_server(items, "rust", "Rust", NULL);
    g_assert_cmpuint(items->len, ==, n);
    gchar* lines[] = {"pylsp", "pyright-langserver --stdio", "pylsp --verbose", NULL};
    diagnostics_add_lang_server(items, "python", "Python", lines);
    DiagItem* item = diagnostics_find(items, "lsp-python");
    g_assert_nonnull(item);
    g_assert_cmpint(item->section, ==, DIAG_SECTION_LANG_SERVERS);
    g_assert_cmpstr(item->feature, ==, "Python");
    g_assert_cmpuint(g_strv_length(item->programs), ==, 2);
    g_assert_cmpstr(item->programs[1], ==, "pyright-langserver");
    gchar* cmd = diagnostics_install_command(item, DIAG_FAMILY_APT);
    g_assert_cmpstr(cmd, ==, "sudo apt install python3-pylsp");
    g_free(cmd);
    cmd = diagnostics_install_command(item, DIAG_FAMILY_UNKNOWN);
    g_assert_cmpstr(cmd, ==, "pipx install python-lsp-server");
    g_free(cmd);
    g_ptr_array_unref(items);
}

static void test_install_command(void) {
    GPtrArray* items = diagnostics_items(FALSE);
    DiagItem* gs = diagnostics_find(items, "gs");
    gchar* cmd = diagnostics_install_command(gs, DIAG_FAMILY_APT);
    g_assert_cmpstr(cmd, ==, "sudo apt install ghostscript");
    g_free(cmd);
    cmd = diagnostics_install_command(gs, DIAG_FAMILY_PACMAN);
    g_assert_cmpstr(cmd, ==, "sudo pacman -S ghostscript");
    g_free(cmd);
    cmd = diagnostics_install_command(gs, DIAG_FAMILY_BREW);
    g_assert_cmpstr(cmd, ==, "brew install ghostscript");
    g_free(cmd);
    g_assert_null(diagnostics_install_command(gs, DIAG_FAMILY_UNKNOWN));
    DiagItem* jupyter = diagnostics_find(items, "jupyter");
    cmd = diagnostics_install_command(jupyter, DIAG_FAMILY_ZYPPER);
    g_assert_cmpstr(cmd, ==, "pip install jupyter_client ipykernel");
    g_free(cmd);
    DiagItem* vte = diagnostics_find(items, "vte");
    cmd = diagnostics_install_command(vte, DIAG_FAMILY_DNF);
    g_assert_cmpstr(cmd, ==, "sudo dnf install vte291-gtk4-devel");
    g_free(cmd);
    DiagItem* libssh = diagnostics_find(items, "libssh");
    cmd = diagnostics_install_command(libssh, DIAG_FAMILY_APT);
    g_assert_cmpstr(cmd, ==, "sudo apt install libssh-dev");
    g_free(cmd);
    /* Found: nothing to install */
    gs->found = g_strdup("/usr/bin/gs");
    g_assert_null(diagnostics_install_command(gs, DIAG_FAMILY_APT));
    g_assert_null(diagnostics_install_command(NULL, DIAG_FAMILY_APT));
    g_ptr_array_unref(items);
}

static void test_check_local(void) {
    GPtrArray* items = diagnostics_items(FALSE);
    gchar* lines[] = {"sh", NULL};
    diagnostics_add_lang_server(items, "shell", "Shell", lines);
    diagnostics_check_local(items);
    for (guint i = 0; i < items->len; i++) {
        DiagItem* item = g_ptr_array_index(items, i);
        g_assert_true(item->checked);
    }
    g_assert_nonnull(diagnostics_find(items, "lsp-shell")->found);
    g_ptr_array_unref(items);
}

static void test_remote_parse(void) {
    GPtrArray* items = diagnostics_items(TRUE);
    diagnostics_add_info(items, DIAG_SECTION_CONNECTION, "ssh-transport", "SSH transport",
                         "libssh (shared connection)");
    gchar* lines[] = {"clangd", NULL};
    diagnostics_add_lang_server(items, "c", "C", lines);
    const gchar* out = "@@os\nID=debian\nPRETTY_NAME=\"Debian\"\n"
                       "@@find\nfind (GNU findutils) 4.9.0\n"
                       "@@jupyter\n/usr/bin/python3\n"
                       "@@path\npython3\t/usr/bin/python3\npython\t\ngit\t\ngrep\t/bin/grep\nclangd\t\n"
                       "@@end\n";
    gchar* os = NULL;
    diagnostics_remote_apply(items, out, &os);
    g_assert_cmpint(diagnostics_family(os), ==, DIAG_FAMILY_APT);
    g_free(os);
    /* Connection info items are preserved across remote_apply */
    DiagItem* tr = diagnostics_find(items, "ssh-transport");
    g_assert_nonnull(tr);
    g_assert_cmpint(tr->section, ==, DIAG_SECTION_CONNECTION);
    g_assert_true(tr->checked);
    g_assert_cmpstr(tr->found, ==, "libssh (shared connection)");
    g_assert_cmpstr(diagnostics_find(items, "python")->found, ==, "/usr/bin/python3");
    g_assert_null(diagnostics_find(items, "git")->found);
    g_assert_true(diagnostics_find(items, "git")->checked);
    g_assert_cmpstr(diagnostics_find(items, "grep")->found, ==, "/bin/grep");
    g_assert_cmpstr(diagnostics_find(items, "jupyter")->found, ==, "/usr/bin/python3");
    g_assert_nonnull(diagnostics_find(items, "find")->found);
    g_assert_null(diagnostics_find(items, "lsp-c")->found);
    /* BSD find, no kernel */
    diagnostics_remote_apply(items, "@@find\n\n@@jupyter\n@@path\n@@end\n", NULL);
    g_assert_null(diagnostics_find(items, "find")->found);
    g_assert_null(diagnostics_find(items, "jupyter")->found);
    g_assert_null(diagnostics_find(items, "python")->found);
    g_assert_cmpstr(diagnostics_find(items, "ssh-transport")->found, ==, "libssh (shared connection)");
    g_ptr_array_unref(items);
    GPtrArray* empty = diagnostics_items_empty();
    g_assert_cmpuint(empty->len, ==, 0);
    diagnostics_add_info(empty, DIAG_SECTION_CONNECTION, "ssh-transport", "SSH transport", "OpenSSH");
    g_assert_cmpuint(empty->len, ==, 1);
    g_assert_cmpstr(diagnostics_find(empty, "ssh-transport")->found, ==, "OpenSSH");
    g_ptr_array_unref(empty);
}

/* The command runs in the local sh and its output reads back */
static void test_remote_command_runs(void) {
    GPtrArray* items = diagnostics_items(TRUE);
    gchar* lines[] = {"sh -s", "no-such-server-xyz", NULL};
    diagnostics_add_lang_server(items, "shell", "Shell", lines);
    gchar* cmd = diagnostics_remote_command(items);
    const gchar* argv[] = {"/bin/sh", "-c", cmd, NULL};
    gchar* out = NULL;
    gint status = -1;
    g_assert_true(g_spawn_sync(NULL, (gchar**)argv, NULL, 0, NULL, NULL, &out, NULL, &status, NULL));
    g_assert_nonnull(strstr(out, "@@end"));
    diagnostics_remote_apply(items, out, NULL);
    g_assert_nonnull(diagnostics_find(items, "lsp-shell")->found);
    g_assert_nonnull(diagnostics_find(items, "grep")->found);
    g_free(out);
    g_free(cmd);
    g_ptr_array_unref(items);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/diagnostics/family", test_family);
    g_test_add_func("/diagnostics/os_name", test_os_name);
    g_test_add_func("/diagnostics/items", test_items);
    g_test_add_func("/diagnostics/lang_server", test_lang_server);
    g_test_add_func("/diagnostics/install_command", test_install_command);
    g_test_add_func("/diagnostics/check_local", test_check_local);
    g_test_add_func("/diagnostics/remote_parse", test_remote_parse);
    g_test_add_func("/diagnostics/remote_command_runs", test_remote_command_runs);
    return g_test_run();
}
