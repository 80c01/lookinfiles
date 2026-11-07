/* Large text files loaded chunk by chunk on scroll (see text_chunks.h). */
#include "text_chunks.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "diff_gutter.h"
#include "color_column.h"
#include "text_encoding.h"
#include "preview_full.h"

typedef struct {
    gchar* path;
    gint64 offset;          /* next byte to read */
    gint64 size;
    gint64 loaded;          /* bytes shown so far */
    gint first_line;
    gboolean line_numbers;
    GCancellable* loading;  /* a chunk being read, or NULL */
    gboolean active;
} TextChunks;

static TextChunks* chunks_of(AppData* app) {
    if (app->text_chunks == NULL) {
        app->text_chunks = g_new0(TextChunks, 1);
    }
    return app->text_chunks;
}

void text_chunks_stop(AppData* app) {
    TextChunks* c = chunks_of(app);
    if (c->loading != NULL) {
        g_cancellable_cancel(c->loading);
        g_clear_object(&c->loading);
    }
    g_clear_pointer(&c->path, g_free);
    c->active = FALSE;
}

typedef struct {
    AppData* app;
    gchar* path;
    gint64 offset;
    gchar* text;            /* the chunk, UTF-8, ending at a line end when possible */
    gsize bytes;            /* bytes of the file it covers */
} ChunkTask;

static void chunk_task_free(gpointer data) {
    ChunkTask* t = data;
    g_free(t->path);
    g_free(t->text);
    g_free(t);
}

static void chunk_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    ChunkTask* t = data;
    FILE* fp = g_fopen(t->path, "rb");
    if (fp != NULL) {
        gchar* buf = g_malloc(PREVIEW_BYTES + 1);
        gsize n = 0;
        if (fseeko(fp, t->offset, SEEK_SET) == 0) {
            n = fread(buf, 1, PREVIEW_BYTES, fp);
        }
        fclose(fp);
        /* Skip the rest of a character cut by the previous chunk, and end
         * at the last line break so lines are not split */
        gsize start = 0;
        while (start < n && start < 3 && ((guchar)buf[start] & 0xC0) == 0x80) {
            start++;
        }
        gsize end = n;
        if (n == PREVIEW_BYTES) {
            const gchar* nl = g_strrstr_len(buf + start, (gssize)(n - start), "\n");
            if (nl != NULL) {
                end = (gsize)(nl - buf) + 1;
            }
        }
        t->bytes = end;
        t->text = text_encoding_to_utf8(buf + start, end - start, NULL);
        g_free(buf);
    }
    g_task_return_boolean(task, TRUE);
}

static void on_chunk_ready(GObject* source, GAsyncResult* res, gpointer user_data) {
    (void)source;
    (void)user_data;
    GTask* task = G_TASK(res);
    ChunkTask* t = g_task_get_task_data(task);
    if (g_cancellable_is_cancelled(g_task_get_cancellable(task))) {
        return;
    }
    AppData* app = t->app;
    TextChunks* c = chunks_of(app);
    g_clear_object(&c->loading);
    if (!c->active || g_strcmp0(c->path, t->path) != 0 || t->text == NULL || t->bytes == 0) {
        c->active = FALSE;
        return;
    }
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->buffer, &end);
    gtk_text_buffer_insert(app->buffer, &end, t->text, -1);
    c->offset += (gint64)t->bytes;
    c->loaded += (gint64)t->bytes;
    if (c->line_numbers) {
        diff_gutter_set_line_numbers(app->text_view, c->first_line);
    }
    preview_full_update(app, c->path, c->offset, c->size);
    if (c->offset >= c->size || c->loaded >= TEXT_CHUNKS_MAX_BYTES) {
        gchar* shown = g_format_size((guint64)c->loaded);
        gchar* note = c->offset >= c->size ? NULL
                      : g_strdup_printf("\n[ Showing the first %s: the preview stops here; Load Whole File above, or the editor (e), shows the rest ]\n", shown);
        if (note != NULL) {
            gtk_text_buffer_get_end_iter(app->buffer, &end);
            gtk_text_buffer_insert(app->buffer, &end, note, -1);
            g_free(note);
        }
        g_free(shown);
        c->active = FALSE;
    }
    /* Inserting text hid the column line */
    if (c->line_numbers) {
        color_column_set(app->text_view, app->editor_color_column);
    }
}

static void load_next(AppData* app) {
    TextChunks* c = chunks_of(app);
    if (!c->active || c->loading != NULL) {
        return;
    }
    c->loading = g_cancellable_new();
    ChunkTask* t = g_new0(ChunkTask, 1);
    t->app = app;
    t->path = g_strdup(c->path);
    t->offset = c->offset;
    GTask* task = g_task_new(NULL, c->loading, on_chunk_ready, NULL);
    g_task_set_task_data(task, t, chunk_task_free);
    g_task_run_in_thread(task, chunk_thread);
    g_object_unref(task);
}

/* Near the end of what is shown: load more */
static void on_scrolled(GtkAdjustment* adj, gpointer data) {
    AppData* app = data;
    TextChunks* c = chunks_of(app);
    if (!c->active) {
        return;
    }
    gdouble page = gtk_adjustment_get_page_size(adj);
    if (gtk_adjustment_get_value(adj) + page >= gtk_adjustment_get_upper(adj) - page) {
        load_next(app);
    }
}

void text_chunks_attach(AppData* app) {
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        g_signal_connect(vadj, "value-changed", G_CALLBACK(on_scrolled), app);
    }
}

void text_chunks_start(AppData* app, const gchar* path, gint64 offset, gint64 size, gint first_line,
                       gboolean line_numbers) {
    text_chunks_stop(app);
    TextChunks* c = chunks_of(app);
    c->path = g_strdup(path);
    c->offset = offset;
    c->size = size;
    c->loaded = offset;
    c->first_line = first_line;
    c->line_numbers = line_numbers;
    c->active = offset < size;
}
