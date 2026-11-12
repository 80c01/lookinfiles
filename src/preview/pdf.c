#include "preview/pdf.h"
#include "preview/preview_state.h"
#include "search/search.h"
#include "core/i18n.h"
#include "core/app_events.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"
#include "theme/icons.h"
#include "theme/text_style.h"
#include "core/file_types.h"
#include "preview/ps_convert.h"
#include "code/code_outline_tree.h"
#include "preview/preview_tagbar.h"
#include "widgets/minimap.h"
#include <sys/stat.h>
#include <glib/gstdio.h>

#ifdef HAVE_POPPLER
#include <poppler.h>
#include <cairo.h>

#define MAX_RENDER_PAGES 20
/* Start of the page badges, found again by PgUp / PgDn (not translated) */
#define PAGE_BADGE_PREFIX "─── "
/* Pages rendered before the preview is first shown; the others follow */
#define FIRST_RENDER_PAGES 3
/* Pages rendered by each next task: they show as they come */
#define NEXT_RENDER_PAGES 3
/* Threads rendering the pages of a task at most, each with its own
 * PopplerDocument (one document is not safe to render from two threads) */
#define MAX_RENDER_THREADS 3
/* Bookmarks read for the tagbar at most */
#define MAX_BOOKMARKS 2000

/* The document of a preview, opened by its first task and reused by the
 * tasks of the next pages (one task at a time) */
typedef struct {
    gatomicrefcount ref;
    gchar* path;              /* the PDF opened (a conversion for PostScript / DVI) */
    GPtrArray* docs;          /* PopplerDocument* (NULL: not opened yet), one per render thread */
} PdfSession;

static void doc_clear(gpointer data) {
    if (data != NULL) {
        g_object_unref(data);
    }
}

static PopplerDocument* pdf_open(const gchar* path, GCancellable* cancellable, GError** err) {
    GFile* f = g_file_new_for_path(path);
    PopplerDocument* doc = poppler_document_new_from_gfile(f, NULL, cancellable, err);
    g_object_unref(f);
    return doc;
}

static PdfSession* pdf_session_new(const gchar* path, PopplerDocument* doc) {
    PdfSession* s = g_new0(PdfSession, 1);
    g_atomic_ref_count_init(&s->ref);
    s->path = g_strdup(path);
    s->docs = g_ptr_array_new_with_free_func(doc_clear);
    g_ptr_array_add(s->docs, doc);
    return s;
}

static PdfSession* pdf_session_ref(PdfSession* s) {
    g_atomic_ref_count_inc(&s->ref);
    return s;
}

static void pdf_session_unref(PdfSession* s) {
    if (g_atomic_ref_count_dec(&s->ref)) {
        g_free(s->path);
        g_ptr_array_unref(s->docs);
        g_free(s);
    }
}

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
    PdfSession* session;   /* the open document (set by the first task) */
    gboolean read_marks;   /* this task reads the bookmarks (after its pages) */

    /* Results populated by worker thread */
    gchar* error_msg;
    gchar* size_str;
    gint total_pages;
    gchar* doc_title;
    gchar* doc_author;
    gchar* doc_ver;
    GPtrArray* page_textures; /* GdkTexture* elements */
    GPtrArray* page_texts;    /* gchar* elements */
    gboolean text_cut;        /* page_texts stops before the end (PREVIEW_BYTES) */
    GPtrArray* marks;         /* PdfMark* (first task only), or NULL */
} PdfTaskData;

/* A bookmark of the document outline */
typedef struct {
    gchar* title;
    gint level;               /* 1: top level */
    gint page;                /* 0-based, -1: none (a group of bookmarks) */
} PdfMark;

static void pdf_mark_free(gpointer data) {
    PdfMark* m = data;
    g_free(m->title);
    g_free(m);
}

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
    if (td->marks != NULL) {
        g_ptr_array_unref(td->marks);
    }
    if (td->session != NULL) {
        pdf_session_unref(td->session);
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

/* ---- Bookmarks for the tagbar ---- */

/* 0-based page of a destination, -1 when unknown */
static gint dest_page(PopplerDocument* doc, const PopplerDest* dest) {
    if (dest->type == POPPLER_DEST_NAMED) {
        PopplerDest* named = dest->named_dest != NULL ? poppler_document_find_dest(doc, dest->named_dest) : NULL;
        gint page = named != NULL ? named->page_num - 1 : -1;
        if (named != NULL) poppler_dest_free(named);
        return page;
    }
    return dest->page_num - 1;
}

/* The outline from iter on, depth first (worker thread) */
static void read_outline(PopplerDocument* doc, PopplerIndexIter* iter, gint level, GPtrArray* out) {
    do {
        if (out->len >= MAX_BOOKMARKS) return;
        PopplerAction* action = poppler_index_iter_get_action(iter);
        if (action != NULL) {
            gint page = -1;
            if (action->type == POPPLER_ACTION_GOTO_DEST && action->goto_dest.dest != NULL) {
                page = dest_page(doc, action->goto_dest.dest);
            }
            if (action->any.title != NULL && action->any.title[0] != '\0') {
                PdfMark* m = g_new0(PdfMark, 1);
                m->title = g_utf8_make_valid(action->any.title, -1);
                m->level = MIN(level, 9);
                m->page = page;
                g_ptr_array_add(out, m);
            }
            poppler_action_free(action);
        }
        PopplerIndexIter* child = poppler_index_iter_get_child(iter);
        if (child != NULL) {
            read_outline(doc, child, level + 1, out);
            poppler_index_iter_free(child);
        }
    } while (poppler_index_iter_next(iter));
}

/* The bookmarks of the preview shown, and the buffer line of each page
 * shown so far (-1: not shown) */
static struct {
    GCancellable* owner;
    GPtrArray* marks;
    GArray* page_lines;
} outline;

/* The bookmarks read by a task, for the preview shown */
static void outline_take_marks(AppData* app, PdfTaskData* td) {
    if (outline.owner != app->preview_cancellable || outline.marks != NULL || td->marks == NULL ||
        td->marks->len == 0) {
        return;
    }
    outline.marks = g_steal_pointer(&td->marks);
}

static void outline_reset(AppData* app, PdfTaskData* td) {
    g_clear_object(&outline.owner);
    g_clear_pointer(&outline.marks, g_ptr_array_unref);
    g_clear_pointer(&outline.page_lines, g_array_unref);
    if (td->total_pages <= 0) return;
    outline.owner = g_object_ref(app->preview_cancellable);
    outline_take_marks(app, td);
    outline.page_lines = g_array_sized_new(FALSE, FALSE, sizeof(gint), (guint)td->total_pages);
    for (gint i = 0; i < td->total_pages; i++) {
        gint none = -1;
        g_array_append_val(outline.page_lines, none);
    }
}

static void outline_note_page(GCancellable* owner, gint page, const GtkTextIter* at) {
    if (outline.owner != owner || outline.page_lines == NULL || page < 0 || (guint)page >= outline.page_lines->len) {
        return;
    }
    g_array_index(outline.page_lines, gint, page) = gtk_text_iter_get_line(at);
}

/* Hand the bookmarks of the pages shown to the tagbar; a group without a
 * page of its own goes to the page of the next bookmark */
static void outline_publish(AppData* app) {
    if (outline.owner == NULL || outline.owner != app->preview_cancellable || outline.marks == NULL) return;
    GPtrArray* entries = code_outline_entries_new();
    gint next_page = -1;
    gint* pages = g_new(gint, outline.marks->len);
    for (guint k = outline.marks->len; k > 0; k--) {
        const PdfMark* m = g_ptr_array_index(outline.marks, k - 1);
        if (m->page >= 0) next_page = m->page;
        pages[k - 1] = m->page >= 0 ? m->page : next_page;
    }
    for (guint i = 0; i < outline.marks->len; i++) {
        const PdfMark* m = g_ptr_array_index(outline.marks, i);
        gint page = pages[i];
        if (page < 0 || (guint)page >= outline.page_lines->len) continue;
        gint line = g_array_index(outline.page_lines, gint, page);
        if (line < 0) continue;
        CodeOutlineEntry* e = code_outline_heading_new(m->title, m->level, line);
        if (e == NULL) continue;
        e->column = (gint)i; /* keeps the outline order on a page */
        g_ptr_array_add(entries, e);
    }
    g_free(pages);
    preview_tagbar_set_outline(app, outline.owner, entries);
    g_ptr_array_unref(entries);
}

/* Append the rendered pages of a task at the end of the preview, then start
 * the task of the next pages (or say how many pages are not shown) */
static void append_pdf_pages(AppData* app, PdfTaskData* td) {
    if (td->error_msg != NULL || td->page_textures == NULL) {
        return;
    }
    outline_take_marks(app, td);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);
    guint render_count = td->page_textures->len;
    for (guint i = 0; i < render_count; i++) {
        GdkTexture* tex = GDK_TEXTURE(g_ptr_array_index(td->page_textures, i));
        outline_note_page(app->preview_cancellable, td->first_page + (gint)i, &iter);
        gchar* page = g_strdup_printf(_("Page %1$d of %2$d"), td->first_page + (gint)i + 1, td->total_pages);
        gchar* page_badge = g_strdup_printf(PAGE_BADGE_PREFIX "%s ───\n\n", page);
        g_free(page);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, page_badge, -1, "pdf_page_badge", NULL);
        g_free(page_badge);

        gtk_text_buffer_insert_paintable(app->buffer, &iter, GDK_PAINTABLE(tex));
        GtkTextIter start_img = iter;
        gtk_text_iter_backward_char(&start_img);
        gtk_text_buffer_apply_tag_by_name(app->buffer, "pdf_center", &start_img, &iter);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "pdf_center", NULL);
    }
    outline_publish(app);
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
        next->page_count = NEXT_RENDER_PAGES;
        next->converted = td->converted;
        next->is_dvi = td->is_dvi;
        next->session = pdf_session_ref(td->session);
        next->total_pages = td->total_pages;
        next->read_marks = td->first_page == 0 && !td->read_marks;
        GTask* task = g_task_new(NULL, app->preview_cancellable, on_pdf_preview_ready, NULL);
        g_task_set_task_data(task, next, pdf_task_data_free);
        g_task_run_in_thread(task, pdf_preview_thread_func);
        g_object_unref(task);
        return;
    }
    if (td->total_pages > limit) {
        gchar* notice = g_strdup_printf(_("[ Showing first %1$d of %2$d pages • press m for the extracted text ]\n\n"),
                                        limit, td->total_pages);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, notice, -1, "pdf_meta", NULL);
        g_free(notice);
    }
}

/* Open the document (PostScript and DVI: their PDF conversion) into
 * td->session; FALSE when the task got its result (error, cancelled, the
 * PostScript source in text mode) */
static gboolean open_document(GTask* task, PdfTaskData* td, GCancellable* cancellable) {
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
            return FALSE;
        }
        open_path = ps_convert_to_pdf(td->full, ps_source, MAX_RENDER_PAGES, cancellable, &err);
        if (open_path == NULL) {
            if (g_task_return_error_if_cancelled(task)) {
                g_clear_error(&err);
                return FALSE;
            }
            td->error_msg = g_strdup_printf(_("[ Cannot preview %1$s: %2$s ]"), td->name,
                                            err ? err->message : _("conversion failed"));
            g_clear_error(&err);
            g_task_return_boolean(task, TRUE);
            return FALSE;
        }
    } else {
        open_path = g_strdup(td->full);
    }
    PopplerDocument* doc = pdf_open(open_path, cancellable, &err);

    if (g_task_return_error_if_cancelled(task)) {
        g_clear_object(&doc);
        g_clear_error(&err);
        g_free(open_path);
        return FALSE;
    }
    if (doc == NULL) {
        td->error_msg = g_strdup_printf(_("[ Cannot open PDF: %1$s (%2$s) ]"),
                                        td->name, err ? err->message : _("unknown error"));
        g_clear_error(&err);
        g_free(open_path);
        g_task_return_boolean(task, TRUE);
        return FALSE;
    }
    td->session = pdf_session_new(open_path, doc);
    g_free(open_path);
    return TRUE;
}

/* Page i rendered max_w pixels wide (within a sane scale), or NULL */
static GdkTexture* render_page(PopplerDocument* doc, gint i, gint max_w) {
    PopplerPage* page = poppler_document_get_page(doc, i);
    if (page == NULL) {
        return NULL;
    }
    double pw = 0.0, ph = 0.0;
    poppler_page_get_size(page, &pw, &ph);

    double scale = (pw > 0.0) ? ((double)max_w / pw) : 1.0;
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
    g_object_unref(page);

    cairo_surface_flush(surface);
    gint stride = cairo_image_surface_get_stride(surface);
    /* The texture keeps the surface's pixels (no copy) */
    GBytes* bytes = g_bytes_new_with_free_func(cairo_image_surface_get_data(surface),
                                               (gsize)stride * (gsize)rh,
                                               (GDestroyNotify)cairo_surface_destroy, surface);
    GdkTexture* tex = gdk_memory_texture_new(rw, rh, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, (gsize)stride);
    g_bytes_unref(bytes);
    return tex;
}

/* One render thread: pages first, first + step... before end, into out
 * (indexed from base), with the session document of its slot (opened by the
 * thread itself when needed) */
typedef struct {
    PdfSession* session;
    guint slot;
    gint first;
    gint end;
    gint step;
    gint base;
    gint max_w;
    GCancellable* cancellable;
    GdkTexture** out;
} PdfRenderJob;

static gpointer render_job_run(gpointer data) {
    PdfRenderJob* job = data;
    PopplerDocument* doc = g_ptr_array_index(job->session->docs, job->slot);
    if (doc == NULL) {
        doc = pdf_open(job->session->path, job->cancellable, NULL);
        if (doc == NULL) {
            return NULL; /* its pages are rendered afterwards with the first document */
        }
        /* Only this thread touches its slot (the array is sized beforehand) */
        g_ptr_array_index(job->session->docs, job->slot) = doc;
    }
    for (gint i = job->first; i < job->end; i += job->step) {
        if (g_cancellable_is_cancelled(job->cancellable)) {
            break;
        }
        job->out[i - job->base] = render_page(doc, i, job->max_w);
    }
    return NULL;
}

/* Pages first..end - 1 into td->page_textures, on up to MAX_RENDER_THREADS
 * threads (this one included) */
static void render_pages(PdfTaskData* td, gint first, gint end, GCancellable* cancellable) {
    td->page_textures = g_ptr_array_new_with_free_func(g_object_unref);
    gint count = end - first;
    if (count <= 0) {
        return;
    }
    gint threads = MIN(MIN((gint)g_get_num_processors(), MAX_RENDER_THREADS), count);
    threads = MAX(threads, 1);
    if (td->session->docs->len < (guint)threads) {
        g_ptr_array_set_size(td->session->docs, threads);
    }
    GdkTexture** out = g_new0(GdkTexture*, count);
    PdfRenderJob* jobs = g_new0(PdfRenderJob, threads);
    GThread** started = g_new0(GThread*, threads);
    for (gint k = 0; k < threads; k++) {
        jobs[k] = (PdfRenderJob){td->session, (guint)k, first + k, end, threads, first, td->max_w, cancellable, out};
        if (k > 0) {
            started[k] = g_thread_try_new("pdf-render", render_job_run, &jobs[k], NULL);
        }
    }
    render_job_run(&jobs[0]);
    for (gint k = 1; k < threads; k++) {
        if (started[k] != NULL) {
            g_thread_join(started[k]);
        }
    }
    PopplerDocument* doc = g_ptr_array_index(td->session->docs, 0);
    for (gint i = 0; i < count; i++) {
        if (out[i] == NULL && !g_cancellable_is_cancelled(cancellable)) {
            out[i] = render_page(doc, first + i, td->max_w); /* a thread could not run */
        }
        if (out[i] != NULL) {
            g_ptr_array_add(td->page_textures, out[i]);
        }
    }
    g_free(started);
    g_free(jobs);
    g_free(out);
}

/* The text of the pages, PREVIEW_BYTES in all at most */
static void extract_text(PdfTaskData* td, PopplerDocument* doc, GCancellable* cancellable) {
    td->page_texts = g_ptr_array_new_with_free_func(g_free);
    gsize text_bytes = 0;
    for (gint i = 0; i < td->total_pages; i++) {
        if (g_cancellable_is_cancelled(cancellable)) {
            break;
        }
        if (text_bytes >= PREVIEW_BYTES) {
            td->text_cut = TRUE;
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
        if (raw_text == NULL) {
            raw_text = g_strdup("");
        }
        /* At most PREVIEW_BYTES in all, cut at a character boundary */
        gsize len = strlen(raw_text);
        if (text_bytes + len > PREVIEW_BYTES) {
            gsize keep = PREVIEW_BYTES - text_bytes;
            while (keep > 0 && ((guchar)raw_text[keep] & 0xC0) == 0x80) {
                keep--;
            }
            raw_text[keep] = '\0';
            len = keep;
            td->text_cut = TRUE;
        }
        text_bytes += len;
        g_ptr_array_add(td->page_texts, raw_text);
        if (td->text_cut) {
            break;
        }
    }
}

static void pdf_preview_thread_func(GTask* task, gpointer source_object,
                                    gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    PdfTaskData* td = (PdfTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    /* The first task opens the document, the next ones reuse it */
    if (td->session == NULL && !open_document(task, td, cancellable)) {
        return;
    }
    PopplerDocument* doc = g_ptr_array_index(td->session->docs, 0);

    if (td->first_page == 0) {
        td->total_pages = poppler_document_get_n_pages(doc);
        td->doc_title = poppler_document_get_title(doc);
        td->doc_author = poppler_document_get_author(doc);
        td->doc_ver = poppler_document_get_pdf_version_string(doc);
        /* The bookmarks wait for the next task, unless there is none */
        td->read_marks = td->render_raw || MIN(td->total_pages, MAX_RENDER_PAGES) <= td->page_count;
    }

    if (!td->render_raw) {
        gint render_end = MIN(MIN(td->total_pages, MAX_RENDER_PAGES), td->first_page + td->page_count);
        render_pages(td, td->first_page, render_end, cancellable);
    } else {
        extract_text(td, doc, cancellable);
    }

    if (td->read_marks && !g_cancellable_is_cancelled(cancellable)) {
        PopplerIndexIter* index = poppler_index_iter_new(doc);
        if (index != NULL) {
            td->marks = g_ptr_array_new_with_free_func(pdf_mark_free);
            read_outline(doc, index, 1, td->marks);
            poppler_index_iter_free(index);
        }
    }

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
    search_forget_viewer_match(app);
    outline_reset(app, td);

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
    const gchar* mode_hint = td->render_raw ? _("  [text mode, press m for rendered pages]")
                                            : _("  [rendered pages, press m for text]");
    gchar* type_text = td->converted
        ? g_strdup(td->is_dvi ? "DVI" : "PostScript")
        : g_strdup_printf("PDF %s", td->doc_ver ? td->doc_ver : "1.x");
    gchar* pages_text = (td->converted && td->total_pages >= MAX_RENDER_PAGES)
        ? g_strdup_printf(_("first %d pages"), MAX_RENDER_PAGES)
        : g_strdup_printf(ngettext("%d page", "%d pages", (gulong)td->total_pages), td->total_pages);
    gchar* meta_text;
    if (td->doc_author != NULL && td->doc_author[0] != '\0') {
        meta_text = g_strdup_printf(_("By %1$s  •  %2$s  •  %3$s  •  %4$s%5$s\n\n"),
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
        g_object_set_data(G_OBJECT(app->buffer), "minimap_paged", GINT_TO_POINTER(1));
        append_pdf_pages(app, td);
    } else {
        g_object_set_data(G_OBJECT(app->buffer), "minimap_paged", NULL);
    }
    if (td->render_raw && td->page_texts != NULL) {
        for (guint i = 0; i < td->page_texts->len; i++) {
            const gchar* text = (const gchar*)g_ptr_array_index(td->page_texts, i);
            outline_note_page(app->preview_cancellable, (gint)i, &iter);
            gchar* page_hdr = g_strdup_printf(_("--- Page %1$u of %2$d ---\n\n"),
                                              i + 1, td->total_pages);
            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, page_hdr, -1,
                                                     "pdf_text_header", NULL);
            g_free(page_hdr);

            if (text != NULL && text[0] != '\0') {
                gtk_text_buffer_insert(app->buffer, &iter, text, -1);
            } else {
                gtk_text_buffer_insert(app->buffer, &iter, _("(no text on this page)\n"), -1);
            }
            gtk_text_buffer_insert(app->buffer, &iter, "\n\n", -1);
        }
        if (td->text_cut) {
            gchar* notice = g_strdup_printf(_("[ Text shown up to page %1$u of %2$d (first %3$d KB) ]\n\n"),
                                            td->page_texts->len, td->total_pages, PREVIEW_BYTES / 1024);
            gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, notice, -1, "pdf_meta", NULL);
            g_free(notice);
        }
        outline_publish(app);
    }
}

gboolean pdf_preview_jump(AppData* app, gint direction) {
    if (preview_state_kind(app) != FILE_KIND_PDF || preview_state_raw(app)) {
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
    while (gtk_text_iter_forward_search(&from, PAGE_BADGE_PREFIX, GTK_TEXT_SEARCH_TEXT_ONLY, &match_start, &match_end,
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
    if (minimap_viewer_is_hidden(app)) {
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
    if (preview_state_kind(app) != FILE_KIND_PDF || preview_state_raw(app) || app->text_view == NULL) {
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
    td->render_raw = preview_state_raw(app);
    /* Zoomed pages are at most 4096 px wide (memory: up to MAX_RENDER_PAGES textures) */
    td->max_w = MIN((gint)(max_w * (preview_state_zoom(app) > 0 ? preview_state_zoom(app) : 1.0)), 4096);
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
        _("[ PDF preview disabled for %s:\n  install libpoppler-glib-dev then rebuild (make). ]"),
        name);
    gtk_text_buffer_set_text(app->buffer, msg, -1);
    g_free(msg);
}

#endif /* HAVE_POPPLER */
