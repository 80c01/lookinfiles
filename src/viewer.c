#include "viewer.h"
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
            if (is_video_file(app->viewed_name)) {
                video_preview_toggle_play(app);
                return TRUE;
            } else if (is_audio_file(app->viewed_name)) {
                audio_preview_toggle_play(app);
                return TRUE;
            }
        }
        return FALSE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && is_renderable_file(app->viewed_name)) {
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
            if (is_image_file(app->viewed_name) || is_pdf_file(app->viewed_name) ||
                is_video_file(app->viewed_name) || is_audio_file(app->viewed_name)) {
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
    gint alloc_w;
    gint alloc_h;

    /* Results populated by worker thread */
    gchar* error_msg;
    gchar* fmt_upper;
    gchar* size_str;
    gint orig_w;
    gint orig_h;
    gint target_w;
    gint target_h;
    gboolean is_fitted;
    GdkTexture* texture;
} ImagePreviewTaskData;

static void image_preview_task_data_free(gpointer data) {
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    g_free(td->error_msg);
    g_free(td->fmt_upper);
    g_free(td->size_str);
    if (td->texture != NULL) {
        g_object_unref(td->texture);
    }
    g_free(td);
}

static void image_preview_thread_func(GTask* task, gpointer source_object,
                                      gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    gint orig_w = 0, orig_h = 0;
    GdkPixbufFormat* fmt = gdk_pixbuf_get_file_info(td->full, &orig_w, &orig_h);
    if (fmt == NULL || orig_w <= 0 || orig_h <= 0) {
        td->error_msg = g_strdup_printf("[ Cannot decode image: %s ]", td->name);
        g_task_return_boolean(task, TRUE);
        return;
    }

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    gchar* fmt_name = gdk_pixbuf_format_get_name(fmt);
    td->fmt_upper = g_ascii_strup(fmt_name, -1);
    g_free(fmt_name);

    struct stat st;
    gint64 fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : 0;
    td->size_str = g_format_size((guint64)fsize);
    td->orig_w = orig_w;
    td->orig_h = orig_h;
    td->target_w = orig_w;
    td->target_h = orig_h;
    td->is_fitted = FALSE;

    if (!td->render_raw) {
        gint max_w = (td->alloc_w > 100) ? (td->alloc_w - 40) : 700;
        gint max_h = (td->alloc_h > 140) ? (td->alloc_h - 120) : 500;
        if (orig_w > max_w || orig_h > max_h) {
            gdouble scale = MIN((gdouble)max_w / orig_w, (gdouble)max_h / orig_h);
            td->target_w = MAX(1, (gint)(orig_w * scale));
            td->target_h = MAX(1, (gint)(orig_h * scale));
            td->is_fitted = TRUE;
        }
    }

    GError* err = NULL;
    GdkPixbuf* pix = gdk_pixbuf_new_from_file_at_scale(
        td->full, td->target_w, td->target_h, TRUE, &err);
    if (pix == NULL) {
        td->error_msg = g_strdup_printf("[ Error loading image: %s (%s) ]",
                                        td->name, err ? err->message : "unknown error");
        if (err != NULL) {
            g_error_free(err);
        }
        g_task_return_boolean(task, TRUE);
        return;
    }

    if (g_task_return_error_if_cancelled(task)) {
        g_object_unref(pix);
        return;
    }

    td->texture = gdk_texture_new_for_pixbuf(pix);
    g_object_unref(pix);
    g_task_return_boolean(task, TRUE);
}

static void on_image_preview_ready(GObject* source_object, GAsyncResult* res,
                                   gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)g_task_get_task_data(task);

    if (td == NULL || g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->preview_cancellable) {
        return;
    }

    AppData* app = td->app;

    apply_viewer_language(app, NULL);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 14);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 14);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }

    gtk_text_buffer_set_text(app->buffer, "", -1);
    app->viewer_match = -1;

    if (td->error_msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(app->buffer);
    if (!gtk_text_tag_table_lookup(tags, "img_center")) {
        gtk_text_buffer_create_tag(app->buffer, "img_center",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_header")) {
        gtk_text_buffer_create_tag(app->buffer, "img_header",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "scale", 1.15,
                                   "foreground", "#4fc1ff",
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_sub")) {
        gtk_text_buffer_create_tag(app->buffer, "img_sub",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "#888888",
                                   "scale", 0.9,
                                   NULL);
    }

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "img_center", NULL);

    gchar* title = g_strdup_printf("🖼️  %s\n", td->name);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, title, -1, "img_header", NULL);
    g_free(title);

    const gchar* mode_hint = td->is_fitted ? "  [fitted, press m for 1:1]"
                           : (td->render_raw && (td->orig_w != td->target_w || td->orig_h != td->target_h))
                                       ? "  [1:1 scale, press m to fit]"
                                       : "  [1:1 scale]";
    gchar* meta = g_strdup_printf("%s  •  %d × %d px  •  %s%s\n\n",
                                  td->fmt_upper, td->orig_w, td->orig_h, td->size_str, mode_hint);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta, -1, "img_sub", NULL);
    g_free(meta);

    if (td->texture != NULL) {
        gtk_text_buffer_insert_paintable(app->buffer, &iter, GDK_PAINTABLE(td->texture));

        GtkTextIter start_img = iter;
        gtk_text_iter_backward_char(&start_img);
        gtk_text_buffer_apply_tag_by_name(app->buffer, "img_center", &start_img, &iter);
    }

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "img_center", NULL);
}

/* Render image preview asynchronously on a worker thread */
void render_image_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    ImagePreviewTaskData* td = g_new0(ImagePreviewTaskData, 1);
    td->app = app;
    td->name = g_strdup(name);
    td->full = g_strdup(full);
    td->render_raw = app->render_raw;
    td->alloc_w = gtk_widget_get_width(app->text_view);
    td->alloc_h = gtk_widget_get_height(app->text_view);

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_image_preview_ready, NULL);
    g_task_set_task_data(task, td, image_preview_task_data_free);
    g_task_run_in_thread(task, image_preview_thread_func);
    g_object_unref(task);
}

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean is_md;
    gboolean is_html;
    gboolean is_org;
    gboolean render_raw;

    /* Results populated by worker thread */
    gint64 fsize;
    gsize raw_len;
    gboolean preview;
    gchar* utf8_text;
    gchar* error_msg;
} TextPreviewTaskData;

static void text_preview_task_data_free(gpointer data) {
    TextPreviewTaskData* td = (TextPreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    g_free(td->utf8_text);
    g_free(td->error_msg);
    g_free(td);
}

static void text_preview_thread_func(GTask* task, gpointer source_object,
                                     gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    TextPreviewTaskData* td = (TextPreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    struct stat st;
    td->fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : -1;
    if (td->fsize < 0) {
        td->error_msg = g_strdup_printf("[ Cannot read: %s ]", td->name);
        g_task_return_boolean(task, TRUE);
        return;
    }

    gchar* contents = NULL;
    gsize len = 0;
    if (td->fsize > PREVIEW_THRESHOLD) {
        td->preview = TRUE;
        FILE* fp = g_fopen(td->full, "rb");
        if (fp != NULL) {
            contents = g_malloc0(PREVIEW_BYTES + 1);
            len = fread(contents, 1, PREVIEW_BYTES, fp);
            contents[len] = '\0';
            fclose(fp);
        }
    } else {
        g_file_get_contents(td->full, &contents, &len, NULL);
    }

    if (g_task_return_error_if_cancelled(task)) {
        g_free(contents);
        return;
    }

    if (contents != NULL) {
        td->raw_len = len;
        if (len > 0 && !g_utf8_validate(contents, (gssize)len, NULL)) {
            td->utf8_text = g_utf8_make_valid(contents, (gssize)len);
            g_free(contents);
        } else {
            td->utf8_text = contents;
        }
    } else {
        td->error_msg = g_strdup_printf("[ Cannot read: %s ]", td->name);
    }

    g_task_return_boolean(task, TRUE);
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

    AppData* app = td->app;

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }

    gboolean is_rendered = td->is_md || td->is_html || td->is_org;
    if (is_rendered) {
        apply_viewer_language(app, NULL);
        set_view_monospace(app->text_view, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 14);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 14);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
    } else {
        gboolean code = is_code_file(td->name) || td->render_raw;
        set_view_monospace(app->text_view, code);
        apply_viewer_language(app, td->name);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_X);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_X);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_Y);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), PREVIEW_TEXT_PADDING_Y);
    }

    app->viewer_match = -1;

    if (td->error_msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

    if (td->utf8_text != NULL) {
        if (td->is_md) {
            ensure_markdown_tags(app->buffer);
            render_markdown_to_buffer(app->buffer, td->utf8_text, td->full,
                                      td->preview, td->name, td->fsize);
        } else if (td->is_html) {
            ensure_html_tags(app->buffer);
            render_html_to_buffer(app->buffer, td->utf8_text, td->full,
                                  td->preview, td->name, td->fsize);
        } else if (td->is_org) {
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
            g_string_append(text, td->utf8_text);
            gtk_text_buffer_set_text(app->buffer, text->str, (gint)text->len);
            g_string_free(text, TRUE);
        }
    }
}

void load_file_content(AppData* app, const gchar* name, const gchar* full) {
    if (is_audio_file(name)) {
        video_preview_stop(app);
        render_audio_preview(app, name, full);
        return;
    }

    if (is_video_file(name)) {
        audio_preview_stop(app);
        render_video_preview(app, name, full);
        return;
    }

    audio_preview_stop(app);
    video_preview_stop(app);
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }

    if (is_pdf_file(name)) {
        render_pdf_preview(app, name, full);
        return;
    }

    if (is_image_file(name)) {
        render_image_preview(app, name, full);
        return;
    }

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
    td->is_md = is_markdown_file(name) && !app->render_raw;
    td->is_html = is_html_file(name) && !app->render_raw;
    td->is_org = is_org_file(name) && !app->render_raw;

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


typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean show_hidden;
    GString* listing;
} DirPreviewTaskData;

static void dir_preview_task_data_free(gpointer data) {
    DirPreviewTaskData* td = (DirPreviewTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    if (td->listing != NULL) {
        g_string_free(td->listing, TRUE);
    }
    g_free(td);
}

static gint cmp_str_ptr(gconstpointer a, gconstpointer b) {
    const gchar* sa = *(const gchar* const*)a;
    const gchar* sb = *(const gchar* const*)b;
    return g_strcmp0(sa, sb);
}

static void dir_preview_thread_func(GTask* task, gpointer source_object,
                                    gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    DirPreviewTaskData* td = (DirPreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    td->listing = g_string_new(NULL);
    g_string_append_printf(td->listing, "[ Directory ] %s\n\n", td->name);

    GDir* sub = g_dir_open(td->full, 0, NULL);
    if (sub != NULL) {
        GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
        const gchar* sub_name;
        while ((sub_name = g_dir_read_name(sub)) != NULL) {
            if (g_cancellable_is_cancelled(cancellable)) {
                g_dir_close(sub);
                g_ptr_array_unref(names);
                g_task_return_error_if_cancelled(task);
                return;
            }
            if (!td->show_hidden && sub_name[0] == '.') {
                continue;
            }
            g_ptr_array_add(names, g_strdup(sub_name));
        }
        g_dir_close(sub);
        g_ptr_array_sort(names, cmp_str_ptr);

        for (guint i = 0; i < names->len; i++) {
            if (g_cancellable_is_cancelled(cancellable)) {
                g_ptr_array_unref(names);
                g_task_return_error_if_cancelled(task);
                return;
            }
            const gchar* sub_entry = g_ptr_array_index(names, i);
            gchar* sub_full = g_build_filename(td->full, sub_entry, NULL);
            gboolean sub_is_dir = g_file_test(sub_full, G_FILE_TEST_IS_DIR);
            g_free(sub_full);
            g_string_append_printf(td->listing, "  %s%s\n",
                                   sub_entry, sub_is_dir ? "/" : "");
        }
        if (names->len == 0) {
            g_string_append(td->listing, "  (empty)\n");
        }
        g_ptr_array_unref(names);
    } else {
        g_string_append(td->listing, "  (cannot read directory)\n");
    }

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

static void on_dir_preview_ready(GObject* source_object, GAsyncResult* res,
                                 gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    DirPreviewTaskData* td = (DirPreviewTaskData*)g_task_get_task_data(task);

    if (td == NULL || td->listing == NULL ||
        g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->preview_cancellable) {
        return;
    }

    AppData* app = td->app;
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_buffer_set_text(app->buffer, td->listing->str, (gint)td->listing->len);
    app->viewer_match = -1;
    apply_viewer_language(app, NULL);
}

void on_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    AppData* app = (AppData*)user_data;
    GtkTreeModel* model = NULL;
    GtkTreeIter it;

    if (!gtk_tree_selection_get_selected(selection, &model, &it)) {
        return;
    }

    gchar* name = NULL;
    gchar* type = NULL;
    gchar* full = NULL;
    gtk_tree_model_get(model, &it,
                       COL_NAME, &name,
                       COL_TYPE, &type,
                       COL_PATH, &full, -1);

    if (g_strcmp0(type, "remote") == 0) {
        if (app != NULL && !app->editor_dedicated && app->notebook != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
        audio_preview_stop(app);
        video_preview_stop(app);
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
        if (app->preview_cancellable != NULL) {
            g_cancellable_cancel(app->preview_cancellable);
            g_clear_object(&app->preview_cancellable);
        }

        RemoteMachine* rm = remote_find_by_id(app, full);
        if (rm != NULL && app->buffer != NULL) {
            GString* s = g_string_new(NULL);
            g_string_append_printf(s, "🌐 Remote Machine: %s\n", rm->name ? rm->name : "Unknown");
            g_string_append(s, "============================================================\n\n");
            g_string_append_printf(s, "  Status:        %s\n", rm->is_connected ? "🟢 Connected (Mounted via SSHFS)" : "⚪ Disconnected");
            g_string_append_printf(s, "  Host:          %s\n", rm->host ? rm->host : "(none)");
            g_string_append_printf(s, "  User:          %s\n", (rm->user && *rm->user) ? rm->user : "(default)");
            g_string_append_printf(s, "  Port:          %d\n", rm->port);
            g_string_append_printf(s, "  Remote Path:   %s\n", rm->remote_path ? rm->remote_path : "~");
            if (rm->identity_file && *rm->identity_file) {
                g_string_append_printf(s, "  Identity Key:  %s\n", rm->identity_file);
            }
            if (rm->is_connected && rm->mount_point) {
                g_string_append_printf(s, "  Mount Point:   %s\n", rm->mount_point);
            }
            if (rm->from_ssh_config) {
                g_string_append(s, "  Source:        Auto-discovered from ~/.ssh/config\n");
            } else {
                g_string_append(s, "  Source:        Configured manually\n");
            }
            g_string_append(s, "\nQuick Actions:\n");
            g_string_append(s, "  • Enter / c / Double-click: Connect and browse remote files in Explorer\n");
            g_string_append(s, "  • t:                        Open SSH terminal to this machine\n");
            g_string_append(s, "  • F2 / r / e:               Edit machine configuration\n");
            g_string_append(s, "  • d / Delete:               Remove machine\n");
            g_string_append(s, "  • a:                        Add new remote machine\n");
            g_string_append(s, "  • F5:                       Re-scan ~/.ssh/config\n");

            gtk_text_buffer_set_text(app->buffer, s->str, -1);
            g_string_free(s, TRUE);
        }
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

    /* Bookmark folders, URL bookmarks and bookmarks to missing locations have no preview */
    gboolean is_bookmark = (g_strcmp0(type, "bookmark") == 0);
    if (name == NULL || type == NULL || g_strcmp0(type, "-") == 0 ||
        g_strcmp0(type, "buffer_create") == 0 || g_strcmp0(type, "bm_folder") == 0 ||
        g_strcmp0(type, "notebook") == 0 || g_strcmp0(type, "trash") == 0 ||
        full == NULL || (is_bookmark && !g_file_test(full, G_FILE_TEST_EXISTS))) {
        g_free(name);
        g_free(type);
        g_free(full);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
        return;
    }
    gboolean as_dir = (g_strcmp0(type, "dir") == 0) ||
                      (is_bookmark && g_file_test(full, G_FILE_TEST_IS_DIR));
    if ((is_bookmark || g_strcmp0(type, "note") == 0 || g_strcmp0(type, "trashed_note") == 0) && !as_dir) {
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
        audio_preview_stop(app);
        video_preview_stop(app);
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
        if (app->preview_cancellable != NULL) {
            g_cancellable_cancel(app->preview_cancellable);
            g_clear_object(&app->preview_cancellable);
        }
        app->preview_cancellable = g_cancellable_new();

        DirPreviewTaskData* td = g_new0(DirPreviewTaskData, 1);
        td->app = app;
        td->name = g_strdup(name);
        td->full = g_strdup(full);
        td->show_hidden = app->show_hidden;

        GTask* task = g_task_new(NULL, app->preview_cancellable, on_dir_preview_ready, NULL);
        g_task_set_task_data(task, td, dir_preview_task_data_free);
        g_task_run_in_thread(task, dir_preview_thread_func);
        g_object_unref(task);
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
            (is_video_file(name) || is_audio_file(name) || gtk_text_buffer_get_char_count(app->buffer) > 0)) {
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
