/* Unit tests of the performance probe (src/core/profile.c) */
#include "core/profile.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_frame_stats(void) {
    ProfileFrameStats s = {0};
    profile_frame_stats_add(&s, 4000, TRUE);
    profile_frame_stats_add(&s, PROFILE_SLOW_FRAME_US, TRUE);     /* not over: not slow */
    profile_frame_stats_add(&s, PROFILE_SLOW_FRAME_US + 1, TRUE); /* slow */
    profile_frame_stats_add(&s, 2000, FALSE);                      /* nothing drawn */
    g_assert_cmpuint(s.count, ==, 3);
    g_assert_cmpuint(s.unpainted, ==, 1);
    g_assert_cmpuint(s.slow, ==, 1);
    g_assert_cmpint(s.worst_us, ==, PROFILE_SLOW_FRAME_US + 1);
    g_assert_cmpint(s.total_us, ==, 4000 + PROFILE_SLOW_FRAME_US + PROFILE_SLOW_FRAME_US + 1 + 2000);
}

static void test_parse_status(void) {
    gint64 rss = 0, peak = 0;
    const gchar* text = "Name:\tlookinfiles\nVmPeak:\t  900000 kB\nVmHWM:\t   61234 kB\n"
                        "VmRSS:\t   51200 kB\nThreads:\t8\n";
    g_assert_true(profile_parse_status(text, &rss, &peak));
    g_assert_cmpint(rss, ==, 51200);
    g_assert_cmpint(peak, ==, 61234);

    /* VmRSSX is not VmRSS; no peak */
    g_assert_false(profile_parse_status("VmRSSX: 5 kB\n", &rss, &peak));
    g_assert_cmpint(rss, ==, -1);
    g_assert_cmpint(peak, ==, -1);
    g_assert_false(profile_parse_status(NULL, &rss, &peak));
}

static void test_cpu_and_memory(void) {
#ifdef G_OS_UNIX
    g_assert_cmpint(profile_cpu_us(), >=, 0);
#endif
#ifdef __linux__
    gint64 rss = 0, peak = 0;
    g_assert_true(profile_memory(&rss, &peak));
    g_assert_cmpint(rss, >, 0);
    g_assert_cmpint(peak, >=, rss);
#endif
}

/* Lines written to the file, in order */
static gchar** read_lines(const gchar* path) {
    gchar* text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    gchar** lines = g_strsplit(text, "\n", -1);
    g_free(text);
    return lines;
}

static void test_output(void) {
    gchar* dir = g_dir_make_tmp("test_profile_XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar* path = g_build_filename(dir, "profile.log", NULL);

    g_assert_false(profile_enabled());
    g_assert_cmpint(profile_start(), ==, 0); /* off: nothing measured */
    profile_mark("ignored");

    profile_init_file(path);
    profile_init_file(NULL); /* only the first call counts */
    g_assert_true(profile_enabled());
    profile_mark("first step");
    gint64 start = profile_start();
    g_assert_cmpint(start, >, 0);
    profile_done(start, "scan %s", "dir");
    profile_done(0, "never written");
    profile_frame(1000, TRUE, NULL);                       /* start-up frame */
    profile_startup_done();
    profile_startup_done();                    /* once */
    ProfileFramePhases phases = {1000, 5000, PROFILE_SLOW_FRAME_US - 6000 + 1};
    profile_frame(PROFILE_SLOW_FRAME_US + 1, TRUE, &phases); /* slow, after the start-up */
    profile_frame(2000, TRUE, &phases); /* fast: no phases shown */
    profile_frame(500, FALSE, NULL);
    profile_log("note %d", 7);
    profile_report();

    gchar** lines = read_lines(path);
    g_assert_cmpuint(g_strv_length(lines), ==, 10); /* 9 lines + the empty end */
    g_assert_true(g_str_has_prefix(lines[0], "profile: mark "));
    g_assert_true(g_str_has_suffix(lines[0], " first step"));
    g_assert_true(g_str_has_prefix(lines[1], "profile: time "));
    g_assert_true(g_str_has_suffix(lines[1], " ms scan dir"));
    g_assert_true(g_str_has_prefix(lines[2], "profile: frame "));
    g_assert_true(g_str_has_suffix(lines[2], " ms"));                   /* fast, painted */
    g_assert_true(g_str_has_suffix(lines[3], " startup done"));
    g_assert_true(g_str_has_suffix(lines[4], " ms slow (update 1.0, layout 5.0, paint 10.7)"));
    g_assert_true(g_str_has_prefix(lines[5], "profile: frame "));
    g_assert_true(g_str_has_suffix(lines[5], " ms"));
    g_assert_true(g_str_has_suffix(lines[6], " ms unpainted"));
    g_assert_nonnull(strstr(lines[6], " ms at "));
    g_assert_cmpstr(lines[7], ==, "profile: note 7");
    g_assert_true(g_str_has_prefix(lines[8], "profile: report "));
    g_assert_nonnull(strstr(lines[8], " startup_frames=1 startup_slow=0 "));
    g_assert_nonnull(strstr(lines[8], " frames=2 unpainted=1 slow=1 "));
    g_assert_cmpstr(lines[9], ==, "");
    g_strfreev(lines);

    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/profile/frame_stats", test_frame_stats);
    g_test_add_func("/profile/parse_status", test_parse_status);
    g_test_add_func("/profile/cpu_and_memory", test_cpu_and_memory);
    g_test_add_func("/profile/output", test_output);
    return g_test_run();
}
