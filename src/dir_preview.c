#include "dir_preview.h"
#include "git/git_state.h"
#include "search.h"
#include "core/i18n.h"
#include "theme/fonts.h"
#include "theme/palette.h"
#include "theme/text_tags.h"
#include "dir_summary.h"
#include "theme/text_style.h"
#include "git/git_utils.h"
#include "mail_dir.h"
#include "preview_doc_view.h"
#include "markdown.h"
#include "orgmode.h"
#include "preview_cache.h"
#include "theme/icons.h"

#include <stdio.h>

/* Wait before reading: holding j/k through folders starts no work until the
 * selection stops on one. */
#define DIR_PREVIEW_DELAY_MS 50
/* Table rows shown at most */
#define DIR_PREVIEW_ROWS 200
/* Name column width in characters (longer names are cut with "…") */
#define DIR_PREVIEW_NAME_MIN 8
#define DIR_PREVIEW_NAME_MAX 40

typedef struct {
    AppData* app;
    GCancellable* cancellable;
    gchar* name;
    gchar* full;
    gboolean show_hidden;
    DirSummarySort sort;
    DirSummary* summary;   /* filled by the worker thread */
    gboolean unreadable;
    gchar* branch;         /* git branch, NULL outside a repository */
    gchar* readme_path;    /* README shown under the table, NULL if none */
    gchar* readme_text;    /* its first PREVIEW_BYTES, valid UTF-8 */
    gint64 readme_size;
    PreviewDoc* mail;      /* Maildir / MH folder: its message table instead */
    gboolean cached_shown; /* the cached overview was shown at the selection */
} DirPreviewJob;

static void job_free(gpointer data) {
    DirPreviewJob* job = data;
    if (job == NULL) {
        return;
    }
    g_clear_object(&job->cancellable);
    g_free(job->name);
    g_free(job->full);
    dir_summary_free(job->summary);
    g_free(job->branch);
    g_free(job->readme_path);
    g_free(job->readme_text);
    preview_doc_free(job->mail);
    g_free(job);
}

static DirSummarySort summary_sort(gint sort_mode) {
    switch (sort_mode) {
    case SORT_DATE: return DIR_SUMMARY_SORT_DATE;
    case SORT_SIZE: return DIR_SUMMARY_SORT_SIZE;
    case SORT_EXT:  return DIR_SUMMARY_SORT_EXT;
    default:        return DIR_SUMMARY_SORT_NAME;
    }
}

/* First PREVIEW_BYTES of the README (never the whole file), as valid UTF-8 */
static void read_readme(DirPreviewJob* job) {
    job->readme_path = g_build_filename(job->full, job->summary->readme, NULL);
    FILE* f = fopen(job->readme_path, "rb");
    if (f == NULL) {
        g_clear_pointer(&job->readme_path, g_free);
        return;
    }
    gchar* buf = g_malloc(PREVIEW_BYTES + 1);
    gsize len = fread(buf, 1, PREVIEW_BYTES, f);
    buf[len] = '\0';
    if (fseek(f, 0, SEEK_END) == 0) {
        job->readme_size = (gint64)ftell(f);
    }
    fclose(f);
    job->readme_text = g_utf8_make_valid(buf, (gssize)len);
    g_free(buf);
}

/* Worker thread: one enumeration of the directory (type, size and date come
 * with the names, no extra stat per entry), never recursive. */
static void job_thread(GTask* task, gpointer source_object, gpointer task_data,
                       GCancellable* cancellable) {
    (void)source_object;
    DirPreviewJob* job = task_data;

    job->summary = dir_summary_new();
    /* Mail folders: the messages, not the cur / new / tmp listing */
    job->mail = mail_dir_preview(job->full, job->name, DIR_PREVIEW_ROWS, cancellable);
    if (job->mail != NULL) {
        if (g_task_return_error_if_cancelled(task)) return;
        g_task_return_boolean(task, TRUE);
        return;
    }
    GFile* dir = g_file_new_for_path(job->full);
    GFileEnumerator* en = g_file_enumerate_children(
        dir,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE, cancellable, NULL);
    g_object_unref(dir);
    if (en == NULL) {
        job->unreadable = TRUE;
    } else {
        GFileInfo* info;
        while ((info = g_file_enumerator_next_file(en, cancellable, NULL)) != NULL) {
            const gchar* raw = g_file_info_get_attribute_byte_string(info, G_FILE_ATTRIBUTE_STANDARD_NAME);
            if (raw != NULL && (job->show_hidden || raw[0] != '.')) {
                gchar* name = g_filename_display_name(raw);
                gboolean is_dir = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_STANDARD_TYPE) ==
                                  G_FILE_TYPE_DIRECTORY;
                guint64 size = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
                gint64 mtime = (gint64)g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
                gboolean added = dir_summary_add(job->summary, name, is_dir, size, mtime);
                g_free(name);
                if (!added) {
                    g_object_unref(info);
                    break;
                }
            }
            g_object_unref(info);
        }
        g_object_unref(en);
        dir_summary_sort(job->summary, job->sort);
        if (job->summary->readme != NULL && !g_cancellable_is_cancelled(cancellable)) {
            read_readme(job);
        }
        if (!g_cancellable_is_cancelled(cancellable)) {
            job->branch = git_read_branch(job->full); /* reads .git/HEAD, no subprocess */
        }
    }

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

static void ensure_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);
    if (gtk_text_tag_table_lookup(tt, "dir_title") != NULL) {
        return;
    }
    text_tag_create(buffer, "dir_title",
                               "weight", 600, "scale", FONT_SCALE_TITLE, "foreground", "@fg_strong",
                               "pixels-below-lines", 2, NULL);
    text_tag_create(buffer, "dir_dim", "foreground", "@fg_subtle", NULL);
    text_tag_create(buffer, "dir_table",
                               "family", "@font_code", "wrap-mode", GTK_WRAP_NONE, NULL);
    gtk_text_buffer_create_tag(buffer, "dir_folder", "weight", 600, NULL);
    text_tag_create(buffer, "dir_section",
                               "weight", 600, "foreground", "@fg_strong",
                               "paragraph-background", "@bg_block",
                               "pixels-above-lines", 6, "pixels-below-lines", 6, NULL);
    text_tag_create(buffer, "dir_plain", "family", "@font_code", NULL);
    /* Git status colours, as in the explorer tree */
    static const struct { const gchar* name; PaletteRole role; } git_tags[] = {
        { "dir_git_M", PAL_GIT_MODIFIED }, { "dir_git_U", PAL_GIT_ADDED }, { "dir_git_A", PAL_GIT_ADDED },
        { "dir_git_D", PAL_GIT_DELETED }, { "dir_git_R", PAL_GIT_RENAMED }, { "dir_git_!", PAL_GIT_CONFLICT },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(git_tags); i++) {
        gchar* role = g_strdup_printf("@%s", palette_name(git_tags[i].role));
        text_tag_create(buffer, git_tags[i].name, "foreground", role, NULL);
        g_free(role);
    }
}

/* Tag colouring a git status letter, NULL if none */
static const gchar* git_tag(gchar letter) {
    switch (letter) {
    case 'M': return "dir_git_M";
    case 'U': return "dir_git_U";
    case 'A': return "dir_git_A";
    case 'D': return "dir_git_D";
    case 'R': return "dir_git_R";
    case '!': return "dir_git_!";
    default:  return NULL;
    }
}

static void insert_tagged(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* text,
                          const gchar* tag1, const gchar* tag2) {
    gtk_text_buffer_insert_with_tags_by_name(buffer, it, text, -1, tag1, tag2, NULL);
}

/* "  <file-type icon> " in the table (folders get the folder glyph) */
static void insert_icon(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* name, gboolean is_dir) {
    insert_tagged(buffer, it, "  ", "dir_table", NULL);
    text_insert_file_icon(buffer, it, name, is_dir, "dir_table");
}

/* "3 files · 1 folder · 48.2 kB · last change 2 h ago" */
static gchar* totals_line(const DirSummary* s, gint64 now) {
    GString* out = g_string_new(NULL);
    if (s->truncated) {
        g_string_append_printf(out, _("%1$u+ entries (only the first %2$u are read)"),
                               (guint)DIR_SUMMARY_SCAN_LIMIT, (guint)DIR_SUMMARY_SCAN_LIMIT);
    } else {
        /* Zero counts are left out, unless both are zero */
        if (s->n_files > 0 || s->n_dirs == 0) {
            g_string_append_printf(out, ngettext("%u file", "%u files", s->n_files), s->n_files);
        }
        if (s->n_dirs > 0) {
            if (out->len > 0) g_string_append(out, " · ");
            g_string_append_printf(out, ngettext("%u folder", "%u folders", s->n_dirs), s->n_dirs);
        }
    }
    if (s->n_files > 0) {
        gchar* size = g_format_size(s->total_size);
        g_string_append(out, " · ");
        g_string_append_printf(out, _("%s in files"), size);
        g_free(size);
    }
    if (s->newest_mtime > 0) {
        gchar* age = dir_summary_format_age(s->newest_mtime, now);
        g_string_append(out, " · ");
        /* TRANSLATORS: %s is an age: "2 h ago", "yesterday", "2026-01-31" */
        g_string_append_printf(out, _("last change %s"), age);
        g_free(age);
    }
    return g_string_free(out, FALSE);
}

/* "⎇ main · 2 untracked · 1 modified" (changes known from the explorer's git scan) */
static gchar* git_line(AppData* app, const DirPreviewJob* job) {
    DirGitCounts c;
    dir_summary_count_git(git_state_badges(app->git_state), job->full, &c);
    GString* out = g_string_new(NULL);
    g_string_append_printf(out, "⎇ %s", job->branch);
    /* TRANSLATORS: counts of git changes: "2 untracked" */
    const struct { guint n; const gchar* format; } parts[] = {
        { c.conflicts, ngettext("%u conflict", "%u conflicts", c.conflicts) },
        { c.modified, ngettext("%u modified", "%u modified", c.modified) },
        { c.added, ngettext("%u added", "%u added", c.added) },
        { c.untracked, ngettext("%u untracked", "%u untracked", c.untracked) },
        { c.deleted, ngettext("%u deleted", "%u deleted", c.deleted) },
        { c.renamed, ngettext("%u renamed", "%u renamed", c.renamed) },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(parts); i++) {
        if (parts[i].n > 0) {
            g_string_append(out, " · ");
            g_string_append_printf(out, parts[i].format, parts[i].n);
        }
    }
    return g_string_free(out, FALSE);
}

static void insert_table(GtkTextBuffer* buffer, GtkTextIter* it, AppData* app,
                         const DirPreviewJob* job, gint64 now) {
    const DirSummary* s = job->summary;
    guint shown = MIN(s->entries->len, (guint)DIR_PREVIEW_ROWS);
    glong name_width = DIR_PREVIEW_NAME_MIN;
    glong age_width = 0;
    for (guint i = 0; i < shown; i++) {
        const DirSummaryEntry* e = &g_array_index(s->entries, DirSummaryEntry, i);
        name_width = MAX(name_width, g_utf8_strlen(e->name, -1) + (e->is_dir ? 1 : 0));
        gchar* age = dir_summary_format_age(e->mtime, now);
        age_width = MAX(age_width, g_utf8_strlen(age, -1));
        g_free(age);
    }
    name_width = MIN(name_width, DIR_PREVIEW_NAME_MAX);

    for (guint i = 0; i < shown; i++) {
        const DirSummaryEntry* e = &g_array_index(s->entries, DirSummaryEntry, i);
        gchar* label = e->is_dir ? g_strconcat(e->name, "/", NULL) : g_strdup(e->name);
        gchar* name = dir_summary_pad(label, name_width, FALSE);
        gchar* size_text = e->is_dir ? g_strdup("") : g_format_size(e->size);
        gchar* size = dir_summary_pad(size_text, 10, TRUE);
        gchar* age_text = dir_summary_format_age(e->mtime, now);
        gchar* age = dir_summary_pad(age_text, age_width, FALSE);

        insert_icon(buffer, it, e->name, e->is_dir);
        insert_tagged(buffer, it, name, "dir_table", e->is_dir ? "dir_folder" : NULL);
        gchar* rest = g_strdup_printf("  %s  ", size);
        insert_tagged(buffer, it, rest, "dir_table", NULL);
        insert_tagged(buffer, it, age, "dir_table", "dir_dim");
        if (git_state_badges(app->git_state) != NULL) {
            gchar* path = g_build_filename(job->full, e->name, NULL);
            const gchar* tag = git_tag(dir_summary_git_letter(git_state_badge(app->git_state, path)));
            if (tag != NULL) {
                gchar letter[] = { ' ', tag[8], '\0' }; /* "dir_git_X" -> " X" */
                insert_tagged(buffer, it, letter, "dir_table", tag);
            }
            g_free(path);
        }
        insert_tagged(buffer, it, "\n", "dir_table", NULL);

        g_free(rest);
        g_free(age);
        g_free(age_text);
        g_free(size);
        g_free(size_text);
        g_free(name);
        g_free(label);
    }
    if (s->entries->len > shown) {
        gchar* more = g_strdup_printf(_("  … and %1$u more (showing %2$u)\n"), s->entries->len - shown, shown);
        insert_tagged(buffer, it, more, "dir_dim", NULL);
        g_free(more);
    }
}

/* Fill the buffer with the README alone (Markdown and Org rendered, other
 * names as plain text); the overview is inserted above it afterwards. */
static void render_readme(AppData* app, const DirPreviewJob* job) {
    GtkTextBuffer* buffer = app->buffer;
    if (job->readme_text == NULL) {
        gtk_text_buffer_set_text(buffer, "", 0);
        return;
    }
    gboolean truncated = job->readme_size > PREVIEW_BYTES;
    switch (file_kind_from_name(job->summary->readme)) {
    case FILE_KIND_MARKDOWN:
        ensure_markdown_tags(buffer);
        render_markdown_to_buffer(buffer, GTK_TEXT_VIEW(app->text_view), job->readme_text,
                                  job->readme_path, truncated, job->summary->readme, job->readme_size);
        break;
    case FILE_KIND_ORG:
        ensure_orgmode_tags(buffer);
        render_orgmode_to_buffer(buffer, GTK_TEXT_VIEW(app->text_view), job->readme_text, job->readme_path, truncated,
                                 job->summary->readme, job->readme_size, NULL, ORG_GLOBAL_SHOW_ALL);
        break;
    default: {
        gtk_text_buffer_set_text(buffer, "", 0);
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        insert_tagged(buffer, &end, job->readme_text, "dir_plain", NULL);
        if (truncated) {
            insert_tagged(buffer, &end, "\n…\n", "dir_dim", NULL);
        }
        break;
    }
    }
}

/* Overviews already read, shown again at once (then checked in the background) */
#define DIR_CACHE_ENTRIES 16
#define DIR_CACHE_BYTES   (4 * 1024 * 1024)

static PreviewCache* dir_cache(void) {
    static PreviewCache* cache = NULL;
    if (cache == NULL) {
        cache = preview_cache_new(DIR_CACHE_ENTRIES, DIR_CACHE_BYTES, job_free);
    }
    return cache;
}

static gchar* dir_cache_key(const DirPreviewJob* job) {
    return g_strdup_printf("%d:%d:%s", job->show_hidden ? 1 : 0, (gint)job->sort, job->full);
}

/* Same overview read twice (mail folders are never cached) */
static gboolean jobs_equal(const DirPreviewJob* a, const DirPreviewJob* b) {
    return a->unreadable == b->unreadable && dir_summary_equal(a->summary, b->summary) &&
           g_strcmp0(a->branch, b->branch) == 0 && g_strcmp0(a->readme_text, b->readme_text) == 0;
}

/* Keep a finished job's results (moved out of job) */
static void cache_job(DirPreviewJob* job) {
    DirPreviewJob* kept = g_memdup2(job, sizeof(DirPreviewJob));
    kept->cancellable = NULL;
    kept->cached_shown = FALSE;
    job->name = NULL;
    job->full = NULL;
    job->summary = NULL;
    job->branch = NULL;
    job->readme_path = NULL;
    job->readme_text = NULL;
    gsize bytes = sizeof(DirPreviewJob) + kept->summary->entries->len * (sizeof(DirSummaryEntry) + 24) +
                  (kept->readme_text != NULL ? strlen(kept->readme_text) : 0);
    gchar* key = dir_cache_key(kept);
    FileStamp none = { 0, 0, 0 };
    preview_cache_insert(dir_cache(), key, &none, kept, bytes);
    g_free(key);
}

static void render_job(AppData* app, const DirPreviewJob* job);

static void job_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    DirPreviewJob* job = g_task_get_task_data(task);
    AppData* app = job->app;
    if (g_cancellable_is_cancelled(job->cancellable) ||
        job->cancellable != app->preview_cancellable || job->summary == NULL) {
        return;
    }
    if (job->mail != NULL) {
        render_job(app, job);
        return;
    }
    gchar* key = dir_cache_key(job);
    DirPreviewJob* cached = preview_cache_lookup(dir_cache(), key, NULL);
    g_free(key);
    /* Nothing changed since the cached overview shown: leave it (and its scroll) */
    if (!(job->cached_shown && cached != NULL && jobs_equal(cached, job))) {
        render_job(app, job);
    }
    cache_job(job);
}

static void render_job(AppData* app, const DirPreviewJob* job) {
    GtkTextBuffer* buffer = app->buffer;
    apply_viewer_language(app, NULL, NULL);
    if (job->mail != NULL) {
        search_forget_viewer_match(app);
        preview_doc_show(GTK_TEXT_VIEW(app->text_view), job->mail, job->full, job->name, -1);
        return;
    }
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
    set_view_text_padding(app->text_view);
    search_forget_viewer_match(app);
    ensure_tags(buffer);
    render_readme(app, job);

    gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    const DirSummary* s = job->summary;
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(buffer, &it);

    text_insert_glyph(buffer, &it, "folder", "dir_title");
    gchar* title = g_strdup_printf("%s\n", job->name);
    insert_tagged(buffer, &it, title, "dir_title", NULL);
    g_free(title);
    gchar* path = g_filename_display_name(job->full);
    gchar* path_line = g_strconcat(path, "\n", NULL);
    insert_tagged(buffer, &it, path_line, "dir_dim", NULL);
    g_free(path_line);
    g_free(path);
    if (!job->unreadable) {
        gchar* totals = totals_line(s, now);
        gchar* line = g_strconcat(totals, "\n", NULL);
        insert_tagged(buffer, &it, line, "dir_dim", NULL);
        g_free(line);
        g_free(totals);
    }
    gchar* kinds = job->unreadable ? NULL : dir_summary_kinds_line(s);
    if (kinds != NULL) {
        gchar* line = g_strconcat(kinds, "\n", NULL);
        insert_tagged(buffer, &it, line, "dir_dim", NULL);
        g_free(line);
        g_free(kinds);
    }
    if (job->branch != NULL) {
        gchar* git = git_line(app, job);
        gchar* line = g_strconcat(git, "\n", NULL);
        insert_tagged(buffer, &it, line, "dir_dim", NULL);
        g_free(line);
        g_free(git);
    }
    gtk_text_buffer_insert(buffer, &it, "\n", 1);

    if (job->unreadable) {
        insert_tagged(buffer, &it, _("  (cannot read directory)\n"), "dir_dim", NULL);
    } else if (s->entries->len == 0) {
        insert_tagged(buffer, &it, _("  (empty)\n"), "dir_dim", NULL);
    } else {
        insert_table(buffer, &it, app, job, now);
    }
    if (job->readme_text != NULL) {
        insert_tagged(buffer, &it, "\n", "dir_section", NULL);
        text_insert_file_icon(buffer, &it, s->readme, FALSE, "dir_section");
        gchar* heading = g_strdup_printf("%s\n", s->readme);
        insert_tagged(buffer, &it, heading, "dir_section", NULL);
        g_free(heading);
    }

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }
}

/* After the delay: start reading, unless another row was selected meanwhile */
static gboolean job_start(gpointer data) {
    DirPreviewJob* job = data;
    if (g_cancellable_is_cancelled(job->cancellable)) {
        job_free(job);
        return G_SOURCE_REMOVE;
    }
    GTask* task = g_task_new(NULL, job->cancellable, job_ready, NULL);
    g_task_set_task_data(task, job, job_free);
    g_task_run_in_thread(task, job_thread);
    g_object_unref(task);
    return G_SOURCE_REMOVE;
}

void dir_preview_show(AppData* app, const gchar* name, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    DirPreviewJob* job = g_new0(DirPreviewJob, 1);
    job->app = app;
    job->cancellable = g_object_ref(app->preview_cancellable);
    job->name = g_strdup(name);
    job->full = g_strdup(full);
    job->show_hidden = app->show_hidden;
    job->sort = summary_sort(app->sort_mode);

    gchar* key = dir_cache_key(job);
    DirPreviewJob* cached = preview_cache_lookup(dir_cache(), key, NULL);
    g_free(key);
    if (cached != NULL) {
        cached->app = app;
        render_job(app, cached);
        job->cached_shown = TRUE;
    }
    g_timeout_add(DIR_PREVIEW_DELAY_MS, job_start, job);
}
