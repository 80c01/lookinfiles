/* Image helpers (see image_utils.h) */
#include "image_utils.h"

#include <string.h>

GdkTexture* texture_new_for_pixbuf(GdkPixbuf* pixbuf) {
    g_return_val_if_fail(GDK_IS_PIXBUF(pixbuf), NULL);
    /* GdkPixbuf stores 8-bit RGB or RGBA (not premultiplied) rows */
    GdkMemoryFormat format = gdk_pixbuf_get_has_alpha(pixbuf) ? GDK_MEMORY_R8G8B8A8
                                                              : GDK_MEMORY_R8G8B8;
    GBytes* bytes = gdk_pixbuf_read_pixel_bytes(pixbuf);
    GdkTexture* texture = gdk_memory_texture_new(gdk_pixbuf_get_width(pixbuf),
                                                 gdk_pixbuf_get_height(pixbuf), format, bytes,
                                                 (gsize)gdk_pixbuf_get_rowstride(pixbuf));
    g_bytes_unref(bytes);
    return texture;
}

GdkPixbuf* pixbuf_from_bytes(const guchar* data, gsize len, gint max_width) {
    if (data == NULL || len == 0) {
        return NULL;
    }
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    GdkPixbuf* pix = NULL;
    if (gdk_pixbuf_loader_write(loader, data, len, NULL) && gdk_pixbuf_loader_close(loader, NULL)) {
        GdkPixbuf* orig = gdk_pixbuf_loader_get_pixbuf(loader);
        if (orig != NULL) {
            gint w = gdk_pixbuf_get_width(orig);
            gint h = gdk_pixbuf_get_height(orig);
            if (max_width > 0 && w > max_width) {
                gint new_h = MAX(1, (gint)((gdouble)h * max_width / w));
                pix = gdk_pixbuf_scale_simple(orig, max_width, new_h, GDK_INTERP_BILINEAR);
            } else {
                pix = g_object_ref(orig);
            }
        }
    } else {
        gdk_pixbuf_loader_close(loader, NULL);
    }
    g_object_unref(loader);
    return pix;
}

GdkPixbuf* pixbuf_from_data_uri(const gchar* uri, gint max_width) {
    if (uri == NULL || !g_str_has_prefix(uri, "data:image/")) {
        return NULL;
    }
    const gchar* comma = strchr(uri, ',');
    if (comma == NULL || g_strstr_len(uri, comma - uri, ";base64") == NULL) {
        return NULL;
    }
    gsize len = 0;
    guchar* decoded = g_base64_decode(comma + 1, &len);
    GdkPixbuf* pix = pixbuf_from_bytes(decoded, len, max_width);
    g_free(decoded);
    return pix;
}

GdkPixbuf* pixbuf_from_file(const gchar* path, gint max_width) {
    gint w = 0;
    gint h = 0;
    if (path == NULL || gdk_pixbuf_get_file_info(path, &w, &h) == NULL || w <= 0) {
        return NULL;
    }
    if (max_width > 0 && w > max_width) {
        return gdk_pixbuf_new_from_file_at_scale(path, max_width, -1, TRUE, NULL);
    }
    return gdk_pixbuf_new_from_file(path, NULL);
}

/* Paintable showing a texture at its natural size, scaled down to the
 * width of a text view (CSS "max-width: 100%") */
struct _LookinFitImage {
    GObject parent_instance;
    GdkTexture* texture;
    GWeakRef view;
    gint width;       /* drawn width, updated outside of layout passes */
    guint resize_id;  /* idle source updating width */
};

static void fit_image_paintable_init(GdkPaintableInterface* iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(LookinFitImage, lookin_fit_image, G_TYPE_OBJECT,
                              G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, fit_image_paintable_init))

/* Width that fits the view now */
static gint fit_image_fit_width(LookinFitImage* self) {
    gint natural = gdk_texture_get_width(self->texture);
    GtkTextView* view = g_weak_ref_get(&self->view);
    if (view == NULL) {
        return natural;
    }
    /* Room for the text, less a pixel of slack so the line does not wrap
     * the image after itself */
    gint avail = gtk_widget_get_width(GTK_WIDGET(view)) - gtk_text_view_get_left_margin(view) -
                 gtk_text_view_get_right_margin(view) - 2;
    g_object_unref(view);
    return avail > 0 ? MIN(natural, avail) : natural;
}

static gint fit_image_get_intrinsic_width(GdkPaintable* paintable) {
    return LOOKIN_FIT_IMAGE(paintable)->width;
}

static gint fit_image_get_intrinsic_height(GdkPaintable* paintable) {
    LookinFitImage* self = LOOKIN_FIT_IMAGE(paintable);
    gint w = gdk_texture_get_width(self->texture);
    gint h = gdk_texture_get_height(self->texture);
    return MAX(1, (gint)((gdouble)h * self->width / MAX(1, w) + 0.5));
}

static gdouble fit_image_get_intrinsic_aspect_ratio(GdkPaintable* paintable) {
    LookinFitImage* self = LOOKIN_FIT_IMAGE(paintable);
    return (gdouble)gdk_texture_get_width(self->texture) /
           MAX(1, gdk_texture_get_height(self->texture));
}

static void fit_image_snapshot(GdkPaintable* paintable, GdkSnapshot* snapshot, gdouble width,
                               gdouble height) {
    LookinFitImage* self = LOOKIN_FIT_IMAGE(paintable);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, (float)width, (float)height);
    /* Trilinear: smooth when a large screenshot is shrunk */
    gtk_snapshot_append_scaled_texture(GTK_SNAPSHOT(snapshot), self->texture,
                                       GSK_SCALING_FILTER_TRILINEAR, &bounds);
}

static GdkPaintableFlags fit_image_get_flags(GdkPaintable* paintable) {
    (void)paintable;
    return GDK_PAINTABLE_STATIC_CONTENTS;
}

static void fit_image_paintable_init(GdkPaintableInterface* iface) {
    iface->get_intrinsic_width = fit_image_get_intrinsic_width;
    iface->get_intrinsic_height = fit_image_get_intrinsic_height;
    iface->get_intrinsic_aspect_ratio = fit_image_get_intrinsic_aspect_ratio;
    iface->snapshot = fit_image_snapshot;
    iface->get_flags = fit_image_get_flags;
}

static void fit_image_finalize(GObject* object) {
    LookinFitImage* self = LOOKIN_FIT_IMAGE(object);
    g_clear_handle_id(&self->resize_id, g_source_remove);
    g_clear_object(&self->texture);
    g_weak_ref_clear(&self->view);
    G_OBJECT_CLASS(lookin_fit_image_parent_class)->finalize(object);
}

static void lookin_fit_image_class_init(LookinFitImageClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = fit_image_finalize;
}

static void lookin_fit_image_init(LookinFitImage* self) {
    g_weak_ref_init(&self->view, NULL);
}

static gboolean fit_image_resize(gpointer user_data) {
    LookinFitImage* self = LOOKIN_FIT_IMAGE(user_data);
    self->resize_id = 0;
    gint width = fit_image_fit_width(self);
    if (width != self->width) {
        self->width = width;
        gdk_paintable_invalidate_size(GDK_PAINTABLE(self));
    }
    return G_SOURCE_REMOVE;
}

/* The text view was resized (its horizontal page size is its width). This
 * runs inside its size allocation: resizing the image there would
 * invalidate the layout being validated (GTK aborts), so wait for idle. */
static void on_view_page_size(GObject* adjustment, GParamSpec* pspec, gpointer user_data) {
    (void)adjustment;
    (void)pspec;
    LookinFitImage* self = LOOKIN_FIT_IMAGE(user_data);
    if (self->resize_id == 0) {
        self->resize_id = g_idle_add(fit_image_resize, self);
    }
}

GdkPaintable* fit_image_new(GdkTexture* texture, GtkTextView* view) {
    g_return_val_if_fail(GDK_IS_TEXTURE(texture), NULL);
    LookinFitImage* self = g_object_new(LOOKIN_TYPE_FIT_IMAGE, NULL);
    self->texture = g_object_ref(texture);
    if (view != NULL) {
        g_weak_ref_set(&self->view, view);
    }
    self->width = fit_image_fit_width(self);
    if (view != NULL) {
        GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view));
        if (hadj != NULL) {
            g_signal_connect_object(hadj, "notify::page-size", G_CALLBACK(on_view_page_size),
                                    self, 0);
        }
    }
    return GDK_PAINTABLE(self);
}
