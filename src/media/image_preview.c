/* Image preview: fitted / 1:1 / zoomed, EXIF orientation and photo details
 * (see image_preview.h); decoded by image_decode. */
#include "media/image_preview.h"
#include "preview/preview_state.h"
#include "search/search.h"
#include "core/i18n.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"
#include "theme/icons.h"

#include <math.h>

#include "media/animation.h"
#include "core/app_events.h"
#include "core/file_types.h"
#include "media/image_decode.h"
#include "theme/text_style.h"
#include "preview/preview_busy.h"

/* A preview waiting for its image */
typedef struct {
    AppData* app;
    gchar* name;
    GCancellable* cancellable; /* the preview's */
    ImageDecodeSize size;
} ImageRequest;

static void image_request_free(gpointer data) {
    ImageRequest* rq = data;
    g_free(rq->name);
    g_clear_object(&rq->cancellable);
    g_free(rq);
}

/* How the preview shows an image now (zoom and box: those of the view) */
static ImageDecodeSize preview_size(AppData* app, gboolean render_raw, gdouble zoom) {
    ImageDecodeSize size = {
        .render_raw = render_raw,
        .zoom = zoom > 0 ? zoom : 1.0,
        .box_w = gtk_widget_get_width(app->text_view),
        .box_h = gtk_widget_get_height(app->text_view),
    };
    return size;
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

static void on_image_preview_ready(ImageDecoded* d, gpointer user_data) {
    ImageRequest* rq = user_data;
    if (rq->cancellable != rq->app->preview_cancellable) {
        image_decoded_free(d);
        return;
    }
    AppData* app = rq->app;
    preview_busy_stop(app); /* the picture is ready */

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

    if (d->error_msg != NULL) {
        gtk_text_buffer_set_text(app->buffer, d->error_msg, -1);
        image_decoded_free(d);
        return;
    }

    ensure_image_tags(app->buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(app->buffer, &iter);

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n", -1, "img_center", NULL);

    text_insert_file_icon(app->buffer, &iter, rq->name, FALSE, "img_header");
    gchar* title = g_strdup_printf("%s\n", rq->name);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, title, -1, "img_header", NULL);
    g_free(title);

    gboolean resized = d->orig_w != d->target_w || d->orig_h != d->target_h;
    const gchar* animated = d->animation != NULL ? _("  •  animated") : "";
    gchar* mode_hint;
    if (rq->size.zoom != 1.0) {
        mode_hint = g_strdup_printf(_("  [%.0f %%, press 0 to fit]"), 100.0 * d->target_w / d->orig_w);
    } else if (d->is_fitted) {
        mode_hint = g_strdup(_("  [fitted, press m for 1:1, + / - to zoom]"));
    } else {
        mode_hint = g_strdup(rq->size.render_raw && resized ? _("  [1:1 scale, press m to fit]") : _("  [1:1 scale]"));
    }
    gchar* meta = g_strdup_printf("%s  •  %d × %d px  •  %s%s%s\n\n",
                                  d->fmt_upper, d->orig_w, d->orig_h, d->size_str, animated, mode_hint);
    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, meta, -1, "img_sub", NULL);
    g_free(meta);
    g_free(mode_hint);

    GdkPaintable* animation = NULL;
    if (d->animation != NULL) {
        animation = lookin_animation_new(d->animation); /* takes the frames */
        d->animation = NULL;
    }
    if (animation != NULL || d->texture != NULL) {
        gtk_text_buffer_insert_paintable(app->buffer, &iter,
                                         animation != NULL ? animation : GDK_PAINTABLE(d->texture));
        g_clear_object(&animation);

        GtkTextIter start_img = iter;
        gtk_text_iter_backward_char(&start_img);
        gtk_text_buffer_apply_tag_by_name(app->buffer, "img_center", &start_img, &iter);
    }

    gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, "\n\n", -1, "img_center", NULL);
    for (gint i = 0; d->details != NULL && d->details[i] != NULL; i++) {
        gchar* line = g_strdup_printf("%s\n", d->details[i]);
        gtk_text_buffer_insert_with_tags_by_name(app->buffer, &iter, line, -1, "img_details", NULL);
        g_free(line);
    }
    image_decoded_free(d);
}

void render_image_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();

    ImageRequest* rq = g_new0(ImageRequest, 1);
    rq->app = app;
    rq->name = g_strdup(name);
    rq->cancellable = g_object_ref(app->preview_cancellable);
    rq->size = preview_size(app, preview_state_raw(app), preview_state_zoom(app));
    image_decode_async(full, name, &rq->size, rq->cancellable, on_image_preview_ready, rq,
                       image_request_free);
}

void image_preview_prefetch(AppData* app, const gchar* const* paths) {
    /* Another file is shown fitted, at the zoom of 1 (show_selected_row()) */
    ImageDecodeSize size = preview_size(app, FALSE, 1.0);
    image_decode_prefetch(paths, &size);
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
    GdkTexture* texture = image_decode_whole(task_data);
    g_task_return_pointer(task, texture, texture != NULL ? g_object_unref : NULL);
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
