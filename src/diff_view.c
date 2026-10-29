/* Unified diff preview (see diff_view.h). */
#include "diff_view.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
    LINE_TEXT,       /* outside any file: commit message, email headers */
    LINE_FILE,       /* diff --git, Index:, diff -u */
    LINE_META,       /* index, mode, rename, similarity, ---, +++ */
    LINE_HUNK,       /* @@ ... @@ */
    LINE_CONTEXT,
    LINE_ADDED,
    LINE_REMOVED,
    LINE_NO_NEWLINE, /* \ No newline at end of file */
    LINE_BINARY,     /* Binary files a and b differ */
} LineType;

typedef struct {
    gint old_left;   /* lines of the current hunk still expected */
    gint new_left;
    gboolean in_file;
} DiffState;

/* "@@ -a,b +c,d @@": the old and new line counts (1 when omitted) */
static gboolean parse_hunk(const gchar* line, gint* old_count, gint* new_count) {
    if (!g_str_has_prefix(line, "@@ -")) {
        return FALSE;
    }
    const gchar* p = line + 4;
    gchar* end = NULL;
    strtol(p, &end, 10);
    *old_count = 1;
    if (*end == ',') {
        *old_count = (gint)strtol(end + 1, &end, 10);
    }
    p = strstr(end, " +");
    if (p == NULL) {
        return FALSE;
    }
    strtol(p + 2, &end, 10);
    *new_count = 1;
    if (*end == ',') {
        *new_count = (gint)strtol(end + 1, &end, 10);
    }
    return TRUE;
}

static LineType classify(DiffState* st, const gchar* line) {
    if (st->old_left > 0 || st->new_left > 0) {
        if (line[0] == '+') {
            st->new_left--;
            return LINE_ADDED;
        }
        if (line[0] == '-') {
            st->old_left--;
            return LINE_REMOVED;
        }
        if (line[0] == ' ' || line[0] == '\0') {
            st->old_left--;
            st->new_left--;
            return LINE_CONTEXT;
        }
        if (line[0] == '\\') {
            return LINE_NO_NEWLINE;
        }
        st->old_left = st->new_left = 0; /* hunk shorter than announced */
    }
    gint old_count, new_count;
    if (parse_hunk(line, &old_count, &new_count)) {
        st->old_left = old_count;
        st->new_left = new_count;
        st->in_file = TRUE;
        return LINE_HUNK;
    }
    if (line[0] == '\\') {
        return LINE_NO_NEWLINE;
    }
    if (g_str_has_prefix(line, "diff ") || g_str_has_prefix(line, "Index: ")) {
        st->in_file = TRUE;
        return LINE_FILE;
    }
    if (g_str_has_prefix(line, "--- ") || g_str_has_prefix(line, "+++ ")) {
        st->in_file = TRUE;
        return LINE_META;
    }
    if (g_str_has_prefix(line, "Binary files ") || g_str_has_prefix(line, "GIT binary patch")) {
        return LINE_BINARY;
    }
    if (st->in_file &&
        (g_str_has_prefix(line, "index ") || g_str_has_prefix(line, "new file mode") ||
         g_str_has_prefix(line, "deleted file mode") || g_str_has_prefix(line, "old mode") ||
         g_str_has_prefix(line, "new mode") || g_str_has_prefix(line, "similarity index") ||
         g_str_has_prefix(line, "dissimilarity index") || g_str_has_prefix(line, "rename ") ||
         g_str_has_prefix(line, "copy ") || g_str_has_prefix(line, "===="))) {
        return LINE_META;
    }
    if (g_str_has_prefix(line, "From ") || g_str_has_prefix(line, "-- ")) {
        st->in_file = FALSE; /* next patch of a series, or the signature */
    }
    return LINE_TEXT;
}

/* Path of a "--- a/x" / "+++ b/x" line: prefix stripped, date after a tab dropped */
static gchar* header_path(const gchar* line) {
    const gchar* p = line + 4;
    gsize n = strcspn(p, "\t");
    gchar* path = g_strndup(p, n);
    g_strchomp(path);
    if ((path[0] == 'a' || path[0] == 'b') && path[1] == '/') {
        memmove(path, path + 2, strlen(path + 2) + 1);
    }
    return path;
}

/* Path of "diff --git a/x b/y" (y) or "Index: x" */
static gchar* file_line_path(const gchar* line) {
    if (g_str_has_prefix(line, "Index: ")) {
        return g_strstrip(g_strdup(line + 7));
    }
    const gchar* b = strstr(line, " b/");
    if (g_str_has_prefix(line, "diff --git ") && b != NULL) {
        return g_strdup(b + 3);
    }
    const gchar* last = strrchr(line, ' ');
    return g_strdup(last != NULL ? last + 1 : line);
}

static DiffFileStat* current_file(DiffSummary* s) {
    return s->files->len > 0 ? &g_array_index(s->files, DiffFileStat, s->files->len - 1) : NULL;
}

static void add_file(DiffSummary* s, gchar* path) {
    DiffFileStat f = { path, 0, 0, FALSE };
    g_array_append_val(s->files, f);
}

DiffSummary* diff_summarize(const gchar* text, gsize len) {
    DiffSummary* s = g_new0(DiffSummary, 1);
    s->files = g_array_new(FALSE, TRUE, sizeof(DiffFileStat));
    DiffState st = { 0, 0, FALSE };
    gboolean hunks = FALSE;
    gboolean file_from_header = FALSE; /* current file came from diff/Index: */
    gchar** lines = g_strsplit(len > 0 ? text : "", "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = lines[i];
        gsize n = strlen(line);
        if (n > 0 && line[n - 1] == '\r') {
            line[n - 1] = '\0';
        }
        LineType type = classify(&st, line);
        DiffFileStat* f = current_file(s);
        switch (type) {
        case LINE_FILE:
            add_file(s, file_line_path(line));
            file_from_header = TRUE;
            break;
        case LINE_META:
            if (g_str_has_prefix(line, "--- ") && !file_from_header) {
                add_file(s, header_path(line));
            } else if (g_str_has_prefix(line, "+++ ") && f != NULL) {
                gchar* path = header_path(line);
                if (g_strcmp0(path, "/dev/null") != 0) {
                    g_free(f->path);
                    f->path = path;
                } else {
                    g_free(path);
                }
                file_from_header = FALSE;
            }
            break;
        case LINE_HUNK:
            hunks = TRUE;
            file_from_header = FALSE;
            break;
        case LINE_ADDED:
            if (f != NULL) {
                f->added++;
            }
            s->added++;
            break;
        case LINE_REMOVED:
            if (f != NULL) {
                f->removed++;
            }
            s->removed++;
            break;
        case LINE_BINARY:
            if (f != NULL) {
                f->binary = TRUE;
            }
            break;
        case LINE_TEXT:
            if (s->subject == NULL && g_str_has_prefix(line, "Subject: ")) {
                s->subject = g_strdup(line + 9);
            }
            break;
        default:
            break;
        }
    }
    g_strfreev(lines);
    gboolean binary = FALSE;
    for (guint i = 0; i < s->files->len; i++) {
        binary |= g_array_index(s->files, DiffFileStat, i).binary;
    }
    if (s->files->len == 0 || (!hunks && !binary)) {
        diff_summary_free(s);
        return NULL;
    }
    return s;
}

void diff_summary_free(DiffSummary* summary) {
    if (summary == NULL) {
        return;
    }
    for (guint i = 0; i < summary->files->len; i++) {
        g_free(g_array_index(summary->files, DiffFileStat, i).path);
    }
    g_array_free(summary->files, TRUE);
    g_free(summary->subject);
    g_free(summary);
}

static void append_summary(PreviewDoc* doc, const DiffSummary* s) {
    if (s->subject != NULL) {
        preview_doc_append(doc, PREVIEW_STYLE_TITLE, s->subject);
        preview_doc_append_len(doc, "\n", 1);
    }
    gint start = preview_doc_offset(doc);
    preview_doc_appendf(doc, PREVIEW_STYLE_PLAIN, "%u %s changed, ", s->files->len,
                        s->files->len == 1 ? "file" : "files");
    preview_doc_appendf(doc, PREVIEW_STYLE_SUCCESS, "+%u", s->added);
    preview_doc_append(doc, PREVIEW_STYLE_PLAIN, " ");
    preview_doc_appendf(doc, PREVIEW_STYLE_ERROR, "−%u", s->removed);
    preview_doc_append_len(doc, "\n", 1);
    preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_BOLD);
    guint width = 0;
    for (guint i = 0; i < s->files->len; i++) {
        width = MAX(width, preview_text_width(g_array_index(s->files, DiffFileStat, i).path));
    }
    width = MIN(width, 60u);
    for (guint i = 0; i < s->files->len; i++) {
        const DiffFileStat* f = &g_array_index(s->files, DiffFileStat, i);
        preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "  ");
        preview_doc_append(doc, PREVIEW_STYLE_FILE, f->path);
        for (guint w = preview_text_width(f->path); w < width + 2; w++) {
            preview_doc_append_len(doc, " ", 1);
        }
        if (f->binary) {
            preview_doc_append(doc, PREVIEW_STYLE_DIM, "binary");
        } else {
            preview_doc_appendf(doc, PREVIEW_STYLE_SUCCESS, "+%u", f->added);
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, " ");
            preview_doc_appendf(doc, PREVIEW_STYLE_ERROR, "−%u", f->removed);
        }
        preview_doc_append_len(doc, "\n", 1);
    }
    preview_doc_append_len(doc, "\n", 1);
}

gboolean diff_view_append(PreviewDoc* doc, const gchar* text, gsize len) {
    g_autoptr(DiffSummary) summary = diff_summarize(text, len);
    if (summary == NULL) {
        return FALSE;
    }
    append_summary(doc, summary);

    DiffState st = { 0, 0, FALSE };
    gchar** lines = g_strsplit(text, "\n", -1);
    gint n_lines = (gint)g_strv_length(lines);
    if (n_lines > 0 && lines[n_lines - 1][0] == '\0') {
        n_lines--; /* text ends with a line break */
    }
    for (gint i = 0; i < n_lines; i++) {
        gchar* line = lines[i];
        gsize n = strlen(line);
        if (n > 0 && line[n - 1] == '\r') {
            line[n - 1] = '\0';
        }
        LineType type = classify(&st, line);
        PreviewStyle style = PREVIEW_STYLE_PLAIN;
        switch (type) {
        case LINE_FILE:
            if (i > 0 && lines[i - 1][0] != '\0') {
                preview_doc_append_len(doc, "\n", 1);
            }
            style = PREVIEW_STYLE_FILE;
            break;
        case LINE_META:
        case LINE_NO_NEWLINE:
        case LINE_BINARY:
            style = PREVIEW_STYLE_DIM;
            break;
        case LINE_HUNK:
            style = PREVIEW_STYLE_HUNK;
            break;
        case LINE_ADDED:
            style = PREVIEW_STYLE_ADDED;
            break;
        case LINE_REMOVED:
            style = PREVIEW_STYLE_REMOVED;
            break;
        case LINE_TEXT:
            if (g_str_has_prefix(line, "Subject: ")) {
                style = PREVIEW_STYLE_BOLD;
            } else if (g_str_has_prefix(line, "From ") || g_str_has_prefix(line, "From: ") ||
                       g_str_has_prefix(line, "Date: ")) {
                style = PREVIEW_STYLE_DIM;
            }
            break;
        default:
            break;
        }
        /* Whole-line backgrounds cover the line break too */
        gint start = preview_doc_offset(doc);
        preview_doc_append_len(doc, line, -1);
        preview_doc_append_len(doc, "\n", 1);
        preview_doc_add_span(doc, start, preview_doc_offset(doc), style);
    }
    g_strfreev(lines);
    return TRUE;
}
