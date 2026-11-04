#include "pdf.h"
#include "app_events.h"
#include "fonts.h"
#include "text_tags.h"
#include "icons.h"
#include "text_style.h"
#include "file_types.h"
#include "ps_convert.h"
#include <sys/stat.h>
#include <glib/gstdio.h>

#ifdef HAVE_POPPLER
#include <poppler.h>
#include <cairo.h>

#define MAX_RENDER_PAGES 20
/* Pages rendered before the preview is first shown; the others follow */
#define FIRST_RENDER_PAGES 3

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean render_raw;
    gint max_w;
    gint first_page;       /* first page rendered by this task */
    gint page_count;       /* pages rendered by this task at most */
    gboolean converted;    /* PostScript or DVI shown through a PDF conversion */
    gboolean is_dvi;

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
        text_tag_create(buffer, "pdf_header",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "scale", FONT_SCALE_TITLE,
                                   "foreground", "@fg_strong",
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_meta")) {
        text_tag_create(buffer, "pdf_meta",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "@fg_subtle",
                                   "scale", FONT_SCALE_SMALL,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_page_badge")) {
        text_tag_create(buffer, "pdf_page_badge",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "foreground", "@fg_muted",
                                   "scale", FONT_SCALE_SMALL,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "pdf_text_header")) {
        text_tag_create(buffer, "pdf_text_header",
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "foreground", "@fg_strong",
                                   NULL);
    }
}

/* PostScript or DVI (from the content, then the name): FALSE for a real PDF */
static gboolean sniff_ps_source(const gchar* full, const gchar* name, PsSource* source) {
    guchar head[16] = {0};
    gsize len = 0;
    FILE* fp = g_fopen(full, "rb");
    if (fp != NULL) {
        len = fread(head, 1, sizeof(head), fp);
        fclose(fp);
    }
    if (len >= 5 && memcmp(head, "%PDF-", 5) == 0) {
        return FALSE;
    }
    if (head_is_dvi(name, head, len) || (len == 0 && is_dvi_file(name))) {
        *source = PS_SOURCE_DVI;
        return TRUE;
    }
    if (head_is_postscript(head, len) || (len == 0 && is_postscript_file(name))) {
        *source = PS_SOURCE_POSTSCRIPT;
        return TRUE;
    }
    return FALSE;
}

/* Text mode of a PostScript file: its source */
static gchar* read_source_head(const gchar* full) {
    gchar* contents = NULL;
    gsize len = 0;
    FILE* fp = g_fopen(full, "rb");
    if (fp == NULL) {
        return NULL;
    }
    contents = g_malloc(PREVIEW_BYTES + 1);
    len = fread(contents, 1, PREVIEW_BYTES, fp);
    fclose(fp);
    contents[len] = '\0';
    gchar* valid = g_utf8_make_valid(contents, (gssize)len);
    g_free(contents);
    return valid;
}

static void pdf_preview_thread_func(GTask* task, gpointer source_object,
                                    gpointer task_data, GCancellable* cancellable);
static void on_pdf_preview_ready(GObject* source_object, GAsyncResult* res, gpointer user_data);

/* Append the rendered pages of a task at the end of the preview, then start
 * the task of the next pages (or say how many pages are not shown) */
static void append_pdf_pages(AppData* app, PdfTaskData* td) {
    if (td->error_msg != NULL || td->page_textures == NULL) {
        return;
    }
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);
    guint render_count = td->page_textures->len;
    for (guint i = 0; i < render_count; i++) {
        GdkTexture* tex = GDK_TEXTURE(g_ptr_array_index(td->page_textures, i));
        gchar* page_badge = g_strdup_printf("─── Page %d of %d ───\n\n",
                                            td->first_page + (gint)i + 1, td->total_pages);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, page_badge, -1, "pdf_page_badge", NULL);
        g_free(page_badge);

        gtk_text_buffer_insert_paintable(app->buffer, &iter, GDK_PAINTABLE(tex));
        GtkTextIter start_img = iter;
        gtk_text_iter_backward_char(&start_img);
        gtk_text_buffer_apply_tag_by_name(app->buffer, "pdf_center", &start_img, &iter);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "pdf_center", NULL);
    }
    gint shown = td->first_page + (gint)render_count;
    gint limit = MIN(td->total_pages, MAX_RENDER_PAGES);
    if (shown < limit && render_count > 0) {
        /* The next pages, in the background (same cancellable: a new selection drops them) */
        PdfTaskData* next = g_new0(PdfTaskData, 1);
        next->app = app;
        next->name = g_strdup(td->name);
        next->full = g_strdup(td->full);
        next->render_raw = FALSE;
        next->max_w = td->max_w;
        next->first_page = shown;
        next->page_count = MAX_RENDER_PAGES;
        next->converted = td->converted;
        next->is_dvi = td->is_dvi;
        GTask* task = g_task_new(NULL, app->preview_cancellable, on_pdf_preview_ready, NULL);
        g_task_set_task_data(task, next, pdf_task_data_free);
        g_task_run_in_thread(task, pdf_preview_thread_func);
        g_object_unref(task);
        return;
    }
    if (td->total_pages > limit) {
        gchar* notice = g_strdup_printf("[ Showing first %d of %d pages • press m to view all extracted text ]\n\n",
                                        limit, td->total_pages);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, notice, -1, "pdf_meta", NULL);
        g_free(notice);
    }
}

static void pdf_preview_thread_func(GTask* task, gpointer source_object,
                                    gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    PdfTaskData* td = (PdfTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    GStatBuf st;
    gint64 fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : 0;
    td->size_str = g_format_size((guint64)fsize);

    GError* err = NULL;
    gchar* open_path = NULL;
    PsSource ps_source;
    if (sniff_ps_source(td->full, td->name, &ps_source)) {
        td->converted = TRUE;
        td->is_dvi = ps_source == PS_SOURCE_DVI;
        if (td->render_raw && ps_source == PS_SOURCE_POSTSCRIPT) {
            /* Text mode: the PostScript source itself */
            gchar* src = read_source_head(td->full);
            td->page_texts = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(td->page_texts, src != NULL ? src : g_strdup(""));
            td->total_pages = 1;
            g_task_return_boolean(task, TRUE);
            return;
        }
        open_path = ps_convert_to_pdf(td->full, ps_source, MAX_RENDER_PAGES, cancellable, &err);
        if (open_path == NULL) {
            if (g_task_return_error_if_cancelled(task)) {
                g_clear_error(&err);
                return;
            }
            td->error_msg = g_strdup_printf("[ Cannot preview %s: %s ]", td->name,
                                            err ? err->message : "conversion failed");
            g_clear_error(&err);
            g_task_return_boolean(task, TRUE);
            return;
        }
    }
    GFile* f = g_file_new_for_path(open_path != NULL ? open_path : td->full);
    PopplerDocument* doc = poppler_document_new_from_gfile(f, NULL, cancellable, &err);
    g_object_unref(f);
    g_free(open_path);

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
        gint render_end = MIN(MIN(td->total_pages, MAX_RENDER_PAGES), td->first_page + td->page_count);
        for (gint i = td->first_page; i < render_end; i++) {
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

    if (td->first_page > 0) {
        append_pdf_pages(app, td);
        return;
    }

    apply_viewer_language(app, NULL, NULL);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
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

    ensure_pdf_tags(app->buffer);

    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    /* Top spacing */
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "pdf_center", NULL);

    /* Header title (document title or file name) */
    gchar* header_text;
    if (td->doc_title != NULL && td->doc_title[0] != '\0') {
        header_text = g_strdup_printf("%s (%s)\n", td->doc_title, td->name);
    } else {
        header_text = g_strdup_printf("%s\n", td->name);
    }
    text_insert_file_icon(app->buffer, &iter, td->name, FALSE, "pdf_header");
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, header_text, -1, "pdf_header", NULL);
    g_free(header_text);

    /* Metadata subtitle: pages, version, size, mode toggle hint */
    const gchar* mode_hint = td->render_raw ? "  [text mode, press m for rendered pages]"
                                            : "  [rendered pages, press m for text]";
    gchar* type_text = td->converted
        ? g_strdup(td->is_dvi ? "DVI" : "PostScript")
        : g_strdup_printf("PDF %s", td->doc_ver ? td->doc_ver : "1.x");
    gchar* pages_text = (td->converted && td->total_pages >= MAX_RENDER_PAGES)
        ? g_strdup_printf("first %d pages", MAX_RENDER_PAGES)
        : g_strdup_printf("%d page%s", td->total_pages, (td->total_pages == 1) ? "" : "s");
    gchar* meta_text;
    if (td->doc_author != NULL && td->doc_author[0] != '\0') {
        meta_text = g_strdup_printf("By %s  •  %s  •  %s  •  %s%s\n\n",
                                    td->doc_author, type_text, pages_text, td->size_str, mode_hint);
    } else {
        meta_text = g_strdup_printf("%s  •  %s  •  %s%s\n\n",
                                    type_text, pages_text, td->size_str, mode_hint);
    }
    g_free(type_text);
    g_free(pages_text);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta_text, -1, "pdf_meta", NULL);
    g_free(meta_text);

    if (!td->render_raw && td->page_textures != NULL) {
        append_pdf_pages(app, td);
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

gboolean pdf_preview_jump(AppData* app, gint direction) {
    if (app->viewed_kind != FILE_KIND_PDF || app->render_raw) {
        return FALSE;
    }
    /* The page badges, from the first: the next one below the scroll
     * position, or the last one above it. Vertical scroll only (the badges
     * are centered: scroll_to_iter would also scroll sideways). */
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gdouble current = gtk_adjustment_get_value(vadj);
    GtkTextIter from, match_start, match_end;
    gtk_text_buffer_get_start_iter(app->buffer, &from);
    gdouble target = direction > 0 ? -1 : 0;
    while (gtk_text_iter_forward_search(&from, "─── Page ", GTK_TEXT_SEARCH_TEXT_ONLY, &match_start, &match_end,
                                        NULL)) {
        GdkRectangle rect;
        gtk_text_view_get_iter_location(view, &match_start, &rect);
        if (direction > 0 && rect.y > current + 2) {
            target = rect.y;
            break;
        }
        if (direction < 0 && rect.y < current - 2) {
            target = rect.y;
        }
        from = match_end;
    }
    if (target >= 0) {
        gtk_adjustment_set_value(vadj, target);
    }
    return TRUE;
}

/* Width the pages are fitted to. Measure the viewport, not the text view: the
 * view can be wider than the scrolled window when the previous file had long
 * unwrapped lines. */
static gint pdf_fit_width(AppData* app) {
    GtkWidget* scroller = gtk_widget_get_ancestor(app->text_view, GTK_TYPE_SCROLLED_WINDOW);
    gint alloc_w = gtk_widget_get_width(scroller != NULL ? scroller : app->text_view);
    /* The minimap appears once the tall content is loaded and then takes its
     * width from the viewport: reserve it up front. */
    if (app->minimap != NULL && !gtk_widget_get_visible(app->minimap)) {
        alloc_w -= MINIMAP_WIDTH;
    }
    return (alloc_w > 120) ? (alloc_w - 60) : 700;
}

static gint last_fit_w = 0; /* width of the last PDF render */

static guint refit_timeout_id = 0;

static gboolean refit_timeout_cb(gpointer data) {
    refit_timeout_id = 0;
    pdf_preview_refit((AppData*)data);
    return G_SOURCE_REMOVE;
}

/* The viewport was resized (editor pane opened, pane dragged...): refit once
 * the size has settled. */
static void on_viewport_resized(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    if (refit_timeout_id != 0) {
        g_source_remove(refit_timeout_id);
    }
    refit_timeout_id = g_timeout_add(150, refit_timeout_cb, data);
}

static void watch_viewport(AppData* app) {
    static gboolean watched = FALSE;
    GtkWidget* scroller = gtk_widget_get_ancestor(app->text_view, GTK_TYPE_SCROLLED_WINDOW);
    if (watched || scroller == NULL) {
        return;
    }
    watched = TRUE;
    GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroller));
    g_signal_connect(hadj, "notify::page-size", G_CALLBACK(on_viewport_resized), app);
}

void pdf_preview_refit(AppData* app) {
    if (app->viewed_kind != FILE_KIND_PDF || app->render_raw || app->text_view == NULL) {
        return;
    }
    gint w = pdf_fit_width(app);
    if (w != last_fit_w && w > 700) {
        app_emit(app, APP_EVENT_PREVIEW_STALE);
    }
}

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    watch_viewport(app);
    gint max_w = pdf_fit_width(app);
    last_fit_w = max_w;

    PdfTaskData* td = g_new0(PdfTaskData, 1);
    td->app = app;
    td->name = g_strdup(name);
    td->full = g_strdup(full);
    td->render_raw = app->render_raw;
    /* Zoomed pages are at most 4096 px wide (memory: up to MAX_RENDER_PAGES textures) */
    td->max_w = MIN((gint)(max_w * (app->preview_zoom > 0 ? app->preview_zoom : 1.0)), 4096);
    td->first_page = 0;
    td->page_count = FIRST_RENDER_PAGES;

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_pdf_preview_ready, NULL);
    g_task_set_task_data(task, td, pdf_task_data_free);
    g_task_run_in_thread(task, pdf_preview_thread_func);
    g_object_unref(task);
}

#else /* !HAVE_POPPLER */

void pdf_preview_refit(AppData* app) {
    (void)app;
}

gboolean pdf_preview_jump(AppData* app, gint direction) {
    (void)app;
    (void)direction;
    return FALSE;
}

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
