/* Video player stream on GStreamer playbin (see media_player.h) */
#include "media_player.h"
#include "i18n.h"

#ifdef HAVE_GSTREAMER

#include "media_tracks.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <gst/video/video.h>

#define PLAY_FLAG_TEXT (1u << 2) /* GstPlayFlags of playbin: render subtitles */
#define TICK_MS 100
#define MAX_DIR_ENTRIES 20000 /* scanned for subtitle files */

#define MEDIA_TYPE_PLAYER (media_player_get_type())
G_DECLARE_FINAL_TYPE(MediaPlayer, media_player, MEDIA, PLAYER, GtkMediaFile)

struct _MediaPlayer {
    GtkMediaFile parent_instance;
    GstElement* playbin; /* NULL until the first file, or when GStreamer has no playbin */
    guint bus_watch;
    guint tick_id;

    GMutex lock;            /* guards next_frame, frame_queued (streaming thread) */
    GdkTexture* next_frame; /* decoded, not shown yet */
    gint next_width;        /* its displayed size (pixel aspect ratio applied) */
    gint next_height;
    gboolean frame_queued;

    GdkTexture* frame; /* shown */
    gint width;
    gint height;

    gboolean opened;         /* a file is loaded in playbin */
    gboolean eos;
    gboolean seeking;        /* a seek of gtk_media_stream_seek() */
    gboolean reloading;      /* reopened with another subtitle file */
    gboolean restore_seek;   /* after a reload: back to restore_pos */
    gint64 restore_pos;      /* ns */
    gboolean restore_play;

    gchar* dir;              /* directory of the file */
    gchar** sub_files;       /* subtitle files beside it (names in dir) */
    gint sub_file_loaded;    /* index in sub_files set as playbin's suburi, or -1 */
    gchar* sub_uri_loaded;
    gint sel_embedded;       /* subtitles chosen: a stream of the file, */
    gint sel_file;           /* or a subtitle file, or neither (off) */
};

enum { SIGNAL_TRACKS_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

static void media_player_paintable_init(GdkPaintableInterface* iface);

G_DEFINE_TYPE_WITH_CODE(MediaPlayer, media_player, GTK_TYPE_MEDIA_FILE,
                        G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, media_player_paintable_init))

/* --- Frames: decoded in the streaming thread, shown in the main loop --- */

static void paintable_snapshot(GdkPaintable* paintable, GdkSnapshot* snapshot, double width, double height) {
    MediaPlayer* self = MEDIA_PLAYER(paintable);
    if (self->frame != NULL) gdk_paintable_snapshot(GDK_PAINTABLE(self->frame), snapshot, width, height);
}

static GdkPaintable* paintable_get_current_image(GdkPaintable* paintable) {
    MediaPlayer* self = MEDIA_PLAYER(paintable);
    if (self->frame != NULL) return GDK_PAINTABLE(g_object_ref(self->frame));
    return gdk_paintable_new_empty(self->width, self->height);
}

static int paintable_get_intrinsic_width(GdkPaintable* paintable) {
    return MEDIA_PLAYER(paintable)->width;
}

static int paintable_get_intrinsic_height(GdkPaintable* paintable) {
    return MEDIA_PLAYER(paintable)->height;
}

static double paintable_get_intrinsic_aspect_ratio(GdkPaintable* paintable) {
    MediaPlayer* self = MEDIA_PLAYER(paintable);
    return self->height > 0 ? (double)self->width / self->height : 0.0;
}

static void media_player_paintable_init(GdkPaintableInterface* iface) {
    iface->snapshot = paintable_snapshot;
    iface->get_current_image = paintable_get_current_image;
    iface->get_intrinsic_width = paintable_get_intrinsic_width;
    iface->get_intrinsic_height = paintable_get_intrinsic_height;
    iface->get_intrinsic_aspect_ratio = paintable_get_intrinsic_aspect_ratio;
}

static gboolean show_next_frame(gpointer data) {
    MediaPlayer* self = data;
    g_mutex_lock(&self->lock);
    GdkTexture* tex = self->next_frame;
    gint w = self->next_width;
    gint h = self->next_height;
    self->next_frame = NULL;
    self->frame_queued = FALSE;
    g_mutex_unlock(&self->lock);
    if (tex == NULL) return G_SOURCE_REMOVE;
    gboolean resized = w != self->width || h != self->height;
    g_set_object(&self->frame, tex);
    g_object_unref(tex);
    self->width = w;
    self->height = h;
    if (resized) gdk_paintable_invalidate_size(GDK_PAINTABLE(self));
    gdk_paintable_invalidate_contents(GDK_PAINTABLE(self));
    return G_SOURCE_REMOVE;
}

static void video_frame_free(gpointer data) {
    GstVideoFrame* frame = data;
    gst_video_frame_unmap(frame);
    g_free(frame);
}

/* Streaming thread: the sample as a texture, shown at the next idle */
static void take_sample(MediaPlayer* self, GstSample* sample) {
    GstVideoInfo info;
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps* caps = gst_sample_get_caps(sample);
    if (buffer == NULL || caps == NULL || !gst_video_info_from_caps(&info, caps)) return;
    GstVideoFrame* frame = g_new(GstVideoFrame, 1);
    if (!gst_video_frame_map(frame, &info, buffer, GST_MAP_READ)) { /* takes a ref on buffer */
        g_free(frame);
        return;
    }
    gint w = GST_VIDEO_FRAME_WIDTH(frame);
    gint h = GST_VIDEO_FRAME_HEIGHT(frame);
    gsize stride = (gsize)GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);
    GBytes* bytes = g_bytes_new_with_free_func(GST_VIDEO_FRAME_PLANE_DATA(frame, 0), stride * (gsize)(h - 1) + (gsize)w * 4,
                                               video_frame_free, frame);
    GdkTexture* tex = gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8, bytes, stride);
    g_bytes_unref(bytes);

    gint par_n = GST_VIDEO_INFO_PAR_N(&info);
    gint par_d = GST_VIDEO_INFO_PAR_D(&info);
    gint shown_w = (par_n > 0 && par_d > 0) ? (gint)((gint64)w * par_n / par_d) : w;

    g_mutex_lock(&self->lock);
    gboolean queue = !self->frame_queued;
    if (self->next_frame != NULL) g_object_unref(self->next_frame);
    self->next_frame = tex;
    self->next_width = shown_w;
    self->next_height = h;
    self->frame_queued = TRUE;
    g_mutex_unlock(&self->lock);
    if (queue) g_idle_add_full(G_PRIORITY_DEFAULT, show_next_frame, g_object_ref(self), g_object_unref);
}

static GstFlowReturn on_new_sample(GstAppSink* sink, gpointer data) {
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (sample == NULL) return GST_FLOW_OK;
    take_sample(data, sample);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static GstFlowReturn on_new_preroll(GstAppSink* sink, gpointer data) {
    GstSample* sample = gst_app_sink_pull_preroll(sink);
    if (sample == NULL) return GST_FLOW_OK;
    take_sample(data, sample);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static void clear_frames(MediaPlayer* self) {
    g_mutex_lock(&self->lock);
    g_clear_object(&self->next_frame);
    g_mutex_unlock(&self->lock);
    gboolean had = self->frame != NULL || self->width != 0;
    g_clear_object(&self->frame);
    self->width = 0;
    self->height = 0;
    if (had) {
        gdk_paintable_invalidate_size(GDK_PAINTABLE(self));
        gdk_paintable_invalidate_contents(GDK_PAINTABLE(self));
    }
}

/* --- Position updates while playing --- */

static void update_position(MediaPlayer* self) {
    gint64 pos = 0;
    if (self->playbin != NULL && gst_element_query_position(self->playbin, GST_FORMAT_TIME, &pos) && pos >= 0) {
        gtk_media_stream_update(GTK_MEDIA_STREAM(self), pos / GST_USECOND);
    }
}

static gboolean on_tick(gpointer data) {
    MediaPlayer* self = data;
    if (!self->seeking && !self->reloading) update_position(self);
    return G_SOURCE_CONTINUE;
}

static void stop_tick(MediaPlayer* self) {
    if (self->tick_id != 0) {
        g_source_remove(self->tick_id);
        self->tick_id = 0;
    }
}

static void start_tick(MediaPlayer* self) {
    if (self->tick_id == 0) self->tick_id = g_timeout_add(TICK_MS, on_tick, self);
}

/* --- Tracks --- */

/* pad (a sink pad of playbin's text selector) is fed by the decoder of uri */
static gboolean pad_from_uri(GstPad* pad, const gchar* uri, gint depth) {
    if (depth <= 0) return FALSE;
    GstPad* peer = gst_pad_get_peer(pad);
    if (peer == NULL) return FALSE;
    GstElement* el = gst_pad_get_parent_element(peer);
    gst_object_unref(peer);
    if (el == NULL) return FALSE;
    gboolean found = FALSE;
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(el), "uri") != NULL) {
        gchar* el_uri = NULL;
        g_object_get(el, "uri", &el_uri, NULL);
        found = g_strcmp0(el_uri, uri) == 0;
        g_free(el_uri);
    } else {
        GstIterator* it = gst_element_iterate_sink_pads(el);
        GValue item = G_VALUE_INIT;
        while (!found && gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            found = pad_from_uri(g_value_get_object(&item), uri, depth - 1);
            g_value_reset(&item);
        }
        g_value_unset(&item);
        gst_iterator_free(it);
    }
    gst_object_unref(el);
    return found;
}

/* playbin text stream i comes from the subtitle file (suburi) */
static gboolean text_is_external(MediaPlayer* self, gint i) {
    if (self->sub_uri_loaded == NULL) return FALSE;
    GstPad* pad = NULL;
    g_signal_emit_by_name(self->playbin, "get-text-pad", i, &pad);
    if (pad == NULL) return FALSE;
    gboolean ext = pad_from_uri(pad, self->sub_uri_loaded, 8);
    gst_object_unref(pad);
    return ext;
}

static gint n_streams(MediaPlayer* self, const gchar* prop) {
    gint n = 0;
    if (self->playbin != NULL && gtk_media_stream_is_prepared(GTK_MEDIA_STREAM(self))) {
        g_object_get(self->playbin, prop, &n, NULL);
    }
    return n;
}

/* playbin indices of the subtitle streams of the file itself, and of the
 * loaded subtitle file (-1 if none) */
static GArray* embedded_text_streams(MediaPlayer* self, gint* external) {
    GArray* idx = g_array_new(FALSE, FALSE, sizeof(gint));
    gint n = n_streams(self, "n-text");
    gint ext = -1;
    for (gint i = 0; i < n; i++) {
        if (ext < 0 && text_is_external(self, i)) {
            ext = i;
        } else {
            g_array_append_val(idx, i);
        }
    }
    if (ext < 0 && self->sub_uri_loaded != NULL && idx->len > 0) {
        /* Not found from the pads: playbin adds the file's stream last */
        ext = g_array_index(idx, gint, idx->len - 1);
        g_array_set_size(idx, idx->len - 1);
    }
    if (external != NULL) *external = ext;
    return idx;
}

static gchar* stream_label(MediaPlayer* self, const gchar* signal, gint i, guint shown_index) {
    GstTagList* tags = NULL;
    g_signal_emit_by_name(self->playbin, signal, i, &tags);
    gchar* code = NULL;
    gchar* lang_name = NULL;
    gchar* title = NULL;
    if (tags != NULL) {
        gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &code);
        gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_NAME, &lang_name);
        gst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
        gst_tag_list_unref(tags);
    }
    const gchar* language = lang_name;
    if (language == NULL && code != NULL) {
        language = gst_tag_get_language_name(code);
        if (language == NULL) language = code;
    }
    gchar* label = media_track_label(language, title, shown_index);
    g_free(code);
    g_free(lang_name);
    g_free(title);
    return label;
}

static void apply_subtitles(MediaPlayer* self) {
    if (self->playbin == NULL || !gtk_media_stream_is_prepared(GTK_MEDIA_STREAM(self))) return;
    gint external = -1;
    GArray* embedded = embedded_text_streams(self, &external);
    gint pb = -1;
    if (self->sel_file >= 0 && self->sel_file == self->sub_file_loaded) {
        pb = external;
    } else if (self->sel_embedded >= 0 && (guint)self->sel_embedded < embedded->len) {
        pb = g_array_index(embedded, gint, self->sel_embedded);
    }
    g_array_free(embedded, TRUE);
    guint flags = 0;
    g_object_get(self->playbin, "flags", &flags, NULL);
    if (pb >= 0) {
        g_object_set(self->playbin, "current-text", pb, NULL);
        flags |= PLAY_FLAG_TEXT;
    } else {
        flags &= ~PLAY_FLAG_TEXT;
    }
    g_object_set(self->playbin, "flags", flags, NULL);
}

static void set_sub_uri(MediaPlayer* self, gint file_index) {
    g_clear_pointer(&self->sub_uri_loaded, g_free);
    self->sub_file_loaded = -1;
    if (file_index >= 0 && self->sub_files != NULL && self->sub_files[file_index] != NULL) {
        gchar* path = g_build_filename(self->dir, self->sub_files[file_index], NULL);
        self->sub_uri_loaded = g_filename_to_uri(path, NULL, NULL);
        self->sub_file_loaded = self->sub_uri_loaded != NULL ? file_index : -1;
        g_free(path);
    }
    g_object_set(self->playbin, "suburi", self->sub_uri_loaded, NULL);
}

/* Drop the messages not handled yet: they are about the previous state */
static void flush_bus(MediaPlayer* self) {
    GstBus* bus = gst_element_get_bus(self->playbin);
    gst_bus_set_flushing(bus, TRUE);
    gst_bus_set_flushing(bus, FALSE);
    gst_object_unref(bus);
}

/* Paused: decode the current frame again (with the new subtitles) */
static void redraw_paused(MediaPlayer* self) {
    gint64 pos = 0;
    if (gtk_media_stream_get_playing(GTK_MEDIA_STREAM(self)) || self->seeking || self->reloading ||
        !gst_element_query_position(self->playbin, GST_FORMAT_TIME, &pos)) {
        return;
    }
    if (gst_element_seek_simple(self->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, pos)) {
        self->restore_seek = TRUE;
    }
}

/* Another subtitle file: playbin takes a new suburi only when reopened, at
 * the same place */
static void reload_with_sub_file(MediaPlayer* self, gint file_index) {
    GtkMediaStream* stream = GTK_MEDIA_STREAM(self);
    self->restore_pos = 0;
    gst_element_query_position(self->playbin, GST_FORMAT_TIME, &self->restore_pos);
    self->restore_play = gtk_media_stream_get_playing(stream);
    stop_tick(self);
    gst_element_set_state(self->playbin, GST_STATE_READY);
    flush_bus(self);
    if (self->seeking) {
        self->seeking = FALSE;
        gtk_media_stream_seek_success(stream);
    }
    self->restore_seek = FALSE;
    set_sub_uri(self, file_index);
    self->reloading = TRUE;
    gst_element_set_state(self->playbin, GST_STATE_PAUSED);
}

/* --- Bus --- */

static void on_prepared(MediaPlayer* self) {
    GtkMediaStream* stream = GTK_MEDIA_STREAM(self);
    gint64 duration = 0;
    if (!gst_element_query_duration(self->playbin, GST_FORMAT_TIME, &duration) || duration < 0) duration = 0;
    gboolean seekable = FALSE;
    GstQuery* q = gst_query_new_seeking(GST_FORMAT_TIME);
    if (gst_element_query(self->playbin, q)) gst_query_parse_seeking(q, NULL, &seekable, NULL, NULL);
    gst_query_unref(q);
    gint n_video = 0;
    gint n_audio = 0;
    g_object_get(self->playbin, "n-video", &n_video, "n-audio", &n_audio, NULL);
    gtk_media_stream_stream_prepared(stream, n_audio > 0, n_video > 0, seekable, duration / GST_USECOND);
    apply_subtitles(self);
    g_signal_emit(self, signals[SIGNAL_TRACKS_CHANGED], 0);
}

static void on_async_done(MediaPlayer* self) {
    GtkMediaStream* stream = GTK_MEDIA_STREAM(self);
    if (!gtk_media_stream_is_prepared(stream)) {
        on_prepared(self);
    } else if (self->reloading) {
        self->reloading = FALSE;
        apply_subtitles(self);
        g_signal_emit(self, signals[SIGNAL_TRACKS_CHANGED], 0);
        self->restore_seek = TRUE;
        gst_element_seek_simple(self->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                                self->restore_pos);
        if (self->restore_play) {
            gst_element_set_state(self->playbin, GST_STATE_PLAYING);
            start_tick(self);
        }
    } else if (self->seeking) {
        self->seeking = FALSE;
        gtk_media_stream_seek_success(stream);
        update_position(self);
    } else if (self->restore_seek) {
        self->restore_seek = FALSE;
        update_position(self);
    }
}

static gboolean on_bus(GstBus* bus, GstMessage* msg, gpointer data) {
    (void)bus;
    MediaPlayer* self = data;
    GtkMediaStream* stream = GTK_MEDIA_STREAM(self);
    if (!self->opened) return G_SOURCE_CONTINUE; /* about a file closed since */
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = NULL;
        gst_message_parse_error(msg, &err, NULL);
        stop_tick(self);
        if (self->seeking) {
            self->seeking = FALSE;
            gtk_media_stream_seek_failed(stream);
        }
        if (err != NULL) gtk_media_stream_gerror(stream, err); /* takes err */
        break;
    }
    case GST_MESSAGE_EOS:
        stop_tick(self);
        self->eos = TRUE;
        if (gtk_media_stream_is_prepared(stream)) {
            gtk_media_stream_update(stream, gtk_media_stream_get_duration(stream));
            gtk_media_stream_stream_ended(stream);
        }
        break;
    case GST_MESSAGE_ASYNC_DONE:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->playbin)) on_async_done(self);
        break;
    default:
        break;
    }
    return G_SOURCE_CONTINUE;
}

/* --- GtkMediaStream / GtkMediaFile --- */

static gboolean ensure_playbin(MediaPlayer* self) {
    if (self->playbin != NULL) return TRUE;
    if (!gst_init_check(NULL, NULL, NULL)) return FALSE;
    GstElement* playbin = gst_element_factory_make("playbin", NULL);
    GstElement* sink = gst_element_factory_make("appsink", NULL);
    if (playbin == NULL || sink == NULL) {
        if (playbin != NULL) gst_object_unref(playbin);
        if (sink != NULL) gst_object_unref(sink);
        return FALSE;
    }
    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA", NULL);
    g_object_set(sink, "caps", caps, "enable-last-sample", FALSE, "max-buffers", 2, NULL);
    gst_caps_unref(caps);
    GstAppSinkCallbacks callbacks = {0};
    callbacks.new_sample = on_new_sample;
    callbacks.new_preroll = on_new_preroll;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, self, NULL);
    g_object_set(playbin, "video-sink", sink, NULL);
    self->playbin = gst_object_ref_sink(playbin);
    /* A new playbin per file: keep the volume of the previous one */
    GtkMediaStream* stream = GTK_MEDIA_STREAM(self);
    gdouble volume = gtk_media_stream_get_volume(stream);
    g_object_set(playbin, "mute", gtk_media_stream_get_muted(stream), "volume", volume * volume * volume, NULL);
    GstBus* bus = gst_element_get_bus(playbin);
    self->bus_watch = gst_bus_add_watch(bus, on_bus, self);
    gst_object_unref(bus);
    return TRUE;
}

/* A playbin is never reused for another file: its stream synchronizer
 * numbers its pads from 0 again once in NULL, while pads requested late by a
 * streaming thread can survive, and the next file then aborts GStreamer with
 * "Padname sink_1 is not unique". */
static void release_playbin(MediaPlayer* self) {
    if (self->playbin == NULL) return;
    gst_element_set_state(self->playbin, GST_STATE_NULL);
    if (self->bus_watch != 0) g_source_remove(self->bus_watch);
    self->bus_watch = 0;
    gst_clear_object(&self->playbin);
}

static void find_sub_files(MediaPlayer* self, const gchar* path) {
    g_clear_pointer(&self->dir, g_free);
    g_clear_pointer(&self->sub_files, g_strfreev);
    self->dir = g_path_get_dirname(path);
    GDir* dir = g_dir_open(self->dir, 0, NULL);
    if (dir == NULL) return;
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    const gchar* n;
    while (names->len < MAX_DIR_ENTRIES && (n = g_dir_read_name(dir)) != NULL) g_ptr_array_add(names, g_strdup(n));
    g_dir_close(dir);
    g_ptr_array_add(names, NULL);
    gchar* base = g_path_get_basename(path);
    self->sub_files = media_subtitle_files(base, (const gchar* const*)names->pdata);
    g_free(base);
    g_ptr_array_unref(names);
}

static void player_open(GtkMediaFile* file) {
    MediaPlayer* self = MEDIA_PLAYER(file);
    GtkMediaStream* stream = GTK_MEDIA_STREAM(file);
    GFile* gfile = gtk_media_file_get_file(file);
    gchar* path = gfile != NULL ? g_file_get_path(gfile) : NULL;
    if (path == NULL) {
        gtk_media_stream_error(stream, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Only local files can be played"));
        return;
    }
    if (!ensure_playbin(self)) {
        gtk_media_stream_error(stream, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "GStreamer has no playbin (install gstreamer1.0-plugins-base)");
        g_free(path);
        return;
    }
    self->eos = FALSE;
    self->seeking = FALSE;
    self->reloading = FALSE;
    self->restore_seek = FALSE;
    find_sub_files(self, path);
    /* A subtitle file beside the video is shown from the start */
    self->sel_embedded = -1;
    self->sel_file = (self->sub_files != NULL && self->sub_files[0] != NULL) ? 0 : -1;

    gchar* uri = g_file_get_uri(gfile);
    g_object_set(self->playbin, "uri", uri, NULL);
    set_sub_uri(self, self->sel_file);
    guint flags = 0;
    g_object_get(self->playbin, "flags", &flags, NULL);
    g_object_set(self->playbin, "flags", flags | PLAY_FLAG_TEXT, NULL);
    g_free(uri);
    g_free(path);
    self->opened = TRUE;
    gst_element_set_state(self->playbin, GST_STATE_PAUSED);
}

static void player_close(GtkMediaFile* file) {
    MediaPlayer* self = MEDIA_PLAYER(file);
    GtkMediaStream* stream = GTK_MEDIA_STREAM(file);
    /* First: unpreparing pauses the stream, which must not reopen the file */
    self->opened = FALSE;
    stop_tick(self);
    release_playbin(self);
    clear_frames(self);
    g_clear_pointer(&self->sub_files, g_strfreev);
    g_clear_pointer(&self->dir, g_free);
    g_clear_pointer(&self->sub_uri_loaded, g_free);
    self->sub_file_loaded = -1;
    self->sel_embedded = -1;
    self->sel_file = -1;
    self->seeking = FALSE;
    self->reloading = FALSE;
    self->restore_seek = FALSE;
    if (gtk_media_stream_is_prepared(stream)) gtk_media_stream_stream_unprepared(stream);
    g_signal_emit(self, signals[SIGNAL_TRACKS_CHANGED], 0);
}

static gboolean player_play(GtkMediaStream* stream) {
    MediaPlayer* self = MEDIA_PLAYER(stream);
    if (!self->opened) return FALSE;
    if (self->eos) {
        self->eos = FALSE;
        gst_element_seek_simple(self->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, 0);
    }
    if (self->reloading) {
        self->restore_play = TRUE;
        return TRUE;
    }
    gst_element_set_state(self->playbin, GST_STATE_PLAYING);
    start_tick(self);
    return TRUE;
}

static void player_pause(GtkMediaStream* stream) {
    MediaPlayer* self = MEDIA_PLAYER(stream);
    stop_tick(self);
    if (self->reloading) {
        self->restore_play = FALSE;
        return;
    }
    if (!self->opened) return;
    gst_element_set_state(self->playbin, GST_STATE_PAUSED);
    update_position(self);
}

static void player_seek(GtkMediaStream* stream, gint64 timestamp) {
    MediaPlayer* self = MEDIA_PLAYER(stream);
    if (!self->opened || self->reloading ||
        !gst_element_seek_simple(self->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                                 timestamp * GST_USECOND)) {
        gtk_media_stream_seek_failed(stream);
        return;
    }
    self->eos = FALSE;
    self->seeking = TRUE;
}

static void player_update_audio(GtkMediaStream* stream, gboolean muted, double volume) {
    MediaPlayer* self = MEDIA_PLAYER(stream);
    if (self->playbin == NULL) return;
    /* The volume slider is perceptual, playbin's volume linear */
    g_object_set(self->playbin, "mute", muted, "volume", volume * volume * volume, NULL);
}

static void player_dispose(GObject* object) {
    MediaPlayer* self = MEDIA_PLAYER(object);
    stop_tick(self);
    release_playbin(self);
    clear_frames(self);
    G_OBJECT_CLASS(media_player_parent_class)->dispose(object);
}

static void player_finalize(GObject* object) {
    MediaPlayer* self = MEDIA_PLAYER(object);
    g_strfreev(self->sub_files);
    g_free(self->dir);
    g_free(self->sub_uri_loaded);
    g_mutex_clear(&self->lock);
    G_OBJECT_CLASS(media_player_parent_class)->finalize(object);
}

static void media_player_class_init(MediaPlayerClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GtkMediaStreamClass* stream_class = GTK_MEDIA_STREAM_CLASS(klass);
    GtkMediaFileClass* file_class = GTK_MEDIA_FILE_CLASS(klass);
    object_class->dispose = player_dispose;
    object_class->finalize = player_finalize;
    stream_class->play = player_play;
    stream_class->pause = player_pause;
    stream_class->seek = player_seek;
    stream_class->update_audio = player_update_audio;
    file_class->open = player_open;
    file_class->close = player_close;
    signals[SIGNAL_TRACKS_CHANGED] =
        g_signal_new("tracks-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void media_player_init(MediaPlayer* self) {
    g_mutex_init(&self->lock);
    self->sub_file_loaded = -1;
    self->sel_embedded = -1;
    self->sel_file = -1;
}

/* --- Public API --- */

GtkMediaStream* media_player_new(void) {
    return g_object_new(MEDIA_TYPE_PLAYER, NULL);
}

gchar** media_player_track_labels(GtkMediaStream* stream, MediaTrackKind kind) {
    if (!MEDIA_IS_PLAYER(stream)) return NULL;
    MediaPlayer* self = MEDIA_PLAYER(stream);
    GPtrArray* labels = g_ptr_array_new();
    if (kind == MEDIA_TRACK_AUDIO) {
        gint n = n_streams(self, "n-audio");
        for (gint i = 0; i < n; i++) g_ptr_array_add(labels, stream_label(self, "get-audio-tags", i, (guint)i));
    } else if (gtk_media_stream_is_prepared(stream)) {
        GArray* embedded = embedded_text_streams(self, NULL);
        for (guint i = 0; i < embedded->len; i++) {
            g_ptr_array_add(labels, stream_label(self, "get-text-tags", g_array_index(embedded, gint, i), i));
        }
        g_array_free(embedded, TRUE);
        for (guint i = 0; self->sub_files != NULL && self->sub_files[i] != NULL; i++) {
            g_ptr_array_add(labels, g_strdup(self->sub_files[i]));
        }
    }
    g_ptr_array_add(labels, NULL);
    return (gchar**)g_ptr_array_free(labels, FALSE);
}

static guint n_embedded_text(MediaPlayer* self) {
    GArray* embedded = embedded_text_streams(self, NULL);
    guint n = embedded->len;
    g_array_free(embedded, TRUE);
    return n;
}

gint media_player_get_track(GtkMediaStream* stream, MediaTrackKind kind) {
    if (!MEDIA_IS_PLAYER(stream) || !gtk_media_stream_is_prepared(stream)) return -1;
    MediaPlayer* self = MEDIA_PLAYER(stream);
    if (kind == MEDIA_TRACK_AUDIO) {
        gint cur = -1;
        if (n_streams(self, "n-audio") > 0) g_object_get(self->playbin, "current-audio", &cur, NULL);
        return MAX(cur, 0) < n_streams(self, "n-audio") ? MAX(cur, 0) : -1;
    }
    if (self->sel_file >= 0) return (gint)n_embedded_text(self) + self->sel_file;
    return self->sel_embedded;
}

void media_player_set_track(GtkMediaStream* stream, MediaTrackKind kind, gint index) {
    if (!MEDIA_IS_PLAYER(stream) || !gtk_media_stream_is_prepared(stream)) return;
    MediaPlayer* self = MEDIA_PLAYER(stream);
    if (self->reloading) return;
    if (kind == MEDIA_TRACK_AUDIO) {
        if (index < 0 || index >= n_streams(self, "n-audio")) return;
        g_object_set(self->playbin, "current-audio", index, NULL);
        g_signal_emit(self, signals[SIGNAL_TRACKS_CHANGED], 0);
        return;
    }
    gint n_embedded = (gint)n_embedded_text(self);
    gint n_files = self->sub_files != NULL ? (gint)g_strv_length(self->sub_files) : 0;
    if (index >= n_embedded + n_files) return;
    self->sel_embedded = index >= 0 && index < n_embedded ? index : -1;
    self->sel_file = index >= n_embedded ? index - n_embedded : -1;
    if (self->sel_file >= 0 && self->sel_file != self->sub_file_loaded) {
        reload_with_sub_file(self, self->sel_file);
    } else {
        apply_subtitles(self);
        redraw_paused(self);
    }
    g_signal_emit(self, signals[SIGNAL_TRACKS_CHANGED], 0);
}

gboolean media_player_cycle_track(GtkMediaStream* stream, MediaTrackKind kind) {
    gchar** labels = media_player_track_labels(stream, kind);
    gint n = labels != NULL ? (gint)g_strv_length(labels) : 0;
    g_strfreev(labels);
    gint cur = media_player_get_track(stream, kind);
    if (kind == MEDIA_TRACK_AUDIO) {
        if (n < 2) return FALSE;
        media_player_set_track(stream, kind, (cur + 1) % n);
    } else {
        if (n < 1) return FALSE;
        media_player_set_track(stream, kind, cur + 1 < n ? cur + 1 : -1);
    }
    return TRUE;
}

void media_player_connect_tracks_changed(GtkMediaStream* stream, GCallback callback, gpointer object) {
    if (MEDIA_IS_PLAYER(stream)) g_signal_connect_object(stream, "tracks-changed", callback, object, 0);
}

#else /* !HAVE_GSTREAMER */

GtkMediaStream* media_player_new(void) {
    return gtk_media_file_new();
}

gchar** media_player_track_labels(GtkMediaStream* stream, MediaTrackKind kind) {
    (void)stream;
    (void)kind;
    return NULL;
}

gint media_player_get_track(GtkMediaStream* stream, MediaTrackKind kind) {
    (void)stream;
    (void)kind;
    return -1;
}

void media_player_set_track(GtkMediaStream* stream, MediaTrackKind kind, gint index) {
    (void)stream;
    (void)kind;
    (void)index;
}

gboolean media_player_cycle_track(GtkMediaStream* stream, MediaTrackKind kind) {
    (void)stream;
    (void)kind;
    return FALSE;
}

void media_player_connect_tracks_changed(GtkMediaStream* stream, GCallback callback, gpointer object) {
    (void)stream;
    (void)callback;
    (void)object;
}

#endif /* HAVE_GSTREAMER */
