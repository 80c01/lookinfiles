/* Unit tests for git_mirror: the listing, and the copy read from a tar stream */
#include "git/git_mirror.h"
#include "git/git_log.h"
#include "git/git_utils.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_listing(void) {
    static const gchar out[] = "41 1795343863.2 HEAD\0" "120 1795343800 refs/heads/main\0" "bad\0" "9 1 cut";
    GHashTable* l = git_mirror_parse_listing(out, sizeof(out) - 1);
    g_assert_cmpuint(g_hash_table_size(l), ==, 2);
    g_assert_cmpstr(g_hash_table_lookup(l, "HEAD"), ==, "41 1795343863.2");
    g_assert_cmpstr(g_hash_table_lookup(l, "refs/heads/main"), ==, "120 1795343800");
    g_hash_table_destroy(l);
}

/* A ustar header for name (with prefix) of size bytes, type */
static void header(GByteArray* a, const gchar* prefix, const gchar* name, gsize size, gchar type) {
    guint8 h[512] = { 0 };
    memcpy(h, name, MIN(strlen(name), 100));
    g_snprintf((gchar*)h + 124, 12, "%011o", (guint)size);
    h[156] = (guint8)type;
    memcpy(h + 257, "ustar", 5);
    if (prefix != NULL) memcpy(h + 345, prefix, strlen(prefix));
    g_byte_array_append(a, h, 512);
}

static void data(GByteArray* a, const gchar* text) {
    gsize n = strlen(text);
    g_byte_array_append(a, (const guint8*)text, (guint)n);
    guint8 zero[512] = { 0 };
    if (n % 512 != 0) g_byte_array_append(a, zero, (guint)(512 - n % 512));
}

static void test_extract(void) {
    GByteArray* a = g_byte_array_new();
    header(a, NULL, "./HEAD", 4, '0');
    data(a, "main");
    header(a, "refs/heads", "dev", 3, '0');
    data(a, "abc");
    header(a, NULL, "refs/", 0, '5');
    header(a, NULL, "././@LongLink", 9, 'L');
    data(a, "objects/x");
    header(a, NULL, "ignored-short-name", 2, '0');
    data(a, "xy");
    header(a, NULL, "../escape", 2, '0');
    data(a, "no");
    guint8 zero[1024] = { 0 };
    g_byte_array_append(a, zero, sizeof zero);
    gchar* dir = g_dir_make_tmp("mirror-XXXXXX", NULL);
    GInputStream* in = g_memory_input_stream_new_from_data(a->data, a->len, NULL);
    guint files = 0;
    GError* err = NULL;
    g_assert_true(git_mirror_extract(in, dir, NULL, &files, &err));
    g_assert_no_error(err);
    g_assert_cmpuint(files, ==, 3);
    gchar* text = NULL;
    gchar* p = g_build_filename(dir, "HEAD", NULL);
    g_assert_true(g_file_get_contents(p, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "main");
    g_free(text);
    g_free(p);
    p = g_build_filename(dir, "refs/heads/dev", NULL);
    g_assert_true(g_file_test(p, G_FILE_TEST_IS_REGULAR));
    g_free(p);
    p = g_build_filename(dir, "objects/x", NULL); /* the long name */
    g_assert_true(g_file_get_contents(p, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "xy");
    g_free(text);
    g_free(p);
    gchar* parent = g_path_get_dirname(dir);
    p = g_build_filename(parent, "escape", NULL);
    g_assert_false(g_file_test(p, G_FILE_TEST_EXISTS));
    g_free(p);
    g_free(parent);
    g_object_unref(in);

    /* Cut short */
    in = g_memory_input_stream_new_from_data(a->data, 512 + 2, NULL);
    g_assert_false(git_mirror_extract(in, dir, NULL, &files, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT);
    g_clear_error(&err);
    g_object_unref(in);
    g_byte_array_unref(a);
    gchar* rm = g_strdup_printf("rm -rf '%s'", dir);
    g_assert_true(g_spawn_command_line_sync(rm, NULL, NULL, NULL, NULL));
    g_free(rm);
    g_free(dir);
}

/* A real .git sent through tar as the machine does: libgit2 reads its copy */
static void test_copy_readable(void) {
    if (g_find_program_in_path("git") == NULL || g_find_program_in_path("tar") == NULL) {
        g_test_skip("no git or tar");
        return;
    }
    gchar* base = g_dir_make_tmp("mirror-repo-XXXXXX", NULL);
    gchar* cmd = g_strdup_printf("sh -c \"cd '%s' && git init -q -b main src && cd src && echo a > a.txt && git add a.txt"
                                 " && git -c user.name=t -c user.email=t@t commit -qm first\"", base);
    gint status = 1;
    g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL));
    g_free(cmd);
    if (status != 0) {
        g_test_skip("git could not make a repository");
        g_free(base);
        return;
    }
    gchar* script = g_strdup_printf("cd '%s/src/.git' && find . -type f -print0 | tar --format=ustar -cf - --null -T -",
                                    base);
    GSubprocess* p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL, "sh", "-c", script, NULL);
    g_free(script);
    g_assert_nonnull(p);
    gchar* gitdir = g_build_filename(base, "copy", ".git", NULL);
    guint files = 0;
    g_assert_true(git_mirror_extract(g_subprocess_get_stdout_pipe(p), gitdir, NULL, &files, NULL));
    g_subprocess_wait(p, NULL, NULL);
    g_object_unref(p);
    g_assert_cmpuint(files, >, 3);
    gchar* copy = g_build_filename(base, "copy", NULL);
    gchar* branch = git_read_branch(copy);
    g_assert_cmpstr(branch, ==, "main");
    g_free(branch);
    GPtrArray* log = git_read_log(copy, 0, 10, NULL, NULL, FALSE, NULL, NULL);
    g_assert_nonnull(log);
    g_assert_cmpuint(log->len, ==, 1);
    g_ptr_array_unref(log);
    g_free(copy);
    g_free(gitdir);
    gchar* rm = g_strdup_printf("rm -rf '%s'", base);
    g_spawn_command_line_sync(rm, NULL, NULL, NULL, NULL);
    g_free(rm);
    g_free(base);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_mirror/listing", test_listing);
    g_test_add_func("/git_mirror/extract", test_extract);
    g_test_add_func("/git_mirror/copy_readable", test_copy_readable);
    return g_test_run();
}
