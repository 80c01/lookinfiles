/* Animated image paintable (see animation.h). */
#include "animation.h"

#include "image_utils.h"

struct _LookinAnimation {
    GObject parent;
    GdkPixbufAnimation* animation;
    GdkPixbufAnimationIter* iter;
    GdkTexture* frame;     /* current frame, scaled */
    gint width;
    gint height;
    guint timer;
};

static void lookin_animation_paintable_init(GdkPaintableInterface* iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(LookinAnimation, lookin_animation, G_TYPE_OBJECT,
                              G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, lookin_animation_paintable_init))

static void update_frame(LookinAnimation* self) {
    GdkPixbuf* pix = gdk_pixbuf_animation_iter_get_pixbuf(self->iter);
    if (pix == NULL) {
        return;
    }
    GdkPixbuf* scaled = NULL;
    if (gdk_pixbuf_get_width(pix) != self->width || gdk_pixbuf_get_height(pix) != self->height) {
        scaled = gdk_pixbuf_scale_simple(pix, self->width, self->height, GDK_INTERP_BILINEAR);
    }
    g_clear_object(&self->frame);
    self->frame = texture_new_for_pixbuf(scaled != NULL ? scaled : pix);
    g_clear_object(&scaled);
}

static gboolean on_tick(gpointer data);

static void schedule(LookinAnimation* self) {
    gint delay = gdk_pixbuf_animation_iter_get_delay_time(self->iter);
    if (delay < 0) {
        self->timer = 0; /* last frame of a non-looping animation */
        return;
    }
    self->timer = g_timeout_add(MAX(delay, 20), on_tick, self);
}

static gboolean on_tick(gpointer data) {
    LookinAnimation* self = data;
    self->timer = 0;
    if (gdk_pixbuf_animation_iter_advance(self->iter, NULL)) {
        update_frame(self);
        gdk_paintable_invalidate_contents(GDK_PAINTABLE(self));
    }
    schedule(self);
    return G_SOURCE_REMOVE;
}

static void lookin_animation_snapshot(GdkPaintable* paintable, GdkSnapshot* snapshot, double width, double height) {
    LookinAnimation* self = LOOKIN_ANIMATION(paintable);
    if (self->frame != NULL) {
        gdk_paintable_snapshot(GDK_PAINTABLE(self->frame), snapshot, width, height);
    }
}

static int lookin_animation_get_intrinsic_width(GdkPaintable* paintable) {
    return LOOKIN_ANIMATION(paintable)->width;
}

static int lookin_animation_get_intrinsic_height(GdkPaintable* paintable) {
    return LOOKIN_ANIMATION(paintable)->height;
}

static void lookin_animation_paintable_init(GdkPaintableInterface* iface) {
    iface->snapshot = lookin_animation_snapshot;
    iface->get_intrinsic_width = lookin_animation_get_intrinsic_width;
    iface->get_intrinsic_height = lookin_animation_get_intrinsic_height;
}

static void lookin_animation_finalize(GObject* object) {
    LookinAnimation* self = LOOKIN_ANIMATION(object);
    if (self->timer != 0) {
        g_source_remove(self->timer);
    }
    g_clear_object(&self->frame);
    g_clear_object(&self->iter);
    g_clear_object(&self->animation);
    G_OBJECT_CLASS(lookin_animation_parent_class)->finalize(object);
}

static void lookin_animation_class_init(LookinAnimationClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = lookin_animation_finalize;
}

static void lookin_animation_init(LookinAnimation* self) {
    (void)self;
}

GdkPaintable* lookin_animation_new(GdkPixbufAnimation* animation, gint width, gint height) {
    LookinAnimation* self = g_object_new(LOOKIN_TYPE_ANIMATION, NULL);
    self->animation = g_object_ref(animation);
    self->width = MAX(width, 1);
    self->height = MAX(height, 1);
    self->iter = gdk_pixbuf_animation_get_iter(animation, NULL);
    update_frame(self);
    schedule(self);
    return GDK_PAINTABLE(self);
}
