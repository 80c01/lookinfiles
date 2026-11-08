/* Unit tests for src/media/media_tracks.c (track labels, subtitle files). */
#include "media/media_tracks.h"

static void check_label(const gchar* language, const gchar* title, guint index, const gchar* expected) {
    gchar* label = media_track_label(language, title, index);
    g_assert_cmpstr(label, ==, expected);
    g_free(label);
}

static void test_label(void) {
    check_label("English", NULL, 0, "English");
    check_label("English", "Commentary", 1, "English - Commentary");
    check_label("English", "english", 1, "English");
    check_label("und", "Director", 2, "Director");
    check_label("", "", 2, "Track 3");
    check_label(NULL, NULL, 0, "Track 1");
    check_label("French", "Audio", 1, "French");
    check_label(NULL, "Subtitle", 0, "Track 1");
}

static void test_subtitle_files(void) {
    const gchar* names[] = {"Movie.mkv", "Movie.srt", "Movie.fr.srt", "Movie.en.ASS", "Movie.en.idx",
                            "Movie 2.srt", "Moviesrt", "Other.srt", "Movie.vtt", "Movie.mkv.srt", NULL};
    gchar** subs = media_subtitle_files("Movie.mkv", names);
    const gchar* expected[] = {"Movie.srt", "Movie.vtt", "Movie.en.ASS", "Movie.fr.srt", "Movie.mkv.srt", NULL};
    g_assert_cmpuint(g_strv_length(subs), ==, g_strv_length((gchar**)expected));
    for (guint i = 0; expected[i] != NULL; i++) g_assert_cmpstr(subs[i], ==, expected[i]);
    g_strfreev(subs);

    const gchar* none[] = {"a.txt", NULL};
    subs = media_subtitle_files("Movie.mkv", none);
    g_assert_cmpuint(g_strv_length(subs), ==, 0);
    g_strfreev(subs);

    subs = media_subtitle_files("clip", (const gchar* const[]){"clip.srt", NULL});
    g_assert_cmpstr(subs[0], ==, "clip.srt");
    g_strfreev(subs);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/media_tracks/label", test_label);
    g_test_add_func("/media_tracks/subtitle_files", test_subtitle_files);
    return g_test_run();
}
