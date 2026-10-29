#include "dir_preview.h"
#include "dir_summary.h"
#include "file_types.h"
#include "text_style.h"

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

/* Worker thread: one enumeration of the directory (type, size and date come
 * with the names, no extra stat per entry), never recursive. */
static void job_thread(GTask* task, gpointer source_object, gpointer task_data,
                       GCancellable* cancellable) {
    (void)source_object;
    DirPreviewJob* job = task_data;

    job->summary = dir_summary_new();
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
    gtk_text_buffer_create_tag(buffer, "dir_title",
                               "weight", 600, "scale", 1.3, "foreground", "#e6e6e6",
                               "pixels-below-lines", 2, NULL);
    gtk_text_buffer_create_tag(buffer, "dir_dim", "foreground", "#8b8b8b", NULL);
    gtk_text_buffer_create_tag(buffer, "dir_table",
                               "family", "monospace", "wrap-mode", GTK_WRAP_NONE, NULL);
    gtk_text_buffer_create_tag(buffer, "dir_folder", "weight", 600, NULL);
}

static void insert_tagged(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* text,
                          const gchar* tag1, const gchar* tag2) {
    gtk_text_buffer_insert_with_tags_by_name(buffer, it, text, -1, tag1, tag2, NULL);
}

/* "3 files · 1 folder · 48.2 kB · last change 2 h ago" */
static gchar* totals_line(const DirSummary* s, gint64 now) {
    GString* out = g_string_new(NULL);
    if (s->truncated) {
        g_string_append_printf(out, "%u+ entries (only the first %u are read)",
                               (guint)DIR_SUMMARY_SCAN_LIMIT, (guint)DIR_SUMMARY_SCAN_LIMIT);
    } else {
        /* Zero counts are left out, unless both are zero */
        if (s->n_files > 0 || s->n_dirs == 0) {
            g_string_append_printf(out, "%u %s", s->n_files, s->n_files == 1 ? "file" : "files");
        }
        if (s->n_dirs > 0) {
            g_string_append_printf(out, "%s%u %s", out->len > 0 ? " · " : "",
                                   s->n_dirs, s->n_dirs == 1 ? "folder" : "folders");
        }
    }
    if (s->n_files > 0) {
        gchar* size = g_format_size(s->total_size);
        g_string_append_printf(out, " · %s in files", size);
        g_free(size);
    }
    if (s->newest_mtime > 0) {
        gchar* age = dir_summary_format_age(s->newest_mtime, now);
        g_string_append_printf(out, " · last change %s", age);
        g_free(age);
    }
    return g_string_free(out, FALSE);
}

static void insert_table(GtkTextBuffer* buffer, GtkTextIter* it, const DirSummary* s, gint64 now) {
    guint shown = MIN(s->entries->len, (guint)DIR_PREVIEW_ROWS);
    glong name_width = DIR_PREVIEW_NAME_MIN;
    for (guint i = 0; i < shown; i++) {
        const DirSummaryEntry* e = &g_array_index(s->entries, DirSummaryEntry, i);
        name_width = MAX(name_width, g_utf8_strlen(e->name, -1) + (e->is_dir ? 1 : 0));
    }
    name_width = MIN(name_width, DIR_PREVIEW_NAME_MAX);

    for (guint i = 0; i < shown; i++) {
        const DirSummaryEntry* e = &g_array_index(s->entries, DirSummaryEntry, i);
        gchar* label = e->is_dir ? g_strconcat(e->name, "/", NULL) : g_strdup(e->name);
        gchar* name = dir_summary_pad(label, name_width, FALSE);
        gchar* size_text = e->is_dir ? g_strdup("") : g_format_size(e->size);
        gchar* size = dir_summary_pad(size_text, 10, TRUE);
        gchar* age_text = dir_summary_format_age(e->mtime, now);
        gchar* age = dir_summary_pad(age_text, 12, FALSE);

        gchar* icon = g_strdup_printf("  %s ", file_icon(e->name, e->is_dir));
        insert_tagged(buffer, it, icon, "dir_table", NULL);
        insert_tagged(buffer, it, name, "dir_table", e->is_dir ? "dir_folder" : NULL);
        gchar* rest = g_strdup_printf("  %s  ", size);
        insert_tagged(buffer, it, rest, "dir_table", NULL);
        insert_tagged(buffer, it, age, "dir_table", "dir_dim");
        insert_tagged(buffer, it, "\n", "dir_table", NULL);

        g_free(rest);
        g_free(icon);
        g_free(age);
        g_free(age_text);
        g_free(size);
        g_free(size_text);
        g_free(name);
        g_free(label);
    }
    if (s->entries->len > shown) {
        gchar* more = g_strdup_printf("  … and %u more (showing %u)\n", s->entries->len - shown, shown);
        insert_tagged(buffer, it, more, "dir_dim", NULL);
        g_free(more);
    }
}

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

    GtkTextBuffer* buffer = app->buffer;
    apply_viewer_language(app, NULL, NULL);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
    set_view_text_padding(app->text_view);
    app->viewer_match = -1;
    ensure_tags(buffer);
    gtk_text_buffer_set_text(buffer, "", 0);

    gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    const DirSummary* s = job->summary;
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(buffer, &it);

    gchar* title = g_strdup_printf("📂 %s\n", job->name);
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
    gtk_text_buffer_insert(buffer, &it, "\n", 1);

    if (job->unreadable) {
        insert_tagged(buffer, &it, "  (cannot read directory)\n", "dir_dim", NULL);
    } else if (s->entries->len == 0) {
        insert_tagged(buffer, &it, "  (empty)\n", "dir_dim", NULL);
    } else {
        insert_table(buffer, &it, s, now);
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
    g_timeout_add(DIR_PREVIEW_DELAY_MS, job_start, job);
}
