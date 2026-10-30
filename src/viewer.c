#include "viewer.h"
#include "image_preview.h"
#include "preview_summary.h"
#include "fs_watch.h"
#include "panels.h"
#include "app_events.h"
#include "text_style.h"
#include "help.h"
#include "file_types.h"
#include "editor.h"
#include "file_list.h"
#include "markdown.h"
#include "html.h"
#include "pdf.h"
#include "video.h"
#include "audio.h"
#include "search.h"
#include "bookmarks.h"
#include "remote.h"
#include "orgmode.h"
#include "diff_gutter.h"
#include "dir_preview.h"
#include "preview_doc_view.h"
#include "preview_formats.h"
#include "text_encoding.h"

/* Text shown from a Word document before it is cut (images not counted) */


void viewer_scroll_lines(AppData* app, gint lines) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));

    GtkTextIter first;
    gtk_text_buffer_get_start_iter(app->buffer, &first);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(view, &first, &rect);
    gdouble line_h = (rect.height > 0.0) ? rect.height : 14.0;

    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + lines * line_h);
}

void viewer_scroll_page(AppData* app, gint direction) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gtk_adjustment_set_value(
        adj, gtk_adjustment_get_value(adj) + direction * gtk_adjustment_get_page_size(adj));
}

void viewer_goto(AppData* app, gboolean top) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkTextIter it;
    if (top) {
        gtk_text_buffer_get_start_iter(app->buffer, &it);
    } else {
        gtk_text_buffer_get_end_iter(app->buffer, &it);
    }
    gtk_text_view_scroll_to_iter(view, &it, 0.0, FALSE, 0.0, top ? 0.0 : 1.0);
}

gboolean on_viewer_key_press(GtkEventControllerKey* controller, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;

    if (orgmode_handle_viewer_key(app, keyval, state)) {
        return TRUE;
    }

    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0 && image_preview_zoom_key(app, keyval)) {
        app->pending_g = FALSE;
        return TRUE;
    }

    guint key = keyval;

    switch (key) {
    case GDK_KEY_j:
        app->pending_g = FALSE;
        viewer_scroll_lines(app, +1);
        return TRUE;
    case GDK_KEY_k:
        app->pending_g = FALSE;
        viewer_scroll_lines(app, -1);
        return TRUE;
    case GDK_KEY_g:
        if (app->pending_g) {
            app->pending_g = FALSE;
            viewer_goto(app, TRUE);
        } else {
            app->pending_g = TRUE;
        }
        return TRUE;
    case GDK_KEY_G:
        app->pending_g = FALSE;
        viewer_goto(app, FALSE);
        return TRUE;
    case GDK_KEY_slash:
        app->pending_g = FALSE;
        show_search(app, TRUE);
        return TRUE;
    case GDK_KEY_n:
        app->pending_g = FALSE;
        do_viewer_search(app, +1);
        return TRUE;
    case GDK_KEY_N:
        app->pending_g = FALSE;
        do_viewer_search(app, -1);
        return TRUE;
    case GDK_KEY_b:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, -1);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_d:
    case GDK_KEY_D:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            bookmarks_add_from_context(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_a:
    case GDK_KEY_A:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            toggle_bookmarks_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_u:
    case GDK_KEY_U:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            toggle_notes_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, +1);
            return TRUE;
        }
        toggle_fullscreen(app->window);
        return TRUE;
    case GDK_KEY_p:
    case GDK_KEY_P:
    case GDK_KEY_space:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && !app->render_raw) {
            if (app->viewed_kind == FILE_KIND_VIDEO) {
                video_preview_toggle_play(app);
                return TRUE;
            } else if (app->viewed_kind == FILE_KIND_AUDIO) {
                audio_preview_toggle_play(app);
                return TRUE;
            }
        }
        return FALSE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && file_kind_is_renderable(app->viewed_kind)) {
            app->render_raw = !app->render_raw;
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            if (app->viewed_path != NULL) {
                load_file_content(app, app->viewed_name, app->viewed_path);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_e:
    case GDK_KEY_E:
        app->pending_g = FALSE;
        if (app->viewed_path != NULL && app->viewed_name != NULL) {
            if (g_file_test(app->viewed_path, G_FILE_TEST_IS_DIR)) {
                return FALSE;
            }
            if (file_kind_is_media(app->viewed_kind)) {
                return FALSE;
            }
            editor_open_file(app, app->viewed_name, app->viewed_path);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_question:
        if ((state & GDK_CONTROL_MASK) == 0) {
            app->pending_g = FALSE;
            toggle_help_panel(app);
            return TRUE;
        }
        app->pending_g = FALSE;
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    default:
        app->pending_g = FALSE;
        return FALSE;
    }
}


typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean render_raw;

    /* Results populated by worker thread */
    FileKind kind;         /* detected from the first bytes */
    gchar* content_type;   /* GIO content type of text files (syntax highlighting) */
    gint64 fsize;
    gsize raw_len;
    gboolean preview;
    gchar* utf8_text;
    PreviewDoc* doc;       /* structured preview (preview_formats) or hex dump */
    const gchar* encoding; /* text encoding detected (static string) */
    gchar* error_msg;
    gint64 start_us;       /* selection time (LOOKIN_DEBUG_PREVIEW timings) */
} TextPreviewTaskData;

static void text_preview_task_data_free(gpointer data) {
    TextPreviewTaskData* td = (TextPreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    g_free(td->content_type);
    g_free(td->utf8_text);
    preview_doc_free(td->doc);
    g_free(td->error_msg);
    g_free(td);
}

/* Worker thread: read the first bytes and detect the file kind. Media
 * viewers read their files themselves; text kinds read the text (up to
 * PREVIEW_BYTES for large files); kinds with a structured preview build it
 * (preview_formats), and binary content without one gets a hex dump. */
static void text_preview_thread_func(GTask* task, gpointer source_object,
                                     gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    TextPreviewTaskData* td = (TextPreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    td->kind = file_kind_from_name(td->name);
    struct stat st;
    td->fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : -1;
    FILE* fp = (td->fsize >= 0) ? g_fopen(td->full, "rb") : NULL;
    if (fp == NULL) {
        td->error_msg = g_strdup_printf("[ Cannot read: %s ]", td->name);
        g_task_return_boolean(task, TRUE);
        return;
    }

    td->preview = (td->fsize > PREVIEW_THRESHOLD);
    gsize cap = td->preview ? PREVIEW_BYTES : (gsize)td->fsize + 1;
    if (cap < FILE_SNIFF_BYTES) {
        cap = FILE_SNIFF_BYTES;
    }
    gchar* contents = g_malloc(cap + 1);
    gsize len = fread(contents, 1, FILE_SNIFF_BYTES, fp);
    td->kind = file_kind_detect(td->name, (const guchar*)contents, len);
    gboolean structured = !td->render_raw && preview_format_has_builder(td->kind);
    PreviewInput input = {
        .path = td->full, .name = td->name, .size = td->fsize, .kind = td->kind,
        .head = (const guchar*)contents, .head_len = MIN(len, FILE_SNIFF_BYTES),
    };

    if (file_kind_is_media(td->kind)) {
        fclose(fp);
        g_free(contents);
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* Text kinds, and text content of other kinds (PEM certificates), are
     * read as text; the rest (gzipped manual pages too) as binary */
    if (!file_content_is_text((const guchar*)contents, len)) {
        /* Binary content: its structured preview, else a hex dump of the
         * first PREVIEW_BYTES only, whatever the file size */
        GError* err = NULL;
        if (structured) {
            td->doc = preview_format_build(&input, cancellable, &err);
        }
        if (td->doc == NULL) {
            if (cap < PREVIEW_BYTES) {
                contents = g_realloc(contents, PREVIEW_BYTES + 1);
            }
            while (len < PREVIEW_BYTES && !g_cancellable_is_cancelled(cancellable)) {
                gsize n = fread(contents + len, 1, PREVIEW_BYTES - len, fp);
                if (n == 0) {
                    break;
                }
                len += n;
            }
            td->doc = preview_format_hex_dump(td->name, td->kind, td->fsize, (const guchar*)contents,
                                              len, err != NULL ? err->message : NULL);
        }
        g_clear_error(&err);
        fclose(fp);
        g_free(contents);
        if (g_task_return_error_if_cancelled(task)) {
            return;
        }
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* Rest of the text: to the end (the file may have grown), capped for large files */
    while (!g_cancellable_is_cancelled(cancellable)) {
        if (len == cap) {
            if (td->preview) {
                break;
            }
            cap *= 2;
            contents = g_realloc(contents, cap + 1);
        }
        gsize n = fread(contents + len, 1, cap - len, fp);
        if (n == 0) {
            break;
        }
        len += n;
    }
    contents[len] = '\0';
    td->content_type = g_content_type_guess(td->name, (const guchar*)contents,
                                            MIN(len, FILE_SNIFF_BYTES), NULL);
    fclose(fp);

    if (g_task_return_error_if_cancelled(task)) {
        g_free(contents);
        return;
    }

    td->raw_len = len;
    td->encoding = "UTF-8";
    if (len > 0 && !g_utf8_validate(contents, (gssize)len, NULL)) {
        /* UTF-16, legacy 8-bit text, or invalid bytes */
        td->utf8_text = text_encoding_to_utf8(contents, len, &td->encoding);
        g_free(contents);
    } else if (len >= 3 && memcmp(contents, "\xEF\xBB\xBF", 3) == 0) {
        td->encoding = "UTF-8 BOM";
        td->utf8_text = contents;
    } else {
        td->utf8_text = contents;
    }

    if (structured) {
        /* Shown as plain text when the structured preview fails (invalid JSON...) */
        gsize text_len = strlen(td->utf8_text);
        input.head = (const guchar*)td->utf8_text;
        input.head_len = MIN(text_len, FILE_SNIFF_BYTES);
        input.text = td->utf8_text;
        input.text_len = text_len;
        input.text_truncated = td->preview;
        td->doc = preview_format_build(&input, cancellable, NULL);
    }

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

/* Show a file in a media viewer (main thread) */
static void show_media_preview(AppData* app, FileKind kind, const gchar* name, const gchar* full) {
    if (kind == FILE_KIND_AUDIO) {
        video_preview_stop(app);
        render_audio_preview(app, name, full);
        return;
    }
    if (kind == FILE_KIND_VIDEO) {
        audio_preview_stop(app);
        render_video_preview(app, name, full);
        return;
    }
    /* Leave the player page before stopping it (its controls must not be
     * redrawn unallocated) */
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }
    audio_preview_stop(app);
    video_preview_stop(app);
    if (kind == FILE_KIND_PDF) {
        render_pdf_preview(app, name, full);
    } else {
        render_image_preview(app, name, full);
    }
}

static void show_text_preview(TextPreviewTaskData* td);

/* LOOKIN_DEBUG_PREVIEW=1: time from the selection to the preview on screen */
static gboolean debug_preview_timings(void) {
    static gint enabled = -1;
    if (enabled < 0) {
        enabled = g_getenv("LOOKIN_DEBUG_PREVIEW") != NULL;
    }
    return enabled;
}

static void on_text_preview_ready(GObject* source_object, GAsyncResult* res,
                                  gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    TextPreviewTaskData* td = (TextPreviewTaskData*)g_task_get_task_data(task);

    if (td == NULL || g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->preview_cancellable) {
        return;
    }
    gint64 ready_us = g_get_monotonic_time();
    show_text_preview(td);
    if (debug_preview_timings()) {
        gint64 end_us = g_get_monotonic_time();
        g_printerr("preview %s [%s]: %.1f ms (worker %.1f ms, display %.1f ms)\n", td->name,
                   file_kind_label(td->kind) != NULL ? file_kind_label(td->kind) : "Text",
                   (end_us - td->start_us) / 1000.0, (ready_us - td->start_us) / 1000.0,
                   (end_us - ready_us) / 1000.0);
    }
}

static void show_text_preview(TextPreviewTaskData* td) {
    AppData* app = td->app;

    if (g_strcmp0(td->full, app->viewed_path) == 0 &&
        (app->viewed_kind != td->kind || g_strcmp0(app->viewed_content_type, td->content_type) != 0 ||
         app->viewed_encoding != td->encoding)) {
        app->viewed_kind = td->kind;
        app->viewed_encoding = td->encoding;
        g_free(app->viewed_content_type);
        app->viewed_content_type = g_strdup(td->content_type);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    }

    if (file_kind_is_media(td->kind)) {
        show_media_preview(app, td->kind, td->name, td->full);
        return;
    }

    /* Leave the player page before stopping it (its controls must not be
     * redrawn unallocated) */
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }
    audio_preview_stop(app);
    video_preview_stop(app);

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }

    app->viewer_match = -1;

    if (td->error_msg != NULL) {
        apply_viewer_language(app, NULL, NULL);
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

    if (td->doc != NULL) {
        apply_viewer_language(app, NULL, NULL);
        preview_doc_show(GTK_TEXT_VIEW(app->text_view), td->doc, td->full, td->name, td->fsize);
        return;
    }

    gboolean is_md = (td->kind == FILE_KIND_MARKDOWN) && !td->render_raw;
    gboolean is_html = (td->kind == FILE_KIND_HTML) && !td->render_raw;
    gboolean is_org = (td->kind == FILE_KIND_ORG) && !td->render_raw;
    gboolean is_rendered = is_md || is_html || is_org;
    gboolean code = FALSE;
    if (is_rendered) {
        apply_viewer_language(app, NULL, NULL);
        set_view_monospace(app->text_view, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
        set_view_text_padding(app->text_view);
    } else {
        gboolean has_lang = apply_viewer_language(app, td->name, td->content_type);
        code = has_lang || is_code_file(td->name) || td->render_raw ||
                        (td->utf8_text != NULL && g_str_has_prefix(td->utf8_text, "#!"));
        set_view_monospace(app->text_view, code);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        set_view_text_padding(app->text_view);
    }

    if (td->utf8_text != NULL) {
        if (is_md) {
            ensure_markdown_tags(app->buffer);
            render_markdown_to_buffer(app->buffer, GTK_TEXT_VIEW(app->text_view), td->utf8_text, td->full,
                                      td->preview, td->name, td->fsize);
        } else if (is_html) {
            ensure_html_tags(app->buffer);
            render_html_to_buffer(app->buffer, td->utf8_text, td->full,
                                  td->preview, td->name, td->fsize);
        } else if (is_org) {
            ensure_orgmode_tags(app->buffer);
            render_orgmode_to_buffer(app->buffer, td->utf8_text, td->full,
                                     td->preview, td->name, td->fsize,
                                     app->org_folded_lines, app->org_global_fold);
        } else {
            GString* text = g_string_new(NULL);
            if (td->preview) {
                g_string_append_printf(text,
                        "[ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)\n"
                        "Showing first %u bytes. Large file, display truncated.\n\n",
                        td->name, td->fsize, (guint)td->raw_len);
            }
            /* Line numbers start after the truncation header */
            gint first_line = 0;
            for (const gchar* c = text->str; *c != '\0'; c++) {
                first_line += (*c == '\n');
            }
            g_string_append(text, td->utf8_text);
            gtk_text_buffer_set_text(app->buffer, text->str, (gint)text->len);
            g_string_free(text, TRUE);
            if (code) {
                diff_gutter_set_line_numbers(app->text_view, first_line);
            }
            /* Git change bars, for whole files only (a truncated preview has a header) */
            if (!td->preview) {
                diff_gutter_set_file(app->text_view, td->full);
            }
        }
    }
}

/* Preview a file. Its kind is detected from its content in a worker thread
 * (one read serves detection and text), so the UI thread does no file I/O. */
void load_file_content(AppData* app, const gchar* name, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    TextPreviewTaskData* td = g_new0(TextPreviewTaskData, 1);
    td->app = app;
    td->name = g_strdup(name);
    td->full = g_strdup(full);
    td->render_raw = app->render_raw;
    td->start_us = g_get_monotonic_time();
    if (g_strcmp0(full, app->viewed_path) != 0) {
        /* Provisional kind until the content is read */
        app->viewed_kind = file_kind_from_name(name);
        g_clear_pointer(&app->viewed_content_type, g_free);
    }

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_text_preview_ready, NULL);
    g_task_set_task_data(task, td, text_preview_task_data_free);
    g_task_run_in_thread(task, text_preview_thread_func);
    g_object_unref(task);
}

/* Re-render the file currently shown in the preview (APP_EVENT_PREVIEW_STALE) */
void viewer_reload_preview(AppData* app) {
    gchar* fallback = (app->viewed_name == NULL && app->viewed_path != NULL)
                          ? g_path_get_basename(app->viewed_path)
                          : NULL;
    load_file_content(app, app->viewed_name != NULL ? app->viewed_name : fallback, app->viewed_path);
    g_free(fallback);
}


void on_selection_changed(ExplorerTree* tree, gpointer user_data) {
    AppData* app = (AppData*)user_data;
    ExplorerRow* row = explorer_tree_get_selected(tree);
    if (row == NULL) {
        return;
    }

    gchar* name = g_strdup(row->name);
    gchar* type = g_strdup(row->type);
    gchar* full = g_strdup(row->path);

    /* Rows without file content (remote machine, notebook, bookmark folder,
     * notes trash): show a text summary instead of keeping the previous preview */
    gboolean is_remote = (g_strcmp0(type, "remote") == 0);
    gchar* summary = is_remote ? preview_summary_remote(remote_find_by_id(app, full))
                               : preview_summary_container(row);
    if (is_remote || summary != NULL) {
        if (app != NULL && !app->editor_dedicated && app->notebook != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
        /* Leave the player page before stopping it (its controls must not be
         * redrawn unallocated) */
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
        audio_preview_stop(app);
        video_preview_stop(app);
        if (app->preview_cancellable != NULL) {
            g_cancellable_cancel(app->preview_cancellable);
            g_clear_object(&app->preview_cancellable);
        }
        if (summary != NULL && app->buffer != NULL) {
            apply_viewer_language(app, NULL, NULL);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
            set_view_text_padding(app->text_view);
            set_view_monospace(app->text_view, TRUE); /* summary columns are space-aligned */
            gtk_text_buffer_set_text(app->buffer, summary, -1);
        }
        g_free(summary);
        g_free(app->viewed_path);
        g_free(app->viewed_name);
        app->viewed_path = NULL;
        app->viewed_name = NULL;
        monitor_file(app, NULL);

        g_free(name);
        g_free(type);
        g_free(full);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
        return;
    }

    /* Placeholders, URL bookmarks and bookmarks to missing locations have no preview */
    gboolean is_bookmark = (g_strcmp0(type, "bookmark") == 0);
    if (name == NULL || type == NULL || g_strcmp0(type, "-") == 0 ||
        g_strcmp0(type, "buffer_create") == 0 ||
        full == NULL || (is_bookmark && !g_file_test(full, G_FILE_TEST_EXISTS))) {
        g_free(name);
        g_free(type);
        g_free(full);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
        return;
    }
    gboolean as_dir = (g_strcmp0(type, "dir") == 0) ||
                      (is_bookmark && g_file_test(full, G_FILE_TEST_IS_DIR));
    if ((is_bookmark || g_strcmp0(type, "note") == 0 || g_strcmp0(type, "trash_note") == 0) && !as_dir) {
        /* Detect the file type from its real name */
        g_free(name);
        name = g_path_get_basename(full);
    }

    if (app != NULL && !app->editor_dedicated && app->notebook != NULL) {
        /* Do not return to preview tab if the user is currently editing in an editor tab,
         * unless the tree view has focus (e.g. user navigating or clicking in the tree view)
         * or a different file/note was selected in the explorer panel. */
        EditorTab* cur = editor_current_tab(app);
        gboolean is_diff_file = (cur != NULL && full != NULL && g_strcmp0(full, cur->path) != 0);
        gboolean tree_focused = (app->tree_view != NULL && gtk_widget_has_focus(GTK_WIDGET(app->tree_view)));
        if (cur == NULL || is_diff_file || tree_focused) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
    }

    if (as_dir) {
        /* Leave the player page before stopping it (its controls must not be
         * redrawn unallocated) */
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
        audio_preview_stop(app);
        video_preview_stop(app);
        dir_preview_show(app, name, full);
    } else {
        /* If there's an open editor tab for this file with modified/buffered content, flush it */
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && g_strcmp0(t->path, full) == 0) {
                if (t->buffer != NULL && (t->is_buffer || gtk_text_buffer_get_modified(t->buffer))) {
                    GtkTextIter start, end;
                    gtk_text_buffer_get_bounds(t->buffer, &start, &end);
                    gchar* text = gtk_text_buffer_get_text(t->buffer, &start, &end, FALSE);
                    g_file_set_contents(t->path, text, -1, NULL);
                    g_free(text);
                    gtk_text_buffer_set_modified(t->buffer, FALSE);
                    editor_tab_label_update(t);
                    app->file_dirty = TRUE;
                }
                break;
            }
        }

        if (g_strcmp0(full, app->viewed_path) == 0 && !app->file_dirty &&
            (app->viewed_kind == FILE_KIND_VIDEO || app->viewed_kind == FILE_KIND_AUDIO ||
             gtk_text_buffer_get_char_count(app->buffer) > 0)) {
            if (!app->switching_ws) {
                app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            }
            g_free(full);
            g_free(name);
            g_free(type);
            return;
        }
        if (!app->switching_ws && g_strcmp0(name, app->viewed_name) != 0) {
            app->render_raw = FALSE;
            app->preview_zoom = 1.0;
        }
        app->file_dirty = FALSE;
        load_file_content(app, name, full);
    }

    g_free(app->viewed_path);
    g_free(app->viewed_name);
    if (as_dir) {
        app->viewed_path = NULL;
        app->viewed_name = NULL;
        monitor_file(app, NULL);
    } else {
        app->viewed_path = g_strdup(full);
        app->viewed_name = g_strdup(name);
        monitor_file(app, full);
    }

    if (!app->switching_ws) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }

    app_emit(app, APP_EVENT_STATUS_CHANGED);

    g_free(full);
    g_free(name);
    g_free(type);
}
