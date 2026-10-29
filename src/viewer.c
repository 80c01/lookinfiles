#include "viewer.h"
#include "image_utils.h"
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
#include "hex_dump.h"
#include "elf_info.h"
#include "docx.h"

/* Text shown from a Word document before it is cut (images not counted) */
#define DOCUMENT_PREVIEW_TEXT (256 * 1024)


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

    td->texture = texture_new_for_pixbuf(pix);
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

    apply_viewer_language(app, NULL, NULL);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    set_view_text_padding(app->text_view);

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
    gboolean render_raw;

    /* Results populated by worker thread */
    FileKind kind;         /* detected from the first bytes */
    gchar* content_type;   /* GIO content type of text files (syntax highlighting) */
    gint64 fsize;
    gsize raw_len;
    gboolean preview;
    gchar* utf8_text;
    gchar* hex_dump;       /* binary files: dump of the first PREVIEW_BYTES */
    gsize hex_len;         /* bytes in hex_dump */
    gchar* elf_summary;    /* ELF files: header details (elf_info_format()) */
    gchar* elf_type;       /* "Shared library", "Relocatable object"... */
    gchar* doc_error;      /* Word documents that could not be converted: why */
    gchar* error_msg;
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
    g_free(td->hex_dump);
    g_free(td->elf_summary);
    g_free(td->elf_type);
    g_free(td->doc_error);
    g_free(td->error_msg);
    g_free(td);
}

/* Worker thread: read the first bytes, detect the file kind, then read the
 * text (up to PREVIEW_BYTES for large files) unless a media viewer takes it. */
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

    gboolean want_hex = (td->kind == FILE_KIND_BINARY);
    if (td->kind == FILE_KIND_EXECUTABLE) {
        /* Header details; the raw view (m) and damaged files get the hex dump */
        if (!td->render_raw) {
            ElfInfo* info = elf_info_read_file(td->full, NULL);
            if (info != NULL) {
                td->elf_summary = elf_info_format(info);
                td->elf_type = g_strdup(elf_info_type_name(info));
                elf_info_free(info);
            }
        }
        want_hex = (td->elf_summary == NULL);
    } else if (td->kind == FILE_KIND_DOCUMENT) {
        /* Converted to HTML; the raw view (m) and unreadable files get the hex dump */
        if (!td->render_raw) {
            GError* err = NULL;
            td->utf8_text = docx_file_to_html(td->full, DOCUMENT_PREVIEW_TEXT, &err);
            if (td->utf8_text == NULL) {
                td->doc_error = g_strdup(err->message);
                g_error_free(err);
            }
        }
        want_hex = (td->utf8_text == NULL);
    }

    if (want_hex) {
        /* Hex dump of the first PREVIEW_BYTES only, whatever the file size */
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
        td->hex_len = len;
        td->hex_dump = hex_dump_format((const guchar*)contents, len, 0);
    } else if (!file_kind_is_media(td->kind) && td->kind != FILE_KIND_EXECUTABLE &&
               td->kind != FILE_KIND_DOCUMENT) {
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
    }
    fclose(fp);

    if (g_task_return_error_if_cancelled(task)) {
        g_free(contents);
        return;
    }

    if (file_kind_is_media(td->kind) || td->kind == FILE_KIND_BINARY ||
        td->kind == FILE_KIND_EXECUTABLE || td->kind == FILE_KIND_DOCUMENT) {
        g_free(contents);
    } else {
        td->raw_len = len;
        if (len > 0 && !g_utf8_validate(contents, (gssize)len, NULL)) {
            td->utf8_text = g_utf8_make_valid(contents, (gssize)len);
            g_free(contents);
        } else {
            td->utf8_text = contents;
        }
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
    audio_preview_stop(app);
    video_preview_stop(app);
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }
    if (kind == FILE_KIND_PDF) {
        render_pdf_preview(app, name, full);
    } else {
        render_image_preview(app, name, full);
    }
}

/* ELF file: name and type, then the header details with dimmed field labels
 * and bold section titles (main thread) */
static void show_elf_summary(AppData* app, const TextPreviewTaskData* td) {
    GtkTextBuffer* buffer = app->buffer;
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    if (gtk_text_tag_table_lookup(table, "elf_label") == NULL) {
        gtk_text_buffer_create_tag(buffer, "elf_title", "weight", 700, "scale", 1.2, NULL);
        gtk_text_buffer_create_tag(buffer, "elf_label", "foreground", "#8b8b8b", NULL);
        gtk_text_buffer_create_tag(buffer, "elf_section", "weight", 700, NULL);
    }

    gchar* size_str = g_format_size((guint64)td->fsize);
    gchar* title = g_strdup_printf("%s\n", td->name);
    gchar* subtitle = g_strdup_printf("%s · %s\n\n", td->elf_type, size_str);
    gtk_text_buffer_set_text(buffer, "", 0);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &end, title, -1, "elf_title", NULL);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &end, subtitle, -1, "elf_label", NULL);
    gint first_line = gtk_text_iter_get_line(&end);
    gtk_text_buffer_insert(buffer, &end, td->elf_summary, -1);
    g_free(subtitle);
    g_free(title);
    g_free(size_str);

    /* Field lines: label padded to ELF_INFO_LABEL_WIDTH; list items start with
     * two spaces; any other non-empty line is a section title */
    gint n_lines = gtk_text_buffer_get_line_count(buffer);
    for (gint line = first_line; line < n_lines; line++) {
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_line(buffer, &a, line);
        b = a;
        if (!gtk_text_iter_ends_line(&b)) {
            gtk_text_iter_forward_to_line_end(&b);
        }
        gchar* text = gtk_text_iter_get_text(&a, &b);
        gsize len = strlen(text);
        if (len > ELF_INFO_LABEL_WIDTH && text[0] != ' ' &&
            text[ELF_INFO_LABEL_WIDTH - 1] == ' ' && text[ELF_INFO_LABEL_WIDTH] == ' ') {
            gtk_text_buffer_get_iter_at_line_offset(buffer, &b, line, ELF_INFO_LABEL_WIDTH);
            gtk_text_buffer_apply_tag_by_name(buffer, "elf_label", &a, &b);
        } else if (len > 0 && text[0] != ' ') {
            gtk_text_buffer_apply_tag_by_name(buffer, "elf_section", &a, &b);
        }
        g_free(text);
    }
}

/* Binary file: header, then the hex dump with dimmed offsets and a coloured
 * ASCII column (main thread) */
static void show_hex_dump(AppData* app, const TextPreviewTaskData* td) {
    GtkTextBuffer* buffer = app->buffer;
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    if (gtk_text_tag_table_lookup(table, "hex_offset") == NULL) {
        gtk_text_buffer_create_tag(buffer, "hex_offset", "foreground", "#6e7681", NULL);
        gtk_text_buffer_create_tag(buffer, "hex_ascii", "foreground", "#ce9178", NULL);
        gtk_text_buffer_create_tag(buffer, "hex_header", "foreground", "#8b8b8b", NULL);
    }

    gchar* size_str = g_format_size((guint64)td->fsize);
    GString* header = g_string_new(NULL);
    g_string_append_printf(header, "[ %s ] %s (%s)\n",
                           td->kind == FILE_KIND_EXECUTABLE ? "ELF file"
                           : td->kind == FILE_KIND_DOCUMENT ? "Word document" : "Binary file",
                           td->name, size_str);
    if (td->doc_error != NULL) {
        g_string_append_printf(header, "Cannot show the document: %s.\n", td->doc_error);
    }
    if ((gint64)td->hex_len < td->fsize) {
        gchar* shown = g_format_size((guint64)td->hex_len);
        g_string_append_printf(header, "Showing the first %s.\n", shown);
        g_free(shown);
    }
    g_string_append_c(header, '\n');
    g_free(size_str);

    gtk_text_buffer_set_text(buffer, header->str, (gint)header->len);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_apply_tag_by_name(buffer, "hex_header", &start, &end);
    gint first_line = gtk_text_iter_get_line(&end);
    g_string_free(header, TRUE);

    if (td->hex_dump == NULL || td->hex_dump[0] == '\0') {
        return;
    }
    gtk_text_buffer_insert(buffer, &end, td->hex_dump, -1);

    /* Every dump line has the same layout (see hex_dump.h) */
    gint n_lines = gtk_text_buffer_get_line_count(buffer);
    for (gint line = first_line; line < n_lines; line++) {
        GtkTextIter a, b;
        if (!gtk_text_buffer_get_iter_at_line_offset(buffer, &a, line, 0) ||
            gtk_text_iter_get_chars_in_line(&a) < HEX_DUMP_LINE_WIDTH) {
            continue;
        }
        gtk_text_buffer_get_iter_at_line_offset(buffer, &b, line, HEX_DUMP_OFFSET_WIDTH);
        gtk_text_buffer_apply_tag_by_name(buffer, "hex_offset", &a, &b);
        gtk_text_buffer_get_iter_at_line_offset(buffer, &a, line, HEX_DUMP_ASCII_COLUMN);
        gtk_text_buffer_get_iter_at_line_offset(buffer, &b, line, HEX_DUMP_LINE_WIDTH);
        gtk_text_buffer_apply_tag_by_name(buffer, "hex_ascii", &a, &b);
    }
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

    if (g_strcmp0(td->full, app->viewed_path) == 0 &&
        (app->viewed_kind != td->kind || g_strcmp0(app->viewed_content_type, td->content_type) != 0)) {
        app->viewed_kind = td->kind;
        g_free(app->viewed_content_type);
        app->viewed_content_type = g_strdup(td->content_type);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    }

    if (file_kind_is_media(td->kind)) {
        show_media_preview(app, td->kind, td->name, td->full);
        return;
    }

    audio_preview_stop(app);
    video_preview_stop(app);
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }

    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }

    gboolean is_md = (td->kind == FILE_KIND_MARKDOWN) && !td->render_raw;
    gboolean is_html = ((td->kind == FILE_KIND_HTML) && !td->render_raw) ||
                       (td->kind == FILE_KIND_DOCUMENT && td->utf8_text != NULL);
    gboolean is_hex = (td->kind == FILE_KIND_BINARY || td->kind == FILE_KIND_EXECUTABLE ||
                       td->hex_dump != NULL);
    gboolean is_org = (td->kind == FILE_KIND_ORG) && !td->render_raw;
    gboolean is_rendered = is_md || is_html || is_org;
    gboolean code = FALSE;
    if (is_hex) {
        apply_viewer_language(app, NULL, NULL);
        set_view_monospace(app->text_view, TRUE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
        set_view_text_padding(app->text_view);
    } else if (is_rendered) {
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

    app->viewer_match = -1;

    if (td->error_msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

    if (td->elf_summary != NULL) {
        show_elf_summary(app, td);
        return;
    }
    if (is_hex) {
        show_hex_dump(app, td);
        return;
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
        audio_preview_stop(app);
        video_preview_stop(app);
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
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
        audio_preview_stop(app);
        video_preview_stop(app);
        if (app->preview_stack != NULL) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
        }
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
