#include "pdf.h"
#include "viewer.h"
#include <sys/stat.h>
#include <glib/gstdio.h>

#ifdef HAVE_POPPLER
#include <poppler.h>
#include <cairo.h>

#define MAX_RENDER_PAGES 20

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean render_raw;
    gint max_w;

    /* Results populated by worker thread */
    gchar* error_msg;
    gchar* size_str;
    gint total_pages;
    gchar* doc_title;
    gchar* doc_author;
    gchar* doc_ver;
    GPtrArray* page_textures; /* GdkTexture* elements */
    GPtrArray* page_texts;    /* gchar* elements */
} PdfTaskData;

static void pdf_task_data_free(gpointer data) {
    PdfTaskData* td = (PdfTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->name);
    g_free(td->full);
    g_free(td->error_msg);
    g_free(td->size_str);
    g_free(td->doc_title);
    g_free(td->doc_author);
    g_free(td->doc_ver);
    if (td->page_textures != NULL) {
        g_ptr_array_unref(td->page_textures);
    }
    if (td->page_texts != NULL) {
        g_ptr_array_unref(td->page_texts);
    }
    g_free(td);
}

static void ensure_pdf_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(tags, "pdf_center")) {
        gtk_text_buffer_create_tag(buffer, "pdf_center",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_header")) {
        gtk_text_buffer_create_tag(buffer, "pdf_header",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "scale", 1.2,
                                   "foreground", "#4fc1ff",
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_meta")) {
        gtk_text_buffer_create_tag(buffer, "pdf_meta",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "#888888",
                                   "scale", 0.9,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_page_badge")) {
        gtk_text_buffer_create_tag(buffer, "pdf_page_badge",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "foreground", "#ce9178",
                                   "scale", 0.95,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_text_header")) {
        gtk_text_buffer_create_tag(buffer, "pdf_text_header",
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "foreground", "#4ec9b0",
                                   NULL);
    }
}

static void pdf_preview_thread_func(GTask* task, gpointer source_object,
                                    gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    PdfTaskData* td = (PdfTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    struct stat st;
    gint64 fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : 0;
    td->size_str = g_format_size((guint64)fsize);

    GFile* f = g_file_new_for_path(td->full);
    GError* err = NULL;
    PopplerDocument* doc = poppler_document_new_from_gfile(f, NULL, cancellable, &err);
    g_object_unref(f);

    if (g_task_return_error_if_cancelled(task)) {
        if (doc != NULL) {
            g_object_unref(doc);
        }
        if (err != NULL) {
            g_error_free(err);
        }
        return;
    }

    if (doc == NULL) {
        td->error_msg = g_strdup_printf("[ Cannot open PDF: %s (%s) ]",
                                        td->name, err ? err->message : "unknown error");
        if (err != NULL) {
            g_error_free(err);
        }
        g_task_return_boolean(task, TRUE);
        return;
    }

    td->total_pages = poppler_document_get_n_pages(doc);
    td->doc_title = poppler_document_get_title(doc);
    td->doc_author = poppler_document_get_author(doc);
    td->doc_ver = poppler_document_get_pdf_version_string(doc);

    if (!td->render_raw) {
        td->page_textures = g_ptr_array_new_with_free_func(g_object_unref);
        gint render_count = MIN(td->total_pages, MAX_RENDER_PAGES);
        for (gint i = 0; i < render_count; i++) {
            if (g_cancellable_is_cancelled(cancellable)) {
                break;
            }
            PopplerPage* page = poppler_document_get_page(doc, i);
            if (page == NULL) {
                continue;
            }

            double pw = 0.0, ph = 0.0;
            poppler_page_get_size(page, &pw, &ph);

            double scale = (pw > 0.0) ? ((double)td->max_w / pw) : 1.0;
            scale = CLAMP(scale, 0.25, 2.5);
            gint rw = MAX(1, (gint)(pw * scale));
            gint rh = MAX(1, (gint)(ph * scale));

            cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
            cairo_t* cr = cairo_create(surface);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_paint(cr);
            cairo_scale(cr, scale, scale);
            poppler_page_render(page, cr);
            cairo_destroy(cr);

            cairo_surface_flush(surface);
            gint stride = cairo_image_surface_get_stride(surface);
            GBytes* bytes = g_bytes_new(cairo_image_surface_get_data(surface),
                                        (gsize)stride * (gsize)rh);
            GdkTexture* tex = gdk_memory_texture_new(
                rw, rh, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, (gsize)stride);
            g_bytes_unref(bytes);
            cairo_surface_destroy(surface);
            g_object_unref(page);

            if (tex != NULL) {
                g_ptr_array_add(td->page_textures, tex);
            }
        }
    } else {
        td->page_texts = g_ptr_array_new_with_free_func(g_free);
        for (gint i = 0; i < td->total_pages; i++) {
            if (g_cancellable_is_cancelled(cancellable)) {
                break;
            }
            PopplerPage* page = poppler_document_get_page(doc, i);
            if (page == NULL) {
                g_ptr_array_add(td->page_texts, g_strdup(""));
                continue;
            }
            gchar* raw_text = poppler_page_get_text(page);
            g_object_unref(page);

            if (raw_text != NULL && !g_utf8_validate(raw_text, -1, NULL)) {
                gchar* valid = g_utf8_make_valid(raw_text, -1);
                g_free(raw_text);
                raw_text = valid;
            }
            g_ptr_array_add(td->page_texts, raw_text ? raw_text : g_strdup(""));
        }
    }

    g_object_unref(doc);

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

static void on_pdf_preview_ready(GObject* source_object, GAsyncResult* res,
                                 gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    PdfTaskData* td = (PdfTaskData*)g_task_get_task_data(task);

    if (td == NULL || g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->preview_cancellable) {
        return;
    }

    AppData* app = td->app;

    apply_viewer_language(app, NULL);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
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

    ensure_pdf_tags(app->buffer);

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    /* Top spacing */
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "pdf_center", NULL);

    /* Header title (document title or file name) */
    gchar* header_text;
    if (td->doc_title != NULL && td->doc_title[0] != '\0') {
        header_text = g_strdup_printf("📕  %s (%s)\n", td->doc_title, td->name);
    } else {
        header_text = g_strdup_printf("📕  %s\n", td->name);
    }
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, header_text, -1, "pdf_header", NULL);
    g_free(header_text);

    /* Metadata subtitle: pages, version, size, mode toggle hint */
    const gchar* mode_hint = td->render_raw ? "  [text mode, press m for rendered pages]"
                                            : "  [rendered pages, press m for text]";
    gchar* meta_text;
    if (td->doc_author != NULL && td->doc_author[0] != '\0') {
        meta_text = g_strdup_printf("By %s  •  PDF %s  •  %d page%s  •  %s%s\n\n",
                                    td->doc_author,
                                    td->doc_ver ? td->doc_ver : "1.x",
                                    td->total_pages, (td->total_pages == 1) ? "" : "s",
                                    td->size_str, mode_hint);
    } else {
        meta_text = g_strdup_printf("PDF %s  •  %d page%s  •  %s%s\n\n",
                                    td->doc_ver ? td->doc_ver : "1.x",
                                    td->total_pages, (td->total_pages == 1) ? "" : "s",
                                    td->size_str, mode_hint);
    }
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta_text, -1, "pdf_meta", NULL);
    g_free(meta_text);

    if (!td->render_raw && td->page_textures != NULL) {
        guint render_count = td->page_textures->len;
        for (guint i = 0; i < render_count; i++) {
            GdkTexture* tex = GDK_TEXTURE(g_ptr_array_index(td->page_textures, i));
            gchar* page_badge = g_strdup_printf("─── Page %u of %d ───\n\n",
                                                i + 1, td->total_pages);
            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter,
                                                     page_badge, -1,
                                                     "pdf_page_badge", NULL);
            g_free(page_badge);

            gtk_text_buffer_insert_paintable(app->buffer, &iter, GDK_PAINTABLE(tex));

            GtkTextIter start_img = iter;
            gtk_text_iter_backward_char(&start_img);
            gtk_text_buffer_apply_tag_by_name(app->buffer, "pdf_center", &start_img, &iter);

            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "pdf_center", NULL);
        }

        if (td->total_pages > (gint)render_count) {
            gchar* notice = g_strdup_printf(
                "[ Showing first %u of %d pages • press m to view all extracted text ]\n\n",
                render_count, td->total_pages);
            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, notice, -1, "pdf_meta", NULL);
            g_free(notice);
        }
    } else if (td->render_raw && td->page_texts != NULL) {
        for (guint i = 0; i < td->page_texts->len; i++) {
            const gchar* text = (const gchar*)g_ptr_array_index(td->page_texts, i);
            gchar* page_hdr = g_strdup_printf("--- Page %u of %d ---\n\n",
                                              i + 1, td->total_pages);
            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, page_hdr, -1,
                                                     "pdf_text_header", NULL);
            g_free(page_hdr);

            if (text != NULL && text[0] != '\0') {
                gtk_text_buffer_insert(app->buffer, &iter, text, -1);
            } else {
                gtk_text_buffer_insert(app->buffer, &iter, "(no text on this page)\n", -1);
            }
            gtk_text_buffer_insert(app->buffer, &iter, "\n\n", -1);
        }
    }
}

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    gint alloc_w = gtk_widget_get_width(app->text_view);
    gint max_w = (alloc_w > 120) ? (alloc_w - 60) : 700;

    PdfTaskData* td = g_new0(PdfTaskData, 1);
    td->app = app;
    td->name = g_strdup(name);
    td->full = g_strdup(full);
    td->render_raw = app->render_raw;
    td->max_w = max_w;

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_pdf_preview_ready, NULL);
    g_task_set_task_data(task, td, pdf_task_data_free);
    g_task_run_in_thread(task, pdf_preview_thread_func);
    g_object_unref(task);
}

#else /* !HAVE_POPPLER */

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full) {
    (void)full;
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    gchar* msg = g_strdup_printf(
        "[ PDF preview disabled for %s:\n  install libpoppler-glib-dev then rebuild (make). ]",
        name);
    gtk_text_buffer_set_text(app->buffer, msg, -1);
    g_free(msg);
}

#endif /* HAVE_POPPLER */
