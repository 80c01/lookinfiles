/* Copy, cut, paste and duplicate Canvas blocks (see canvas_clip.h) */
#include "canvas_clip.h"
#include "canvas_doc.h"
#include "canvas_page_private.h"

#include <string.h>

#define CLIP_MIME "application/x-lookinfiles-canvas"
#define COPY_OFFSET 16 /* a duplicate, or blocks pasted away from the pointer */

/* Top-left corner of the blocks of doc */
static void doc_origin(const CanvasDoc* doc, gint* x, gint* y) {
    *x = G_MAXINT;
    *y = G_MAXINT;
    for (guint i = 0; i < doc->blocks->len; i++) {
        const CanvasBlock* b = g_ptr_array_index(doc->blocks, i);
        *x = MIN(*x, b->x);
        *y = MIN(*y, b->y);
    }
}

void canvas_clip_duplicate(CanvasPage* p) {
    if (p->dir == NULL) return;
    CanvasDoc* doc = canvas_page_selected_doc(p);
    canvas_page_add_copies(p, doc, COPY_OFFSET, COPY_OFFSET, p->dir);
    canvas_doc_free(doc);
}

gboolean canvas_clip_copy(CanvasPage* p, gboolean cut) {
    if (p->dir == NULL) return FALSE;
    CanvasDoc* doc = canvas_page_selected_doc(p);
    if (doc->blocks->len == 0) {
        canvas_doc_free(doc);
        return FALSE;
    }
    /* The note directory on the first line, then the blocks */
    gchar* xml = canvas_doc_to_xml(doc);
    gchar* data = g_strconcat(p->dir, "\n", xml, NULL);
    GBytes* bytes = g_bytes_new_take(data, strlen(data));
    gchar* md = canvas_doc_to_markdown(doc, NULL);
    GdkContentProvider* providers[] = {
        gdk_content_provider_new_for_bytes(CLIP_MIME, bytes),
        gdk_content_provider_new_typed(G_TYPE_STRING, md),
    };
    GdkContentProvider* provider = gdk_content_provider_new_union(providers, G_N_ELEMENTS(providers));
    gdk_clipboard_set_content(gtk_widget_get_clipboard(p->root), provider);
    g_object_unref(provider);
    g_bytes_unref(bytes);
    g_free(md);
    g_free(xml);
    canvas_doc_free(doc);
    if (cut) canvas_page_remove_selected(p);
    return TRUE;
}

/* Blocks being read from the clipboard; the page root is kept alive
 * meanwhile, and the blocks dropped if another note was opened */
typedef struct {
    GtkWidget* root;
    guint generation;
    GMemoryOutputStream* out;
} PasteJob;

static void paste_job_free(PasteJob* job) {
    g_object_unref(job->root);
    if (job->out != NULL) g_object_unref(job->out);
    g_free(job);
}

static void paste_data(CanvasPage* p, const gchar* data, gsize len) {
    const gchar* nl = memchr(data, '\n', len);
    if (nl == NULL) return;
    gchar* from_dir = g_strndup(data, nl - data);
    g_autoptr(GError) error = NULL;
    CanvasDoc* doc = canvas_doc_parse(nl + 1, (gssize)(len - (gsize)(nl + 1 - data)), &error);
    if (doc == NULL) {
        g_warning("canvas: cannot paste the blocks: %s", error != NULL ? error->message : "");
        g_free(from_dir);
        return;
    }
    gint x0, y0;
    doc_origin(doc, &x0, &y0);
    gint dx = COPY_OFFSET, dy = COPY_OFFSET;
    if (p->pointer_in) {
        dx = (gint)p->pointer_x - x0;
        dy = (gint)p->pointer_y - y0;
    }
    canvas_page_add_copies(p, doc, dx, dy, from_dir);
    canvas_doc_free(doc);
    g_free(from_dir);
}

static void on_splice_done(GObject* source, GAsyncResult* result, gpointer data) {
    PasteJob* job = data;
    g_autoptr(GError) error = NULL;
    if (g_output_stream_splice_finish(G_OUTPUT_STREAM(source), result, &error) < 0) {
        g_warning("canvas: cannot read the clipboard: %s", error->message);
        paste_job_free(job);
        return;
    }
    CanvasPage* p = g_object_get_data(G_OBJECT(job->root), CANVAS_PAGE_KEY);
    if (p != NULL && !p->closed && p->dir != NULL && p->generation == job->generation) {
        paste_data(p, g_memory_output_stream_get_data(job->out), g_memory_output_stream_get_data_size(job->out));
    }
    paste_job_free(job);
}

static void on_clipboard_read(GObject* source, GAsyncResult* result, gpointer data) {
    PasteJob* job = data;
    g_autoptr(GError) error = NULL;
    GInputStream* in = gdk_clipboard_read_finish(GDK_CLIPBOARD(source), result, NULL, &error);
    if (in == NULL) {
        g_warning("canvas: cannot read the clipboard: %s", error != NULL ? error->message : "");
        paste_job_free(job);
        return;
    }
    job->out = G_MEMORY_OUTPUT_STREAM(g_memory_output_stream_new_resizable());
    g_output_stream_splice_async(G_OUTPUT_STREAM(job->out), in,
                                 G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                 G_PRIORITY_DEFAULT, NULL, on_splice_done, job);
    g_object_unref(in);
}

gboolean canvas_clip_paste(CanvasPage* p) {
    if (p->dir == NULL) return FALSE;
    GdkClipboard* clip = gtk_widget_get_clipboard(p->root);
    if (!gdk_content_formats_contain_mime_type(gdk_clipboard_get_formats(clip), CLIP_MIME)) return FALSE;
    PasteJob* job = g_new0(PasteJob, 1);
    job->root = g_object_ref(p->root);
    job->generation = p->generation;
    static const gchar* MIMES[] = {CLIP_MIME, NULL};
    gdk_clipboard_read_async(clip, MIMES, G_PRIORITY_DEFAULT, NULL, on_clipboard_read, job);
    return TRUE;
}
