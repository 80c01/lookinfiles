#include "git/git_lib.h"

void git_lib_ready(void) {
    static gsize done = 0;
    if (g_once_init_enter(&done)) {
        git_libgit2_init();
        g_once_init_leave(&done, 1);
    }
}

git_repository* git_lib_open(const gchar* root) {
    if (root == NULL) {
        return NULL;
    }
    git_lib_ready();
    git_repository* repo = NULL;
    if (git_repository_open_ext(&repo, root, GIT_REPOSITORY_OPEN_NO_SEARCH, NULL) != 0) {
        return NULL;
    }
    if (git_repository_is_bare(repo)) {
        git_repository_free(repo);
        return NULL;
    }
    return repo;
}

int git_lib_patch_line(const git_diff_delta* delta, const git_diff_hunk* hunk, const git_diff_line* line,
                       void* data) {
    (void)hunk;
    GString* out = data;
    switch (line->origin) {
    case GIT_DIFF_LINE_CONTEXT:
    case GIT_DIFF_LINE_ADDITION:
    case GIT_DIFF_LINE_DELETION:
        g_string_append_c(out, line->origin);
        g_string_append_len(out, line->content, (gssize)line->content_len);
        break;
    case GIT_DIFF_LINE_HUNK_HDR: {
        gsize len = line->content_len;
        while (len > 0 && g_ascii_isspace(line->content[len - 1])) {
            len--;
        }
        g_string_append_len(out, line->content, (gssize)len);
        g_string_append_c(out, '\n');
        break;
    }
    case GIT_DIFF_LINE_FILE_HDR:
        if (delta->old_file.size == 0 && delta->new_file.size == 0) {
            /* No content on either side: git prints no "---" / "+++" lines */
            gchar* hdr = g_strndup(line->content, line->content_len);
            gchar** rows = g_strsplit(hdr, "\n", -1);
            for (gint i = 0; rows[i] != NULL && rows[i][0] != '\0'; i++) {
                if (!g_str_has_prefix(rows[i], "--- ") && !g_str_has_prefix(rows[i], "+++ ")) {
                    g_string_append_printf(out, "%s\n", rows[i]);
                }
            }
            g_strfreev(rows);
            g_free(hdr);
            break;
        }
        g_string_append_len(out, line->content, (gssize)line->content_len);
        break;
    default:
        g_string_append_len(out, line->content, (gssize)line->content_len);
        break;
    }
    return 0;
}

gchar* git_lib_short_id(const git_object* obj) {
    git_buf buf = {0};
    gchar* id = NULL;
    if (git_object_short_id(&buf, obj) == 0 && buf.ptr != NULL) {
        id = g_strndup(buf.ptr, buf.size);
    } else {
        id = g_strndup(git_oid_tostr_s(git_object_id(obj)), 7);
    }
    git_buf_dispose(&buf);
    return id;
}

/* git's default date format, in the signature's time zone and in English
 * whatever the locale: "Thu Nov 5 14:03:12 2026 +0100" */
gchar* git_lib_signature_date(const git_time* when) {
    static const gchar* const DAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const gchar* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    GDateTime* utc = g_date_time_new_from_unix_utc((gint64)when->time);
    if (utc == NULL) {
        return g_strdup("");
    }
    GTimeZone* tz = g_time_zone_new_offset(when->offset * 60);
    GDateTime* t = g_date_time_to_timezone(utc, tz);
    gint offset = when->offset < 0 ? -when->offset : when->offset;
    gchar* text = g_strdup_printf("%s %s %d %02d:%02d:%02d %d %c%02d%02d",
                                  DAYS[g_date_time_get_day_of_week(t) - 1],
                                  MONTHS[g_date_time_get_month(t) - 1],
                                  g_date_time_get_day_of_month(t), g_date_time_get_hour(t),
                                  g_date_time_get_minute(t), g_date_time_get_second(t),
                                  g_date_time_get_year(t), when->offset < 0 ? '-' : '+',
                                  offset / 60, offset % 60);
    g_date_time_unref(t);
    g_time_zone_unref(tz);
    g_date_time_unref(utc);
    return text;
}
