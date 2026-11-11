#include "media/audio.h"
#include "preview/preview_state.h"
#include "search/search.h"
#include "core/i18n.h"
#include "theme/text_style.h"
#include "core/file_types.h"
#include "media/image_utils.h"
#include "media/media_player.h"
#include "media/media_tags.h"
#include "theme/icons.h"
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

/* Audio player state (app->audio), created by audio_preview_init() */
typedef struct AudioPreview AudioPreview;
struct AudioPreview {
    GtkWidget* controls;     /* GtkMediaControls */
    GtkWidget* title;        /* title label */
    GtkWidget* meta;         /* format, size and duration label */
    GtkWidget* status;       /* warning / error label */
    GtkWidget* icon;         /* note icon, hidden when there is a cover */
    GtkWidget* cover;        /* GtkImage: embedded cover art */
    GtkWidget* tag_line;     /* album / year / track / genre */
    gchar* details;          /* technical line from the tags (codec, rate...), or NULL */
    GtkMediaStream* stream;  /* the session's stream: previews swap its file */
    gchar* current_path;     /* file loaded in the player, or NULL */
};

static void update_audio_metadata_label(AppData* app, GtkMediaStream* stream) {
    AudioPreview* a = app != NULL ? app->audio : NULL;
    if (a == NULL || a->current_path == NULL) {
        return;
    }

    GStatBuf st;
    gint64 fsize = (g_stat(a->current_path, &st) == 0) ? (gint64)st.st_size : 0;
    gchar* size_str = g_format_size((guint64)fsize);

    const gchar* ext = file_extension(a->current_path);
    gchar* ext_upper = g_ascii_strup(ext, -1);

    GString* info = g_string_new(a->details != NULL ? a->details : ext_upper);
    g_string_append_printf(info, "  •  %s", size_str);

    if (stream != NULL && (a->details == NULL || strchr(a->details, ':') == NULL)) {
        gint64 dur_us = gtk_media_stream_get_duration(stream);
        if (dur_us > 0) {
            gint64 total_secs = dur_us / G_USEC_PER_SEC;
            gint hours = (gint)(total_secs / 3600);
            gint mins = (gint)((total_secs % 3600) / 60);
            gint secs = (gint)(total_secs % 60);
            if (hours > 0) {
                g_string_append_printf(info, "  •  %d:%02d:%02d", hours, mins, secs);
            } else {
                g_string_append_printf(info, "  •  %02d:%02d", mins, secs);
            }
        }
    }

    g_string_append(info, _("  •  [press m for details]"));

    gtk_label_set_text(GTK_LABEL(a->meta), info->str);

    g_string_free(info, TRUE);
    g_free(ext_upper);
    g_free(size_str);
}

static void on_audio_duration_notify(GtkMediaStream* stream, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    update_audio_metadata_label(app, stream);
}

static void on_audio_prepared_notify(GtkMediaStream* stream, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    update_audio_metadata_label(app, stream);
}

static void on_audio_error_notify(GtkMediaStream* stream, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    if (app == NULL || app->audio == NULL || stream == NULL) {
        return;
    }
    AudioPreview* a = app->audio;
    if (a->current_path == NULL) {
        return; /* about a file closed since */
    }
    const GError* err = gtk_media_stream_get_error(stream);
    if (err != NULL) {
        const gchar* err_msg = (err->message != NULL && err->message[0] != '\0')
                                   ? err->message
                                   : _("Audio playback error");
        gchar* msg = g_strdup_printf(_("Media playback warning: %s (press 'm' to inspect raw file details)"),
                                     err_msg);
        gtk_label_set_text(GTK_LABEL(a->status), msg);
        gtk_widget_set_visible(a->status, TRUE);
        g_free(msg);
    }
}

void audio_preview_init(AppData* app) {
    if (app == NULL || app->preview_stack == NULL) {
        return;
    }

    GtkWidget* audio_scroll = gtk_scrolled_window_new();
    gtk_widget_set_name(audio_scroll, "audio-scroll");
    gtk_widget_set_vexpand(audio_scroll, TRUE);
    gtk_widget_set_hexpand(audio_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(audio_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget* audio_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(audio_box, "audio-box");
    gtk_widget_set_hexpand(audio_box, TRUE);
    gtk_widget_set_vexpand(audio_box, TRUE);
    gtk_widget_set_halign(audio_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(audio_box, GTK_ALIGN_CENTER);

    /* Card container */
    GtkWidget* audio_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_name(audio_card, "audio-card");
    gtk_widget_set_halign(audio_card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(audio_card, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(audio_card, 400, -1);

    /* Big audio file icon, or the cover art when the file has one */
    GtkWidget* icon_label = file_type_image_new();
    file_type_image_set(icon_label, "audio.mp3", FALSE);
    gtk_image_set_pixel_size(GTK_IMAGE(icon_label), 64);
    gtk_widget_set_name(icon_label, "audio-icon");
    gtk_widget_set_halign(icon_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(audio_card), icon_label);
    GtkWidget* cover = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(cover), 240);
    gtk_widget_set_halign(cover, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(cover, FALSE);
    gtk_box_append(GTK_BOX(audio_card), cover);

    /* Title label */
    GtkWidget* audio_title = gtk_label_new(_("Audio Preview"));
    gtk_widget_set_name(audio_title, "audio-title");
    gtk_label_set_selectable(GTK_LABEL(audio_title), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(audio_title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(audio_title), 0.5f);
    gtk_box_append(GTK_BOX(audio_card), audio_title);

    /* Album, year, track, genre (from the tags) */
    GtkWidget* audio_tags = gtk_label_new("");
    gtk_widget_set_name(audio_tags, "audio-tags");
    gtk_label_set_selectable(GTK_LABEL(audio_tags), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(audio_tags), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(audio_tags), 0.5f);
    gtk_widget_set_visible(audio_tags, FALSE);
    gtk_box_append(GTK_BOX(audio_card), audio_tags);

    /* Metadata label */
    GtkWidget* audio_meta = gtk_label_new("");
    gtk_widget_set_name(audio_meta, "audio-meta");
    gtk_label_set_selectable(GTK_LABEL(audio_meta), TRUE);
    gtk_label_set_xalign(GTK_LABEL(audio_meta), 0.5f);
    gtk_box_append(GTK_BOX(audio_card), audio_meta);

    /* Status / error label */
    GtkWidget* audio_status = gtk_label_new("");
    gtk_widget_set_name(audio_status, "audio-status");
    gtk_label_set_xalign(GTK_LABEL(audio_status), 0.5f);
    gtk_widget_set_visible(audio_status, FALSE);
    gtk_box_append(GTK_BOX(audio_card), audio_status);

    /* Standard playback controls: Play/Pause, Seek bar, Time, Volume */
    GtkWidget* audio_controls = gtk_media_controls_new(NULL);
    gtk_widget_set_name(audio_controls, "audio-controls");
    gtk_widget_set_hexpand(audio_controls, TRUE);
    gtk_box_append(GTK_BOX(audio_card), audio_controls);

    gtk_box_append(GTK_BOX(audio_box), audio_card);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(audio_scroll), audio_box);
    gtk_stack_add_named(GTK_STACK(app->preview_stack), audio_scroll, "audio");

    AudioPreview* a = g_new0(AudioPreview, 1);
    app->audio = a;
    a->controls = audio_controls;
    a->title = audio_title;
    a->meta = audio_meta;
    a->status = audio_status;
    a->icon = icon_label;
    a->cover = cover;
    a->tag_line = audio_tags;

    /* One media stream for the whole session, as for videos (video.c): a
     * GTK stream per file is never freed by its GStreamer backend, and the
     * pipelines left behind kept running until one aborted the process. */
    a->stream = media_player_new();
    g_signal_connect(a->stream, "notify::duration", G_CALLBACK(on_audio_duration_notify), app);
    g_signal_connect(a->stream, "notify::prepared", G_CALLBACK(on_audio_prepared_notify), app);
    g_signal_connect(a->stream, "notify::error", G_CALLBACK(on_audio_error_notify), app);
    gtk_media_controls_set_media_stream(GTK_MEDIA_CONTROLS(audio_controls), a->stream);
}

void audio_preview_stop(AppData* app) {
    AudioPreview* a = app != NULL ? app->audio : NULL;
    if (a == NULL || a->current_path == NULL) {
        return;
    }
    /* Cleared first: the notifications emitted while closing see no file */
    g_clear_pointer(&a->current_path, g_free);
    g_clear_pointer(&a->details, g_free);

    if (gtk_media_stream_get_playing(a->stream)) {
        gtk_media_stream_pause(a->stream);
    }
    gtk_media_file_clear(GTK_MEDIA_FILE(a->stream));
}

void audio_preview_free(AppData* app) {
    if (app == NULL || app->audio == NULL) {
        return;
    }
    audio_preview_stop(app);
    gtk_media_controls_set_media_stream(GTK_MEDIA_CONTROLS(app->audio->controls), NULL);
    g_clear_object(&app->audio->stream);
    g_clear_pointer(&app->audio, g_free);
}

void audio_preview_pause(AppData* app) {
    AudioPreview* a = app != NULL ? app->audio : NULL;
    if (a == NULL || a->current_path == NULL) {
        return;
    }
    if (gtk_media_stream_get_playing(a->stream)) {
        gtk_media_stream_pause(a->stream);
    }
}

void audio_preview_toggle_play(AppData* app) {
    AudioPreview* a = app != NULL ? app->audio : NULL;
    if (a == NULL || a->current_path == NULL) {
        return;
    }
    if (gtk_media_stream_get_playing(a->stream)) {
        gtk_media_stream_pause(a->stream);
    } else {
        gtk_media_stream_play(a->stream);
    }
}

static void render_audio_raw(AppData* app, const gchar* name, const gchar* full) {
    audio_preview_stop(app);

    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }

    apply_viewer_language(app, NULL, NULL);
    set_view_monospace(app->text_view, TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    set_view_text_padding(app->text_view);

    GStatBuf st;
    gint64 fsize = (g_stat(full, &st) == 0) ? (gint64)st.st_size : 0;
    gchar* size_str = g_format_size((guint64)fsize);

    GDateTime* dt = g_date_time_new_from_unix_local(st.st_mtime);
    gchar* time_str = dt ? g_date_time_format(dt, "%Y-%m-%d %H:%M:%S") : g_strdup("-");
    if (dt != NULL) {
        g_date_time_unref(dt);
    }

    const gchar* ext = file_extension(name);
    gchar* ext_upper = g_ascii_strup(ext, -1);

    GString* out = g_string_new(NULL);
    g_string_append_printf(out, "%s\n", name);
    g_string_append_printf(out, "═══════════════════════════════════════════════════════════════\n");
    gchar* bytes = g_strdup_printf("%" G_GINT64_FORMAT, fsize);
    /* TRANSLATORS: the labels of these lines are padded to the same width */
    g_string_append_printf(out, _("File:         %s\n"), name);
    g_string_append_printf(out, _("Path:         %s\n"), full);
    g_string_append_printf(out, _("Format:       %s\n"), ext_upper);
    g_string_append_printf(out, _("Size:         %1$s (%2$s bytes)\n"), size_str, bytes);
    g_free(bytes);
    g_string_append_printf(out, _("Permissions:  0%o\n"), (unsigned int)(st.st_mode & 0777));
    g_string_append_printf(out, "Modified:     %s\n", time_str ? time_str : "-");
    g_string_append(out, _("Mode:         Text / Raw mode active\n"));
    g_string_append(out, _("              Press 'm' to switch to interactive audio player\n"));
    g_string_append_printf(out, "═══════════════════════════════════════════════════════════════\n\n");

    /* Read first 512 bytes for hex dump and inspection */
    FILE* fp = g_fopen(full, "rb");
    if (fp != NULL) {
        unsigned char buf[512];
        size_t n = fread(buf, 1, sizeof(buf), fp);
        fclose(fp);

        const gchar* tag_detected = NULL;
        if (n >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
            tag_detected = "ID3v2 header (MP3 audio)";
        } else if (n >= 12 && memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WAVE", 4) == 0) {
            tag_detected = "RIFF WAVE container";
        } else if (n >= 4 && memcmp(buf, "OggS", 4) == 0) {
            tag_detected = "Ogg bitstream container (Vorbis / Opus / FLAC)";
        } else if (n >= 4 && memcmp(buf, "fLaC", 4) == 0) {
            tag_detected = "Free Lossless Audio Codec (FLAC)";
        } else if (n >= 4 && memcmp(buf, "MThd", 4) == 0) {
            tag_detected = "Standard MIDI File";
        }

        if (tag_detected != NULL) {
            g_string_append_printf(out, _("Detected Header: %s\n\n"), tag_detected);
        }

        g_string_append_printf(out, _("=== File Header Dump (first %zu bytes) ===\n"), n);
        for (size_t i = 0; i < n; i += 16) {
            g_string_append_printf(out, "%08zx  ", i);
            for (size_t j = 0; j < 16; j++) {
                if (i + j < n) {
                    g_string_append_printf(out, "%02x ", buf[i + j]);
                } else {
                    g_string_append(out, "   ");
                }
                if (j == 7) {
                    g_string_append_c(out, ' ');
                }
            }
            g_string_append(out, " |");
            for (size_t j = 0; j < 16 && (i + j) < n; j++) {
                unsigned char c = buf[i + j];
                g_string_append_c(out, (c >= 32 && c <= 126) ? (gchar)c : '.');
            }
            g_string_append(out, "|\n");
        }
    } else {
        g_string_append(out, _("[ Cannot open file for reading ]\n"));
    }

    gtk_text_buffer_set_text(app->buffer, out->str, (gint)out->len);
    search_forget_viewer_match(app);

    g_string_free(out, TRUE);
    g_free(ext_upper);
    g_free(size_str);
    g_free(time_str);
}

/* --- Tags, read in a worker thread --- */

typedef struct {
    AppData* app;
    gchar* path;
    MediaTags* tags;
    GdkTexture* cover;
} AudioTagsTask;

static void audio_tags_task_free(gpointer data) {
    AudioTagsTask* t = data;
    g_free(t->path);
    media_tags_free(t->tags);
    g_clear_object(&t->cover);
    g_free(t);
}

static void audio_tags_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    AudioTagsTask* t = data;
    t->tags = media_tags_read_file(t->path);
    if (t->tags != NULL && t->tags->cover != NULL) {
        gsize len = 0;
        const guchar* bytes = g_bytes_get_data(t->tags->cover, &len);
        GdkPixbuf* pix = pixbuf_from_bytes(bytes, len, 480);
        if (pix != NULL) {
            t->cover = texture_new_for_pixbuf(pix);
            g_object_unref(pix);
        }
    }
    g_task_return_boolean(task, TRUE);
}

static void on_audio_tags_ready(GObject* source, GAsyncResult* res, gpointer user_data) {
    (void)source;
    (void)user_data;
    GTask* task = G_TASK(res);
    AudioTagsTask* t = g_task_get_task_data(task);
    AudioPreview* a = t->app->audio;
    if (a == NULL || g_cancellable_is_cancelled(g_task_get_cancellable(task)) ||
        g_strcmp0(a->current_path, t->path) != 0) {
        return;
    }
    MediaTags* tags = t->tags;
    if (tags == NULL) {
        return;
    }
    if (tags->title != NULL) {
        gchar* title = tags->artist != NULL ? g_strdup_printf("%s — %s", tags->artist, tags->title)
                                            : g_strdup(tags->title);
        gtk_label_set_text(GTK_LABEL(a->title), title);
        g_free(title);
    }
    GString* line = g_string_new(tags->album != NULL ? tags->album : "");
    if (tags->year != NULL) {
        g_string_append_printf(line, "%s%.4s", line->len > 0 ? " · " : "", tags->year);
    }
    if (tags->track != NULL) {
        if (line->len > 0) g_string_append(line, " · ");
        g_string_append_printf(line, _("Track %s"), tags->track);
    }
    if (tags->genre != NULL) {
        g_string_append_printf(line, "%s%s", line->len > 0 ? " · " : "", tags->genre);
    }
    gtk_label_set_text(GTK_LABEL(a->tag_line), line->str);
    gtk_widget_set_visible(a->tag_line, line->len > 0);
    g_string_free(line, TRUE);
    if (t->cover != NULL) {
        gtk_image_set_from_paintable(GTK_IMAGE(a->cover), GDK_PAINTABLE(t->cover));
        gtk_widget_set_visible(a->cover, TRUE);
        gtk_widget_set_visible(a->icon, FALSE);
    }
    g_free(a->details);
    a->details = media_tags_technical_line(tags);
    update_audio_metadata_label(t->app, a->stream);
}

static void read_audio_tags(AppData* app, const gchar* full) {
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    app->preview_cancellable = g_cancellable_new();
    AudioTagsTask* t = g_new0(AudioTagsTask, 1);
    t->app = app;
    t->path = g_strdup(full);
    GTask* task = g_task_new(NULL, app->preview_cancellable, on_audio_tags_ready, NULL);
    g_task_set_task_data(task, t, audio_tags_task_free);
    g_task_run_in_thread(task, audio_tags_thread);
    g_object_unref(task);
}

void render_audio_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app == NULL || name == NULL || full == NULL) {
        return;
    }

    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }

    if (preview_state_raw(app)) {
        render_audio_raw(app, name, full);
        return;
    }

    AudioPreview* a = app->audio;
    if (a == NULL) {
        return;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "audio");

    if (a->current_path != NULL && g_strcmp0(a->current_path, full) == 0) {
        return;
    }

    audio_preview_stop(app);
    a->current_path = g_strdup(full);

    gtk_label_set_text(GTK_LABEL(a->title), name);
    gtk_label_set_text(GTK_LABEL(a->status), "");
    gtk_widget_set_visible(a->status, FALSE);
    gtk_image_clear(GTK_IMAGE(a->cover));
    gtk_widget_set_visible(a->cover, FALSE);
    gtk_widget_set_visible(a->icon, TRUE);
    gtk_widget_set_visible(a->tag_line, FALSE);

    update_audio_metadata_label(app, NULL);
    read_audio_tags(app, full);

    GFile* file = g_file_new_for_path(full);
    gtk_media_file_set_file(GTK_MEDIA_FILE(a->stream), file);
    g_object_unref(file);
}
