/* Image preview: fitted / 1:1 / zoomed, EXIF orientation and photo details
 * (see image_preview.h). */
#include "image_preview.h"

#include <glib/gstdio.h>
#include <math.h>
#include <sys/stat.h>

#include "app_events.h"
#include "exif.h"
#include "file_types.h"
#include "image_utils.h"
#include "text_style.h"

/* Largest decoded image, in pixels (zooming in on a large photo) */
#define MAX_DECODED_PIXELS (48u * 1024 * 1024)

typedef struct {
    AppData* app;
    gchar* name;
    gchar* full;
    gboolean render_raw;
    gdouble zoom;
    gint alloc_w;
    gint alloc_h;

    /* Results populated by worker thread */
    gchar* error_msg;
    gchar* fmt_upper;
    gchar* size_str;
    gint orig_w;           /* upright size (EXIF orientation applied) */
    gint orig_h;
    gint target_w;
    gint target_h;
    gboolean is_fitted;
    gchar** details;       /* photo details lines (EXIF), or NULL */
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
    g_strfreev(td->details);
    if (td->texture != NULL) {
        g_object_unref(td->texture);
    }
    g_free(td);
}

/* Turn a decoded image upright from its EXIF orientation (2..8) */
static GdkPixbuf* apply_orientation(GdkPixbuf* pix, gint orientation) {
    GdkPixbuf* a = NULL;
    GdkPixbuf* b = NULL;
    switch (orientation) {
    case 2: a = gdk_pixbuf_flip(pix, TRUE); break;
    case 3: a = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_UPSIDEDOWN); break;
    case 4: a = gdk_pixbuf_flip(pix, FALSE); break;
    case 5:
        b = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_CLOCKWISE);
        a = b != NULL ? gdk_pixbuf_flip(b, TRUE) : NULL;
        break;
    case 6: a = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_CLOCKWISE); break;
    case 7:
        b = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_CLOCKWISE);
        a = b != NULL ? gdk_pixbuf_flip(b, FALSE) : NULL;
        break;
    case 8: a = gdk_pixbuf_rotate_simple(pix, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE); break;
    default: break;
    }
    g_clear_object(&b);
    if (a == NULL) {
        return pix;
    }
    g_object_unref(pix);
    return a;
}

static void image_preview_thread_func(GTask* task, gpointer source_object,
                                      gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    gint file_w = 0, file_h = 0;
    GdkPixbufFormat* fmt = gdk_pixbuf_get_file_info(td->full, &file_w, &file_h);
    if (fmt == NULL || file_w <= 0 || file_h <= 0) {
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

    /* Photo details; orientations 5 to 8 swap width and height */
    ExifInfo* exif = exif_read_file(td->full);
    gint orientation = exif != NULL ? exif->orientation : 0;
    if (exif != NULL) {
        td->details = exif_summary_lines(exif);
        if (td->details[0] == NULL) {
            g_clear_pointer(&td->details, g_strfreev);
        }
        exif_info_free(exif);
    }
    gboolean swap = orientation >= 5 && orientation <= 8;
    td->orig_w = swap ? file_h : file_w;
    td->orig_h = swap ? file_w : file_h;

    gdouble scale = 1.0;
    if (!td->render_raw) {
        gint max_w = (td->alloc_w > 100) ? (td->alloc_w - 40) : 700;
        gint max_h = (td->alloc_h > 140) ? (td->alloc_h - 120) : 500;
        if (td->orig_w > max_w || td->orig_h > max_h) {
            scale = MIN((gdouble)max_w / td->orig_w, (gdouble)max_h / td->orig_h);
            td->is_fitted = TRUE;
        }
    }
    scale *= td->zoom;
    gdouble pixels = (gdouble)td->orig_w * td->orig_h * scale * scale;
    if (pixels > MAX_DECODED_PIXELS) {
        scale *= sqrt(MAX_DECODED_PIXELS / pixels);
    }
    td->target_w = MAX(1, (gint)(td->orig_w * scale + 0.5));
    td->target_h = MAX(1, (gint)(td->orig_h * scale + 0.5));

    GError* err = NULL;
    GdkPixbuf* pix = gdk_pixbuf_new_from_file_at_scale(
        td->full, swap ? td->target_h : td->target_w, swap ? td->target_w : td->target_h, TRUE, &err);
    if (pix == NULL) {
        td->error_msg = g_strdup_printf("[ Error loading image: %s (%s) ]",
                                        td->name, err ? err->message : "unknown error");
        if (err != NULL) {
            g_error_free(err);
        }
        g_task_return_boolean(task, TRUE);
        return;
    }
    pix = apply_orientation(pix, orientation);

    if (g_task_return_error_if_cancelled(task)) {
        g_object_unref(pix);
        return;
    }

    td->texture = texture_new_for_pixbuf(pix);
    g_object_unref(pix);
    g_task_return_boolean(task, TRUE);
}

static void ensure_image_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(tags, "img_center")) {
        gtk_text_buffer_create_tag(buffer, "img_center",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_header")) {
        gtk_text_buffer_create_tag(buffer, "img_header",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "scale", 1.15,
                                   "foreground", "#4fc1ff",
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_sub")) {
        gtk_text_buffer_create_tag(buffer, "img_sub",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "#888888",
                                   "scale", 0.9,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_details")) {
        gtk_text_buffer_create_tag(buffer, "img_details",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "#b0b0b0",
                                   "pixels-below-lines", 2,
                                   NULL);
    }
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

    ensure_image_tags(app->buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "img_center", NULL);

    gchar* title = g_strdup_printf("🖼️  %s\n", td->name);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, title, -1, "img_header", NULL);
    g_free(title);

    gboolean resized = td->orig_w != td->target_w || td->orig_h != td->target_h;
    gchar* mode_hint;
    if (td->zoom != 1.0) {
        mode_hint = g_strdup_printf("  [%.0f %%, press 0 to fit]", 100.0 * td->target_w / td->orig_w);
    } else if (td->is_fitted) {
        mode_hint = g_strdup("  [fitted, press m for 1:1, + / - to zoom]");
    } else {
        mode_hint = g_strdup(td->render_raw && resized ? "  [1:1 scale, press m to fit]" : "  [1:1 scale]");
    }
    gchar* meta = g_strdup_printf("%s  •  %d × %d px  •  %s%s\n\n",
                                  td->fmt_upper, td->orig_w, td->orig_h, td->size_str, mode_hint);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta, -1, "img_sub", NULL);
    g_free(meta);
    g_free(mode_hint);

    if (td->texture != NULL) {
        gtk_text_buffer_insert_paintable(app->buffer, &iter, GDK_PAINTABLE(td->texture));

        GtkTextIter start_img = iter;
        gtk_text_iter_backward_char(&start_img);
        gtk_text_buffer_apply_tag_by_name(app->buffer, "img_center", &start_img, &iter);
    }

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "img_center", NULL);
    for (gint i = 0; td->details != NULL && td->details[i] != NULL; i++) {
        gchar* line = g_strdup_printf("%s\n", td->details[i]);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, line, -1, "img_details", NULL);
        g_free(line);
    }
}

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
    td->zoom = app->preview_zoom > 0 ? app->preview_zoom : 1.0;
    td->alloc_w = gtk_widget_get_width(app->text_view);
    td->alloc_h = gtk_widget_get_height(app->text_view);

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_image_preview_ready, NULL);
    g_task_set_task_data(task, td, image_preview_task_data_free);
    g_task_run_in_thread(task, image_preview_thread_func);
    g_object_unref(task);
}

/* --- Zoom --- */

static gboolean zoomable(AppData* app) {
    return app->viewed_path != NULL &&
           (app->viewed_kind == FILE_KIND_IMAGE || app->viewed_kind == FILE_KIND_PDF);
}

static void set_zoom(AppData* app, gdouble zoom) {
    zoom = CLAMP(zoom, PREVIEW_ZOOM_MIN, PREVIEW_ZOOM_MAX);
    if (fabs(zoom - 1.0) < 0.01) {
        zoom = 1.0;
    }
    if (zoom == app->preview_zoom) {
        return;
    }
    app->preview_zoom = zoom;
    app_emit(app, APP_EVENT_PREVIEW_STALE);
}

gboolean image_preview_zoom_key(AppData* app, guint keyval) {
    if (!zoomable(app)) {
        return FALSE;
    }
    switch (keyval) {
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_KP_Add:
        set_zoom(app, app->preview_zoom * 1.25);
        return TRUE;
    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
        set_zoom(app, app->preview_zoom / 1.25);
        return TRUE;
    case GDK_KEY_0:
    case GDK_KEY_KP_0:
        set_zoom(app, 1.0);
        return TRUE;
    default:
        return FALSE;
    }
}

static gboolean on_preview_scroll(GtkEventControllerScroll* controller, gdouble dx, gdouble dy,
                                  gpointer data) {
    (void)dx;
    AppData* app = data;
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
    if (!(state & GDK_CONTROL_MASK) || !zoomable(app) || dy == 0) {
        return FALSE;
    }
    set_zoom(app, dy < 0 ? app->preview_zoom * 1.25 : app->preview_zoom / 1.25);
    return TRUE;
}

void image_preview_attach_zoom(AppData* app, GtkWidget* text_view) {
    GtkEventController* scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
    gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_preview_scroll), app);
    gtk_widget_add_controller(text_view, scroll);
}
