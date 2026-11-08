/* Animated image paintable (see animation.h). */
#include "media/animation.h"

#include "media/gif_decoder.h"
#include "media/image_utils.h"

/* Largest GIF file read, and memory budget of the scaled frames */
#define MAX_GIF_FILE_BYTES (64u * 1024 * 1024)
#define MAX_FRAMES_BYTES (256u * 1024 * 1024)

struct _AnimationFrames {
    GPtrArray* textures;   /* GdkTexture*, all width x height */
    GArray* delays;        /* guint milliseconds, one per texture */
    guint play_count;      /* 0: forever */
    gint width;
    gint height;
};

void animation_frames_free(AnimationFrames* frames) {
    if (frames == NULL) {
        return;
    }
    g_ptr_array_unref(frames->textures);
    g_array_unref(frames->delays);
    g_free(frames);
}

/* Texture of the decoder's canvas scaled to width x height */
static GdkTexture* canvas_texture(GifDecoder* gif, gint width, gint height) {
    gint w = gif_decoder_get_width(gif);
    gint h = gif_decoder_get_height(gif);
    GdkPixbuf* canvas = gdk_pixbuf_new_from_data(gif_decoder_get_canvas(gif), GDK_COLORSPACE_RGB, TRUE, 8,
                                                 w, h, w * 4, NULL, NULL);
    /* Both calls copy the pixels: the canvas changes with the next frame */
    GdkPixbuf* frame = (w == width && h == height)
                           ? gdk_pixbuf_copy(canvas)
                           : gdk_pixbuf_scale_simple(canvas, width, height, GDK_INTERP_BILINEAR);
    g_object_unref(canvas);
    if (frame == NULL) {
        return NULL;
    }
    GdkTexture* texture = texture_new_for_pixbuf(frame);
    g_object_unref(frame);
    return texture;
}

AnimationFrames* animation_frames_load_gif(const gchar* path, gint width, gint height,
                                           GCancellable* cancellable) {
    GMappedFile* mapped = g_mapped_file_new(path, FALSE, NULL);
    if (mapped == NULL) {
        return NULL;
    }
    if (g_mapped_file_get_length(mapped) > MAX_GIF_FILE_BYTES) {
        g_mapped_file_unref(mapped);
        return NULL;
    }
    GBytes* bytes = g_mapped_file_get_bytes(mapped);
    g_mapped_file_unref(mapped);
    GifDecoder* gif = gif_decoder_new(bytes, NULL);
    g_bytes_unref(bytes);
    if (gif == NULL || gif_decoder_get_frame_count(gif) < 2) {
        gif_decoder_free(gif);
        return NULL;
    }

    width = MAX(width, 1);
    height = MAX(height, 1);
    AnimationFrames* frames = g_new0(AnimationFrames, 1);
    frames->textures = g_ptr_array_new_with_free_func(g_object_unref);
    frames->delays = g_array_new(FALSE, FALSE, sizeof(guint));
    frames->play_count = gif_decoder_get_play_count(gif);
    frames->width = width;
    frames->height = height;
    guint64 frame_bytes = (guint64)width * (guint64)height * 4;
    guint max_frames = (guint)MAX(1, MAX_FRAMES_BYTES / frame_bytes);
    guint delay = 0;
    while (frames->textures->len < max_frames && gif_decoder_next_frame(gif, &delay)) {
        if (g_cancellable_is_cancelled(cancellable)) {
            break;
        }
        GdkTexture* texture = canvas_texture(gif, width, height);
        if (texture == NULL) {
            break;
        }
        g_ptr_array_add(frames->textures, texture);
        g_array_append_val(frames->delays, delay);
    }
    gif_decoder_free(gif);
    if (frames->textures->len < 2) {
        animation_frames_free(frames);
        return NULL;
    }
    return frames;
}

struct _LookinAnimation {
    GObject parent;
    AnimationFrames* frames;
    guint current;
    guint plays_done;
    guint timer;
};

static void lookin_animation_paintable_init(GdkPaintableInterface* iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(LookinAnimation, lookin_animation, G_TYPE_OBJECT,
                              G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, lookin_animation_paintable_init))

static gboolean on_tick(gpointer data);

static void schedule(LookinAnimation* self) {
    guint delay = g_array_index(self->frames->delays, guint, self->current);
    self->timer = g_timeout_add(MAX(delay, 20), on_tick, self);
}

static gboolean on_tick(gpointer data) {
    LookinAnimation* self = data;
    self->timer = 0;
    guint next = self->current + 1;
    if (next >= self->frames->textures->len) {
        self->plays_done++;
        if (self->frames->play_count != 0 && self->plays_done >= self->frames->play_count) {
            return G_SOURCE_REMOVE; /* stays on the last frame */
        }
        next = 0;
    }
    self->current = next;
    gdk_paintable_invalidate_contents(GDK_PAINTABLE(self));
    schedule(self);
    return G_SOURCE_REMOVE;
}

static void lookin_animation_snapshot(GdkPaintable* paintable, GdkSnapshot* snapshot, double width, double height) {
    LookinAnimation* self = LOOKIN_ANIMATION(paintable);
    GdkPaintable* frame = g_ptr_array_index(self->frames->textures, self->current);
    gdk_paintable_snapshot(frame, snapshot, width, height);
}

static int lookin_animation_get_intrinsic_width(GdkPaintable* paintable) {
    return LOOKIN_ANIMATION(paintable)->frames->width;
}

static int lookin_animation_get_intrinsic_height(GdkPaintable* paintable) {
    return LOOKIN_ANIMATION(paintable)->frames->height;
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
    animation_frames_free(self->frames);
    G_OBJECT_CLASS(lookin_animation_parent_class)->finalize(object);
}

static void lookin_animation_class_init(LookinAnimationClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = lookin_animation_finalize;
}

static void lookin_animation_init(LookinAnimation* self) {
    (void)self;
}

GdkPaintable* lookin_animation_new(AnimationFrames* frames) {
    g_return_val_if_fail(frames != NULL && frames->textures->len > 0, NULL);
    LookinAnimation* self = g_object_new(LOOKIN_TYPE_ANIMATION, NULL);
    self->frames = frames;
    schedule(self);
    return GDK_PAINTABLE(self);
}
