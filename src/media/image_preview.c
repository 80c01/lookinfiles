/* Image preview: fitted / 1:1 / zoomed, EXIF orientation and photo details
 * (see image_preview.h). */
#include "media/image_preview.h"
#include "preview/preview_state.h"
#include "search/search.h"
#include "core/i18n.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"
#include "theme/icons.h"

#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "media/animation.h"
#include "core/app_events.h"
#include "media/exif.h"
#include "core/file_types.h"
#include "media/image_utils.h"
#include "theme/text_style.h"

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
    AnimationFrames* animation; /* animated GIF: played instead of texture */
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
    animation_frames_free(td->animation);
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

/* Largest RAW file read for its embedded preview */
#define MAX_RAW_FILE (256u * 1024 * 1024)

/* First bytes of a file */
static gsize read_head(const gchar* path, guchar* buf, gsize len) {
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    gsize n = fread(buf, 1, len, fp);
    fclose(fp);
    return n;
}

/* Embedded JPEG preview of a camera RAW file, and its EXIF details */
static GdkPixbuf* raw_preview(const gchar* path, ExifInfo** exif) {
    gchar* data = NULL;
    gsize len = 0;
    GStatBuf st;
    if (g_stat(path, &st) != 0 || (guint64)st.st_size > MAX_RAW_FILE ||
        !g_file_get_contents(path, &data, &len, NULL)) {
        return NULL;
    }
    *exif = exif_parse((const guchar*)data, len);
    gsize off = 0, n = 0;
    GdkPixbuf* pix = NULL;
    if (exif_find_preview((const guchar*)data, len, &off, &n)) {
        pix = pixbuf_from_bytes((const guchar*)data + off, n, 0);
    }
    g_free(data);
    return pix;
}

/* Why an image cannot be decoded: the loader to install for its format */
static gchar* missing_loader_message(const gchar* name, const gchar* path) {
    guchar head[32];
    gsize n = read_head(path, head, sizeof(head));
    const gchar* format = NULL;
    const gchar* package = NULL;
    if (n >= 12 && memcmp(head + 4, "ftyp", 4) == 0) {
        if (memcmp(head + 8, "avif", 4) == 0 || memcmp(head + 8, "avis", 4) == 0) {
            format = "AVIF";
            package = "libavif-gdk-pixbuf";
        } else {
            format = "HEIC";
            package = "heif-gdk-pixbuf";
        }
    } else if ((n >= 2 && head[0] == 0xFF && head[1] == 0x0A) || (n >= 12 && memcmp(head + 4, "JXL ", 4) == 0)) {
        format = "JPEG XL";
        package = "libjxl-gdk-pixbuf";
    } else if (n >= 12 && memcmp(head, "RIFF", 4) == 0 && memcmp(head + 8, "WEBP", 4) == 0) {
        format = "WebP";
        package = "webp-pixbuf-loader";
    }
    if (package != NULL) {
        return g_strdup_printf(_("[ %1$s: no image loader for %2$s is installed (package %3$s) ]"), name, format, package);
    }
    return g_strdup_printf(_("[ Cannot decode image: %s ]"), name);
}

static void image_preview_thread_func(GTask* task, gpointer source_object,
                                      gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    ImagePreviewTaskData* td = (ImagePreviewTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    /* Camera RAW: the JPEG preview the camera embedded (no RAW decoding) */
    ExifInfo* exif = NULL;
    GdkPixbuf* embedded = NULL;
    gint file_w = 0, file_h = 0;
    guchar head[16];
    gsize head_len = read_head(td->full, head, sizeof(head));
    if (is_raw_photo_file(td->name) || (head_len >= 15 && memcmp(head, "FUJIFILMCCD-RAW", 15) == 0)) {
        embedded = raw_preview(td->full, &exif);
    }
    if (embedded != NULL) {
        file_w = gdk_pixbuf_get_width(embedded);
        file_h = gdk_pixbuf_get_height(embedded);
        gchar* ext = g_ascii_strup(file_extension(td->name), -1);
        td->fmt_upper = g_strdup_printf(_("%s RAW (embedded preview)"), ext);
        g_free(ext);
    } else {
        exif_info_free(exif);
        exif = NULL;
        GdkPixbufFormat* fmt = gdk_pixbuf_get_file_info(td->full, &file_w, &file_h);
        if (fmt == NULL || file_w <= 0 || file_h <= 0) {
            td->error_msg = missing_loader_message(td->name, td->full);
            g_task_return_boolean(task, TRUE);
            return;
        }
        gchar* fmt_name = gdk_pixbuf_format_get_name(fmt);
        td->fmt_upper = g_ascii_strup(fmt_name, -1);
        g_free(fmt_name);
        exif = exif_read_file(td->full);
    }

    if (g_task_return_error_if_cancelled(task)) {
        exif_info_free(exif);
        g_clear_object(&embedded);
        return;
    }

    GStatBuf st;
    gint64 fsize = (g_stat(td->full, &st) == 0) ? (gint64)st.st_size : 0;
    td->size_str = g_format_size((guint64)fsize);

    /* Photo details; orientations 5 to 8 swap width and height */
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
    gint load_w = swap ? td->target_h : td->target_w;
    gint load_h = swap ? td->target_w : td->target_h;

    /* Animated GIF: frames decoded and scaled here, played by the view
     * (other animated formats show their first frame) */
    if (embedded == NULL && head_len >= 6 && memcmp(head, "GIF8", 4) == 0) {
        td->animation = animation_frames_load_gif(td->full, td->target_w, td->target_h, cancellable);
        if (g_task_return_error_if_cancelled(task)) {
            return;
        }
        if (td->animation != NULL) {
            g_task_return_boolean(task, TRUE);
            return;
        }
    }

    GdkPixbuf* pix;
    if (embedded != NULL) {
        pix = gdk_pixbuf_scale_simple(embedded, load_w, load_h, GDK_INTERP_BILINEAR);
        g_object_unref(embedded);
    } else {
        GError* err = NULL;
        pix = gdk_pixbuf_new_from_file_at_scale(td->full, load_w, load_h, TRUE, &err);
        if (pix == NULL) {
            td->error_msg = g_strdup_printf(_("[ Error loading image: %1$s (%2$s) ]"),
                                            td->name, err ? err->message : _("unknown error"));
            if (err != NULL) {
                g_error_free(err);
            }
            g_task_return_boolean(task, TRUE);
            return;
        }
    }
    if (pix == NULL) {
        td->error_msg = g_strdup_printf(_("[ Cannot decode image: %s ]"), td->name);
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
        text_tag_create(buffer, "img_header",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "weight", PANGO_WEIGHT_BOLD,
                                   "scale", FONT_SCALE_TITLE,
                                   "foreground", "@fg_strong",
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_sub")) {
        text_tag_create(buffer, "img_sub",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "@fg_subtle",
                                   "scale", FONT_SCALE_SMALL,
                                   NULL);
    }
    if (!gtk_text_tag_table_lookup(tags, "img_details")) {
        text_tag_create(buffer, "img_details",
                                   "justification", GTK_JUSTIFY_CENTER,
                                   "foreground", "@fg_muted",
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
    search_forget_viewer_match(app);

    if (td->error_msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, td->error_msg, -1);
        return;
    }

    ensure_image_tags(app->buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "img_center", NULL);

    text_insert_file_icon(app->buffer, &iter, td->name, FALSE, "img_header");
    gchar* title = g_strdup_printf("%s\n", td->name);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, title, -1, "img_header", NULL);
    g_free(title);

    gboolean resized = td->orig_w != td->target_w || td->orig_h != td->target_h;
    const gchar* animated = td->animation != NULL ? _("  •  animated") : "";
    gchar* mode_hint;
    if (td->zoom != 1.0) {
        mode_hint = g_strdup_printf(_("  [%.0f %%, press 0 to fit]"), 100.0 * td->target_w / td->orig_w);
    } else if (td->is_fitted) {
        mode_hint = g_strdup(_("  [fitted, press m for 1:1, + / - to zoom]"));
    } else {
        mode_hint = g_strdup(td->render_raw && resized ? _("  [1:1 scale, press m to fit]") : _("  [1:1 scale]"));
    }
    gchar* meta = g_strdup_printf("%s  •  %d × %d px  •  %s%s%s\n\n",
                                  td->fmt_upper, td->orig_w, td->orig_h, td->size_str, animated, mode_hint);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta, -1, "img_sub", NULL);
    g_free(meta);
    g_free(mode_hint);

    GdkPaintable* animation = NULL;
    if (td->animation != NULL) {
        animation = lookin_animation_new(td->animation); /* takes the frames */
        td->animation = NULL;
    }
    if (animation != NULL || td->texture != NULL) {
        gtk_text_buffer_insert_paintable(app->buffer, &iter,
                                         animation != NULL ? animation : GDK_PAINTABLE(td->texture));
        g_clear_object(&animation);

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
    td->render_raw = preview_state_raw(app);
    td->zoom = preview_state_zoom(app) > 0 ? preview_state_zoom(app) : 1.0;
    td->alloc_w = gtk_widget_get_width(app->text_view);
    td->alloc_h = gtk_widget_get_height(app->text_view);

    GTask* task = g_task_new(NULL, app->preview_cancellable, on_image_preview_ready, NULL);
    g_task_set_task_data(task, td, image_preview_task_data_free);
    g_task_run_in_thread(task, image_preview_thread_func);
    g_object_unref(task);
}

/* --- Zoom --- */

static gboolean zoomable(AppData* app) {
    return preview_state_path(app) != NULL &&
           (preview_state_kind(app) == FILE_KIND_IMAGE || preview_state_kind(app) == FILE_KIND_PDF);
}

static void set_zoom(AppData* app, gdouble zoom) {
    zoom = CLAMP(zoom, PREVIEW_ZOOM_MIN, PREVIEW_ZOOM_MAX);
    if (fabs(zoom - 1.0) < 0.01) {
        zoom = 1.0;
    }
    if (zoom == preview_state_zoom(app)) {
        return;
    }
    preview_state_set_zoom(app, zoom);
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
        set_zoom(app, preview_state_zoom(app) * 1.25);
        return TRUE;
    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
        set_zoom(app, preview_state_zoom(app) / 1.25);
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
    set_zoom(app, dy < 0 ? preview_state_zoom(app) * 1.25 : preview_state_zoom(app) / 1.25);
    return TRUE;
}

/* --- Copy Image (preview context menu) --- */

/* Worker: the whole image, upright, at its own size (capped) */
static void copy_image_thread_func(GTask* task, gpointer source_object,
                                   gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    const gchar* path = task_data;
    gchar* name = g_path_get_basename(path);
    ExifInfo* exif = NULL;
    GdkPixbuf* pix = NULL;
    if (is_raw_photo_file(name)) {
        pix = raw_preview(path, &exif);
    }
    if (pix == NULL) {
        exif_info_free(exif);
        exif = exif_read_file(path);
        gint w = 0, h = 0;
        if (gdk_pixbuf_get_file_info(path, &w, &h) != NULL && w > 0 && h > 0) {
            gdouble pixels = (gdouble)w * h;
            if (pixels > MAX_DECODED_PIXELS) {
                gdouble scale = sqrt(MAX_DECODED_PIXELS / pixels);
                pix = gdk_pixbuf_new_from_file_at_scale(path, MAX(1, (gint)(w * scale)),
                                                        MAX(1, (gint)(h * scale)), TRUE, NULL);
            } else {
                pix = gdk_pixbuf_new_from_file(path, NULL);
            }
        }
    }
    g_free(name);
    if (pix == NULL) {
        exif_info_free(exif);
        g_task_return_pointer(task, NULL, NULL);
        return;
    }
    pix = apply_orientation(pix, exif != NULL ? exif->orientation : 0);
    exif_info_free(exif);
    GdkTexture* texture = texture_new_for_pixbuf(pix);
    g_object_unref(pix);
    g_task_return_pointer(task, texture, g_object_unref);
}

static void on_copy_image_ready(GObject* source_object, GAsyncResult* res, gpointer user_data) {
    (void)source_object;
    AppData* app = user_data;
    GdkTexture* texture = g_task_propagate_pointer(G_TASK(res), NULL);
    gchar* name = g_path_get_basename(g_task_get_task_data(G_TASK(res)));
    gchar* msg;
    if (texture != NULL) {
        gdk_clipboard_set_texture(gtk_widget_get_clipboard(app->text_view), texture);
        g_object_unref(texture);
        msg = g_strdup_printf(_("Copied image “%s”"), name);
    } else {
        msg = g_strdup_printf(_("Cannot copy image “%s”"), name);
    }
    app_notice(app, msg, FALSE);
    g_free(msg);
    g_free(name);
}

static void on_copy_image(GSimpleAction* action, GVariant* param, gpointer data) {
    (void)action;
    (void)param;
    AppData* app = data;
    const gchar* path = preview_state_path(app);
    if (path == NULL || preview_state_kind(app) != FILE_KIND_IMAGE) {
        return;
    }
    GTask* task = g_task_new(NULL, NULL, on_copy_image_ready, app);
    g_task_set_task_data(task, g_strdup(path), g_free);
    g_task_run_in_thread(task, copy_image_thread_func);
    g_object_unref(task);
}

/* Right click: the menu item shows only while an image is previewed */
static void on_preview_secondary_press(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y,
                                       gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    GSimpleAction* action = data;
    AppData* app = g_object_get_data(G_OBJECT(action), "app");
    g_simple_action_set_enabled(action, preview_state_path(app) != NULL &&
                                            preview_state_kind(app) == FILE_KIND_IMAGE);
}

void image_preview_attach_menu(AppData* app, GtkWidget* text_view) {
    GSimpleActionGroup* group = g_simple_action_group_new();
    GSimpleAction* action = g_simple_action_new("copy-image", NULL);
    g_object_set_data(G_OBJECT(action), "app", app);
    g_simple_action_set_enabled(action, FALSE);
    g_signal_connect(action, "activate", G_CALLBACK(on_copy_image), app);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));
    gtk_widget_insert_action_group(text_view, "image", G_ACTION_GROUP(group));
    g_object_unref(group);

    GMenu* menu = g_menu_new();
    GMenuItem* item = g_menu_item_new(_("Copy Image"), "image.copy-image");
    g_menu_item_set_attribute(item, "hidden-when", "s", "action-disabled");
    g_menu_append_item(menu, item);
    g_object_unref(item);
    gtk_text_view_set_extra_menu(GTK_TEXT_VIEW(text_view), G_MENU_MODEL(menu));
    g_object_unref(menu);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_preview_secondary_press), action);
    gtk_widget_add_controller(text_view, GTK_EVENT_CONTROLLER(click));
    g_object_unref(action);
}

void image_preview_attach_zoom(AppData* app, GtkWidget* text_view) {
    GtkEventController* scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
    gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_preview_scroll), app);
    gtk_widget_add_controller(text_view, scroll);
}
