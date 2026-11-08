/* Unit tests for src/formats/torrent_info.c (BitTorrent metainfo preview). */
#include "formats/torrent_info.h"

#include <string.h>

static void test_torrent_multi_file(void) {
    const gchar sample[] =
        "d8:announce35:http://tracker.example.com/announce"
        "13:announce-listll35:http://tracker.example.com/announceel26:udp://tracker2.example.comee"
        "7:comment15:A test download"
        "10:created by13:TestTorrent/1"
        "13:creation datei1700000000e"
        "4:infod"
          "5:filesl"
            "d6:lengthi1048576e4:pathl4:docs8:read.txtee"
            "d6:lengthi2097152e4:pathl3:bin7:app.binee"
          "e"
          "4:name9:test_data"
          "12:piece lengthi262144e"
          "6:pieces20:12345678901234567890"
        "ee";

    GError* error = NULL;
    TorrentInfo* info = torrent_info_parse((const guchar*)sample, strlen(sample), &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpstr(info->name, ==, "test_data");
    g_assert_cmpint(info->total_size, ==, 3145728);
    g_assert_cmpint(info->piece_length, ==, 262144);
    g_assert_cmpint(info->n_pieces, ==, 1);
    g_assert_cmpint(info->creation_date, ==, 1700000000);
    g_assert_cmpstr(info->created_by, ==, "TestTorrent/1");
    g_assert_cmpstr(info->comment, ==, "A test download");

    g_assert_cmpint(info->trackers->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(info->trackers, 0), ==, "http://tracker.example.com/announce");
    g_assert_cmpstr(g_ptr_array_index(info->trackers, 1), ==, "udp://tracker2.example.com");

    g_assert_cmpint(info->files->len, ==, 2);
    const TorrentFile* f0 = &g_array_index(info->files, TorrentFile, 0);
    g_assert_cmpstr(f0->path, ==, "docs/read.txt");
    g_assert_cmpint(f0->size, ==, 1048576);

    const TorrentFile* f1 = &g_array_index(info->files, TorrentFile, 1);
    g_assert_cmpstr(f1->path, ==, "bin/app.bin");
    g_assert_cmpint(f1->size, ==, 2097152);

    torrent_info_free(info);
}

static void test_torrent_single_file_and_doc(void) {
    const gchar sample[] =
        "d8:announce26:http://tracker.example.com"
        "4:infod6:lengthi512e4:name8:file.iso12:piece lengthi256e6:pieces40:1234567890123456789012345678901234567890ee";

    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = torrent_info_append(doc, (const guchar*)sample, strlen(sample), &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    const gchar* text = doc->text->str;
    g_assert_true(strstr(text, "file.iso") != NULL);
    g_assert_true(strstr(text, "512 bytes") != NULL || strstr(text, "512 bytes") != NULL || strstr(text, "512") != NULL);
    g_assert_true(strstr(text, "http://tracker.example.com") != NULL);
    g_assert_true(strstr(text, "2 pieces") != NULL);

    preview_doc_free(doc);
}

static void test_torrent_invalid(void) {
    GError* error = NULL;
    g_assert_null(torrent_info_parse(NULL, 0, &error));
    g_clear_error(&error);

    const gchar bad[] = "not a bencoded dict";
    g_assert_null(torrent_info_parse((const guchar*)bad, strlen(bad), &error));
    g_clear_error(&error);

    const gchar no_info[] = "d8:announce26:http://tracker.example.come";
    g_assert_null(torrent_info_parse((const guchar*)no_info, strlen(no_info), &error));
    g_clear_error(&error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/torrent_info/multi_file", test_torrent_multi_file);
    g_test_add_func("/torrent_info/single_file_and_doc", test_torrent_single_file_and_doc);
    g_test_add_func("/torrent_info/invalid", test_torrent_invalid);
    return g_test_run();
}
