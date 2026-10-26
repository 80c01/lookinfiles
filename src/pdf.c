#include "pdf.h"
#include "file_list.h"
#include "viewer.h"
#include <sys/stat.h>
#include <glib/gstdio.h>

#ifdef HAVE_POPPLER
#include <poppler.h>
#include <cairo.h>

#define MAX_RENDER_PAGES 20

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full) {
    gtk_text_buffer_set_text(app->buffer, "", -1);

    struct stat st;
    gint64 fsize = (g_stat(full, &st) == 0) ? (gint64)st.st_size : 0;
    gchar* size_str = g_format_size((guint64)fsize);

    GFile* f = g_file_new_for_path(full);
    GError* err = NULL;
    PopplerDocument* doc = poppler_document_new_from_gfile(f, NULL, NULL, &err);
    g_object_unref(f);

    if (doc == NULL) {
        gchar* msg = g_strdup_printf("[ Cannot open PDF: %s (%s) ]",
                                     name, err ? err->message : "unknown error");
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        if (err != NULL) {
            g_error_free(err);
        }
        g_free(size_str);
        return;
    }

    gint total_pages = poppler_document_get_n_pages(doc);
    gchar* doc_title = poppler_document_get_title(doc);
    gchar* doc_author = poppler_document_get_author(doc);
    gchar* doc_ver = poppler_document_get_pdf_version_string(doc);

    /* Setup text tags for styling and centering */
    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(app->buffer);
    if (!gtk_text_tag_table_lookup(tags, "pdf_center")) {
        gtk_text_buffer_create_tag(app->buffer, "pdf_center",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_header")) {
        gtk_text_buffer_create_tag(app->buffer, "pdf_header",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "scale", 1.2,
                                   "foreground", "#4fc1ff",
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_meta")) {
        gtk_text_buffer_create_tag(app->buffer, "pdf_meta",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "#888888",
                                   "scale", 0.9,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_page_badge")) {
        gtk_text_buffer_create_tag(app->buffer, "pdf_page_badge",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "foreground", "#ce9178",
                                   "scale", 0.95,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_text_header")) {
        gtk_text_buffer_create_tag(app->buffer, "pdf_text_header",
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "foreground", "#4ec9b0",
                                   NULL);
    }

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    /* Top spacing */
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "pdf_center", NULL);

    /* Header title (document title or file name) */
    gchar* header_text;
    if (doc_title != NULL && doc_title[0] != '\0') {
        header_text = g_strdup_printf("📕  %s (%s)\n", doc_title, name);
    } else {
        header_text = g_strdup_printf("📕  %s\n", name);
    }
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, header_text, -1, "pdf_header", NULL);
    g_free(header_text);

    /* Metadata subtitle: pages, version, size, mode toggle hint */
    const gchar* mode_hint = app->render_raw ? "  [text mode, press m for rendered pages]"
                                             : "  [rendered pages, press m for text]";
    gchar* meta_text;
    if (doc_author != NULL && doc_author[0] != '\0') {
        meta_text = g_strdup_printf("By %s  •  PDF %s  •  %d page%s  •  %s%s\n\n",
                                    doc_author,
                                    doc_ver ? doc_ver : "1.x",
                                    total_pages, (total_pages == 1) ? "" : "s",
                                    size_str, mode_hint);
    } else {
        meta_text = g_strdup_printf("PDF %s  •  %d page%s  •  %s%s\n\n",
                                    doc_ver ? doc_ver : "1.x",
                                    total_pages, (total_pages == 1) ? "" : "s",
                                    size_str, mode_hint);
    }
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta_text, -1, "pdf_meta", NULL);
    g_free(meta_text);

    if (!app->render_raw) {
        /* Rendered pages mode: draw each page via Cairo into a texture */
        gint alloc_w = gtk_widget_get_width(app->text_view);
        gint max_w = (alloc_w > 120) ? (alloc_w - 60) : 700;

        gint render_count = MIN(total_pages, MAX_RENDER_PAGES);
        for (gint i = 0; i < render_count; i++) {
            PopplerPage* page = poppler_document_get_page(doc, i);
            if (page == NULL) {
                continue;
            }

            double pw = 0.0, ph = 0.0;
            poppler_page_get_size(page, &pw, &ph);

            double scale = (pw > 0.0) ? ((double)max_w / pw) : 1.0;
            scale = CLAMP(scale, 0.25, 2.5);
            gint rw = MAX(1, (gint)(pw * scale));
            gint rh = MAX(1, (gint)(ph * scale));

            cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
            cairo_t* cr = cairo_create(surface);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* white background for PDF page */
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
                /* Page badge separator */
                gchar* page_badge = g_strdup_printf("─── Page %d of %d ───\n\n",
                                                    i + 1, total_pages);
                gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter,
                                                         page_badge, -1,
                                                         "pdf_page_badge", NULL);
                g_free(page_badge);

                /* Insert rendered page texture */
                gtk_text_buffer_insert_paintable(app->buffer, &iter, GDK_PAINTABLE(tex));
                g_object_unref(tex);

                GtkTextIter start_img = iter;
                gtk_text_iter_backward_char(&start_img);
                gtk_text_buffer_apply_tag_by_name(app->buffer, "pdf_center", &start_img, &iter);

                gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "pdf_center", NULL);
            }
        }

        if (total_pages > render_count) {
            gchar* notice = g_strdup_printf(
                "[ Showing first %d of %d pages • press m to view all extracted text ]\n\n",
                render_count, total_pages);
            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, notice, -1, "pdf_meta", NULL);
            g_free(notice);
        }
    } else {
        /* Text extraction mode: extract text per page */
        for (gint i = 0; i < total_pages; i++) {
            PopplerPage* page = poppler_document_get_page(doc, i);
            if (page == NULL) {
                continue;
            }

            gchar* text = poppler_page_get_text(page);
            g_object_unref(page);

            gchar* page_hdr = g_strdup_printf("--- Page %d of %d ---\n\n", i + 1, total_pages);
            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, page_hdr, -1,
                                                     "pdf_text_header", NULL);
            g_free(page_hdr);

            if (text != NULL && text[0] != '\0') {
                gtk_text_buffer_insert(app->buffer, &iter, text, -1);
            } else {
                gtk_text_buffer_insert(app->buffer, &iter, "(no text on this page)\n", -1);
            }
            g_free(text);
            gtk_text_buffer_insert(app->buffer, &iter, "\n\n", -1);
        }
    }

    g_free(doc_title);
    g_free(doc_author);
    g_free(doc_ver);
    g_free(size_str);
    g_object_unref(doc);
}

#else /* !HAVE_POPPLER */

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full) {
    (void)full;
    gchar* msg = g_strdup_printf(
        "[ PDF preview disabled for %s:\n  install libpoppler-glib-dev then rebuild (make). ]",
        name);
    gtk_text_buffer_set_text(app->buffer, msg, -1);
    g_free(msg);
}

#endif /* HAVE_POPPLER */
