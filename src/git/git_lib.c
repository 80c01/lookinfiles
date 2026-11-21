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
