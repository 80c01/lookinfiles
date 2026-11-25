#include "core/wsl_distros.h"

#include <string.h>

/* text as wsl.exe writes it: UTF-16LE, "\r\n", with a byte order mark when bom */
static GBytes* utf16(const gchar* text, gboolean bom) {
    glong n = 0;
    gunichar2* units = g_utf8_to_utf16(text, -1, NULL, &n, NULL);
    GByteArray* b = g_byte_array_new();
    if (bom) {
        guint8 m[2] = {0xFF, 0xFE};
        g_byte_array_append(b, m, 2);
    }
    for (glong i = 0; i < n; i++) {
        guint8 pair[2] = {(guint8)(units[i] & 0xFF), (guint8)(units[i] >> 8)};
        g_byte_array_append(b, pair, 2);
    }
    g_free(units);
    return g_byte_array_free_to_bytes(b);
}

static gchar* decode(GBytes* b) {
    gsize len = 0;
    const gchar* data = g_bytes_get_data(b, &len);
    return wsl_output_to_utf8(data, len);
}

static void test_decode(void) {
    GBytes* b = utf16("  NAME   STATE   VERSION\r\n* Ubuntu Running 2\r\n", FALSE);
    gchar* t = decode(b);
    g_assert_cmpstr(t, ==, "  NAME   STATE   VERSION\n* Ubuntu Running 2\n");
    g_free(t);
    g_bytes_unref(b);
    b = utf16("Débian\r\n", TRUE);
    t = decode(b);
    g_assert_cmpstr(t, ==, "Débian\n");
    g_free(t);
    g_bytes_unref(b);
    t = wsl_output_to_utf8("Ubuntu\r\nDebian\r\n", 16); /* WSL_UTF8=1 */
    g_assert_cmpstr(t, ==, "Ubuntu\nDebian\n");
    g_free(t);
    t = wsl_output_to_utf8(NULL, 0);
    g_assert_cmpstr(t, ==, "");
    g_free(t);
}

static void test_parse(void) {
    /* A French Windows: translated header and states */
    const gchar* verbose = "  NOM                    ÉTAT                    VERSION\n"
                           "* Ubuntu-24.04           En cours d'exécution    2\n"
                           "  docker-desktop         Arrêté                  2\n"
                           "  Alpine                 Arrêté                  1\n"
                           "  debian                 Arrêté                  2\n";
    GPtrArray* d = wsl_distros_parse(verbose);
    g_assert_cmpuint(d->len, ==, 3);
    WslDistro* first = g_ptr_array_index(d, 0);
    g_assert_cmpstr(first->name, ==, "Ubuntu-24.04");
    g_assert_true(first->is_default);
    g_assert_cmpint(first->version, ==, 2);
    g_assert_cmpstr(((WslDistro*)g_ptr_array_index(d, 1))->name, ==, "Alpine");
    g_assert_cmpint(((WslDistro*)g_ptr_array_index(d, 1))->version, ==, 1);
    g_assert_cmpstr(((WslDistro*)g_ptr_array_index(d, 2))->name, ==, "debian");
    wsl_distros_mark_running(d, "Ubuntu-24.04\ndocker-desktop\n");
    g_assert_true(first->running);
    g_assert_false(wsl_distros_find(d, "debian")->running);
    g_assert_null(wsl_distros_find(d, "docker-desktop"));
    g_ptr_array_unref(d);

    /* No distribution: wsl prints a sentence */
    d = wsl_distros_parse("Windows Subsystem for Linux has no installed distributions.\n");
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);
    d = wsl_distros_parse(NULL);
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);
}

static void test_paths(void) {
    gchar* p = wsl_distro_path("Ubuntu");
    g_assert_cmpstr(p, ==, "\\\\wsl.localhost\\Ubuntu\\");
    gchar* host = NULL;
    gchar* share = NULL;
    g_assert_cmpuint(wsl_unc_root(p, &host, &share), ==, strlen("\\\\wsl.localhost\\Ubuntu"));
    g_assert_cmpstr(host, ==, "wsl.localhost");
    g_assert_cmpstr(share, ==, "Ubuntu");
    g_free(host);
    g_free(share);
    g_free(p);
    g_assert_cmpuint(wsl_unc_root("//nas/films/2024", NULL, NULL), ==, strlen("//nas/films"));
    g_assert_cmpuint(wsl_unc_root("\\\\nas", NULL, NULL), ==, 0);
    g_assert_cmpuint(wsl_unc_root("C:\\Users", NULL, NULL), ==, 0);
    g_assert_cmpuint(wsl_unc_root("/home/a", NULL, NULL), ==, 0);
    g_assert_cmpuint(wsl_unc_root(NULL, NULL, NULL), ==, 0);

    gchar* d = wsl_distro_of_path("\\\\wsl.localhost\\Debian\\home\\ann");
    g_assert_cmpstr(d, ==, "Debian");
    g_free(d);
    d = wsl_distro_of_path("\\\\WSL$\\Ubuntu");
    g_assert_cmpstr(d, ==, "Ubuntu");
    g_free(d);
    g_assert_null(wsl_distro_of_path("\\\\nas\\films"));
    g_assert_null(wsl_distro_of_path("C:\\"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/wsl_distros/decode", test_decode);
    g_test_add_func("/wsl_distros/parse", test_parse);
    g_test_add_func("/wsl_distros/paths", test_paths);
    return g_test_run();
}
