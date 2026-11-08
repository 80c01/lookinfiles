/* Unit tests for src/formats/archive_listing.c (ZIP, tar, gzip and ar listings). */
#include "formats/archive_listing.h"

#include <glib/gstdio.h>
#include <string.h>

#include "zip_builder.h"

static gchar* tmp_dir;

static gchar* write_file(const gchar* name, const void* data, gsize len) {
    gchar* path = g_build_filename(tmp_dir, name, NULL);
    g_assert_true(g_file_set_contents(path, data, (gssize)len, NULL));
    return path;
}

/* One tar header block (ustar) */
static void tar_header(GByteArray* tar, const gchar* name, gchar type, guint64 size,
                       const gchar* link) {
    guchar h[512] = { 0 };
    strncpy((gchar*)h, name, 100);
    memcpy(h + 100, "0000644", 7);
    g_snprintf((gchar*)h + 124, 12, "%011" G_GINT64_MODIFIER "o", size);
    g_snprintf((gchar*)h + 136, 12, "%011o", 1761732000u); /* 2025-10-29 */
    h[156] = (guchar)type;
    if (link != NULL) {
        strncpy((gchar*)h + 157, link, 100);
    }
    memcpy(h + 257, "ustar\0" "00", 8);
    memset(h + 148, ' ', 8);
    guint sum = 0;
    for (gint i = 0; i < 512; i++) {
        sum += h[i];
    }
    g_snprintf((gchar*)h + 148, 8, "%06o", sum);
    g_byte_array_append(tar, h, 512);
}

static void tar_data(GByteArray* tar, const gchar* text) {
    gsize len = strlen(text);
    g_byte_array_append(tar, (const guint8*)text, (guint)len);
    guint8 zero[512] = { 0 };
    g_byte_array_append(tar, zero, (guint)((512 - len % 512) % 512));
}

static GByteArray* sample_tar(void) {
    GByteArray* tar = g_byte_array_new();
    tar_header(tar, "project/", '5', 0, NULL);
    tar_header(tar, "project/README", '0', 6, NULL);
    tar_data(tar, "hello\n");
    tar_header(tar, "project/link", '2', 0, "README");
    /* GNU long name */
    gchar* long_name = g_strnfill(150, 'n');
    tar_header(tar, "././@LongLink", 'L', 151, NULL);
    tar_data(tar, long_name);
    g_byte_array_append(tar, (const guint8*)"", 0);
    tar_header(tar, "short", '0', 3, NULL);
    tar_data(tar, "abc");
    g_free(long_name);
    guint8 zero[1024] = { 0 };
    g_byte_array_append(tar, zero, sizeof(zero));
    return tar;
}

static GBytes* gzip(const guint8* data, gsize len) {
    GZlibCompressor* c = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP, 6);
    GFileInfo* info = g_file_info_new();
    g_file_info_set_name(info, "notes.txt");
    g_zlib_compressor_set_file_info(c, info);
    g_object_unref(info);
    GInputStream* raw = g_memory_input_stream_new_from_data(data, (gssize)len, NULL);
    GInputStream* in = g_converter_input_stream_new(raw, G_CONVERTER(c));
    GOutputStream* out = g_memory_output_stream_new_resizable();
    g_output_stream_splice(out, in, G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, NULL, NULL);
    GBytes* bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(out));
    g_object_unref(out);
    g_object_unref(in);
    g_object_unref(raw);
    g_object_unref(c);
    return bytes;
}

static void test_zip(void) {
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "src/", "", FALSE);
    zip_builder_add(zb, "src/main.c", "int main(void) { return 0; } /* padding padding padding */", TRUE);
    zip_builder_add(zb, "META-INF/MANIFEST.MF", "Manifest-Version: 1.0\r\nMain-Class: demo.Main\r\n\r\nName: x\r\n", FALSE);
    GBytes* bytes = zip_builder_finish(zb);
    gchar* path = write_file("a.jar", g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
    GError* error = NULL;
    g_autoptr(ArchiveListing) l = archive_list(path, 100, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(l->format, ==, "ZIP archive");
    g_assert_cmpuint(l->n_entries, ==, 3);
    g_assert_cmpuint(l->n_dirs, ==, 1);
    g_assert_cmpuint(l->n_files, ==, 2);
    const ArchiveEntry* e = &g_array_index(l->entries, ArchiveEntry, 1);
    g_assert_cmpstr(e->path, ==, "src/main.c");
    g_assert_cmpint(e->packed, <, (gint64)e->size);
    g_assert_cmpstr(l->manifest, ==, "Manifest-Version: 1.0\nMain-Class: demo.Main");
    /* Entry limit: all are counted, the first ones kept */
    g_autoptr(ArchiveListing) few = archive_list(path, 1, NULL, NULL);
    g_assert_cmpuint(few->entries->len, ==, 1);
    g_assert_cmpuint(few->n_entries, ==, 3);
    g_bytes_unref(bytes);
    g_remove(path);
    g_free(path);
}

static void check_tar_listing(const ArchiveListing* l) {
    g_assert_cmpuint(l->n_entries, ==, 4);
    g_assert_cmpuint(l->n_dirs, ==, 1);
    const ArchiveEntry* dir = &g_array_index(l->entries, ArchiveEntry, 0);
    g_assert_cmpint(dir->type, ==, ARCHIVE_ENTRY_DIR);
    const ArchiveEntry* readme = &g_array_index(l->entries, ArchiveEntry, 1);
    g_assert_cmpstr(readme->path, ==, "project/README");
    g_assert_cmpuint(readme->size, ==, 6);
    g_assert_cmpint(readme->mtime, ==, 1761732000);
    const ArchiveEntry* link = &g_array_index(l->entries, ArchiveEntry, 2);
    g_assert_cmpint(link->type, ==, ARCHIVE_ENTRY_SYMLINK);
    g_assert_cmpstr(link->link, ==, "README");
    const ArchiveEntry* longe = &g_array_index(l->entries, ArchiveEntry, 3);
    g_assert_cmpuint(strlen(longe->path), ==, 150);
    g_assert_cmpuint(l->total_size, ==, 9);
}

static void test_tar(void) {
    GByteArray* tar = sample_tar();
    gchar* path = write_file("a.tar", tar->data, tar->len);
    GError* error = NULL;
    g_autoptr(ArchiveListing) l = archive_list(path, 100, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(l->format, ==, "tar archive");
    check_tar_listing(l);
    g_assert_false(l->incomplete);
    g_assert_cmpstr(archive_listing_format_name(tar->data, tar->len), ==, "tar archive");

    GBytes* gz = gzip(tar->data, tar->len);
    gchar* gz_path = write_file("a.tgz", g_bytes_get_data(gz, NULL), g_bytes_get_size(gz));
    g_autoptr(ArchiveListing) lz = archive_list(gz_path, 100, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(lz->format, ==, "gzip-compressed tar archive");
    check_tar_listing(lz);
    g_bytes_unref(gz);
    g_byte_array_unref(tar);
    g_remove(gz_path);
    g_remove(path);
    g_free(gz_path);
    g_free(path);
}

static void test_gzip_single(void) {
    const gchar* text = "line one\nline two\n";
    GBytes* gz = gzip((const guint8*)text, strlen(text));
    gchar* path = write_file("notes.txt.gz", g_bytes_get_data(gz, NULL), g_bytes_get_size(gz));
    g_autoptr(ArchiveListing) l = archive_list(path, 100, NULL, NULL);
    g_assert_true(l->single);
    g_assert_cmpstr(l->inner_name, ==, "notes.txt");
    g_assert_cmpint(l->inner_size, ==, (gint64)strlen(text));
    g_assert_cmpmem(g_bytes_get_data(l->inner_head, NULL), g_bytes_get_size(l->inner_head), text, strlen(text));
    g_bytes_unref(gz);
    g_remove(path);
    g_free(path);
}

static void test_ar(void) {
    GString* ar = g_string_new("!<arch>\n");
    /* symbol table, GNU long names table, two members */
    g_string_append_printf(ar, "%-16s%-12s%-6s%-6s%-8s%-10u`\n", "/", "0", "0", "0", "0", 4u);
    g_string_append_len(ar, "\0\0\0\0", 4);
    const gchar* names = "a_very_long_member_name.o/\n";
    g_string_append_printf(ar, "%-16s%-12s%-6s%-6s%-8s%-10u`\n", "//", "0", "0", "0", "0", (guint)strlen(names));
    g_string_append(ar, names);
    g_string_append_c(ar, '\n'); /* pad to even */
    g_string_append_printf(ar, "%-16s%-12s%-6s%-6s%-8s%-10u`\n", "/0", "1700000000", "0", "0", "644", 3u);
    g_string_append(ar, "abc\n");
    g_string_append_printf(ar, "%-16s%-12s%-6s%-6s%-8s%-10u`\n", "b.o/", "1700000000", "0", "0", "644", 2u);
    g_string_append(ar, "xy");
    gchar* path = write_file("libx.a", ar->str, ar->len);
    g_autoptr(ArchiveListing) l = archive_list(path, 100, NULL, NULL);
    g_assert_nonnull(l);
    g_assert_cmpstr(l->format, ==, "ar archive");
    g_assert_cmpuint(l->n_entries, ==, 2);
    g_assert_cmpstr(g_array_index(l->entries, ArchiveEntry, 0).path, ==, "a_very_long_member_name.o");
    g_assert_cmpuint(g_array_index(l->entries, ArchiveEntry, 0).size, ==, 3);
    g_assert_cmpstr(g_array_index(l->entries, ArchiveEntry, 1).path, ==, "b.o");
    g_string_free(ar, TRUE);
    g_remove(path);
    g_free(path);
}

static void test_other_compressions(void) {
#ifdef HAVE_LIBARCHIVE
    gchar* xz = g_find_program_in_path("xz");
    if (xz == NULL) {
        g_test_skip("xz not installed");
        return;
    }
    /* A single xz-compressed file shows its content; a .tar.xz lists its entries */
    gchar* text = write_file("notes.txt", "hello xz\n", 9);
    gchar* cmd = g_strdup_printf("xz -k -f %s", text);
    g_assert_true(g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL));
    gchar* xz_path = g_strconcat(text, ".xz", NULL);
    g_autoptr(ArchiveListing) single = archive_list(xz_path, 100, NULL, NULL);
    g_assert_nonnull(single);
    g_assert_true(single->single);
    g_assert_cmpstr(single->format, ==, "xz-compressed file");
    g_assert_cmpmem(g_bytes_get_data(single->inner_head, NULL), g_bytes_get_size(single->inner_head), "hello xz\n", 9);
    GByteArray* tar = sample_tar();
    gchar* tar_path = write_file("t.tar", tar->data, tar->len);
    gchar* cmd2 = g_strdup_printf("xz -k -f %s", tar_path);
    g_assert_true(g_spawn_command_line_sync(cmd2, NULL, NULL, NULL, NULL));
    gchar* txz = g_strconcat(tar_path, ".xz", NULL);
    g_autoptr(ArchiveListing) l = archive_list(txz, 100, NULL, NULL);
    g_assert_nonnull(l);
    g_assert_cmpstr(l->format, ==, "xz-compressed tar archive");
    g_assert_cmpuint(l->n_entries, ==, 4);
    g_assert_true(archive_listing_can_list((const guchar*)"\xFD" "7zXZ", 6));
    g_remove(txz);
    g_remove(tar_path);
    g_remove(xz_path);
    g_remove(text);
    g_byte_array_unref(tar);
    g_free(txz);
    g_free(cmd2);
    g_free(tar_path);
    g_free(xz_path);
    g_free(cmd);
    g_free(text);
    g_free(xz);
#else
    gchar* path = write_file("a.xz", "\xFD" "7zXZ\0\0\0\0", 10);
    GError* error = NULL;
    g_assert_null(archive_list(path, 100, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_assert_cmpstr(error->message, ==, "xz-compressed file");
    g_error_free(error);
    g_assert_false(archive_listing_can_list((const guchar*)"\xFD" "7zXZ", 6));
    g_remove(path);
    g_free(path);
#endif
    g_assert_true(archive_listing_can_list((const guchar*)"\x1F\x8B\x08", 3));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    tmp_dir = g_dir_make_tmp("archive-XXXXXX", NULL);
    g_test_add_func("/archive/zip", test_zip);
    g_test_add_func("/archive/tar", test_tar);
    g_test_add_func("/archive/gzip_single", test_gzip_single);
    g_test_add_func("/archive/ar", test_ar);
    g_test_add_func("/archive/other_compressions", test_other_compressions);
    int status = g_test_run();
    g_rmdir(tmp_dir);
    g_free(tmp_dir);
    return status;
}
