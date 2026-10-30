/* Unit tests for src/media_tags.c (ID3, FLAC, MP4 tags and audio details). */
#include "media_tags.h"

#include <math.h>
#include <string.h>

static MediaTags* read_fixture(const gchar* name) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", name, NULL);
    MediaTags* t = media_tags_read_file(path);
    g_assert_nonnull(t);
    g_free(path);
    return t;
}

static void test_mp3(void) {
    g_autoptr(MediaTags) t = read_fixture("song.mp3");
    g_assert_cmpstr(t->title, ==, "Northern Lights");
    g_assert_cmpstr(t->artist, ==, "Ada & The Engines");
    g_assert_cmpstr(t->album, ==, "Analytical");
    g_assert_cmpstr(t->year, ==, "1843");
    g_assert_cmpstr(t->track, ==, "3/12");
    g_assert_cmpstr(t->genre, ==, "Ambient");
    g_assert_nonnull(t->cover);
    g_assert_cmpstr(t->codec, ==, "MP3");
    g_assert_cmpuint(t->bitrate, ==, 128);
    g_assert_cmpuint(t->sample_rate, ==, 44100);
    g_assert_cmpuint(t->channels, ==, 2);
    g_assert_cmpfloat_with_epsilon(t->duration, 100 * 417 * 8 / 128000.0, 0.01);
    gchar* line = media_tags_technical_line(t);
    g_assert_cmpstr(line, ==, "MP3 · 128 kbit/s · 44.1 kHz · stereo · 0:03");
    g_free(line);
}

static void test_flac(void) {
    g_autoptr(MediaTags) t = read_fixture("waltz.flac");
    g_assert_cmpstr(t->title, ==, "Analytical Waltz");
    g_assert_cmpstr(t->artist, ==, "Ada Lovelace");
    g_assert_cmpstr(t->track, ==, "1");
    g_assert_cmpuint(t->sample_rate, ==, 48000);
    g_assert_cmpuint(t->bits_per_sample, ==, 24);
    g_assert_cmpfloat_with_epsilon(t->duration, 125.0, 1e-9);
    g_assert_nonnull(t->cover);
    gchar* line = media_tags_technical_line(t);
    g_assert_cmpstr(line, ==, "FLAC · 48 kHz · 24-bit · stereo · 2:05");
    g_free(line);
}

static void test_m4a(void) {
    g_autoptr(MediaTags) t = read_fixture("tide.m4a");
    g_assert_cmpstr(t->title, ==, "Morning Tide");
    g_assert_cmpstr(t->artist, ==, "Grace");
    g_assert_cmpstr(t->track, ==, "7/9");
    g_assert_cmpstr(t->codec, ==, "AAC");
    g_assert_cmpuint(t->sample_rate, ==, 44100);
    g_assert_cmpfloat_with_epsilon(t->duration, 215.5, 1e-9);
    g_assert_nonnull(t->cover);
}

static void test_durations(void) {
    gchar* a = media_tags_format_duration(65.4);
    gchar* b = media_tags_format_duration(3723);
    g_assert_cmpstr(a, ==, "1:05");
    g_assert_cmpstr(b, ==, "1:02:03");
    g_free(a);
    g_free(b);
    g_assert_null(media_tags_read_file("tests/fixtures/preview/sample.csv"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/media_tags/mp3", test_mp3);
    g_test_add_func("/media_tags/flac", test_flac);
    g_test_add_func("/media_tags/m4a", test_m4a);
    g_test_add_func("/media_tags/durations", test_durations);
    return g_test_run();
}
