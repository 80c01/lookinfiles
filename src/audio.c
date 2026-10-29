#include "audio.h"
#include "text_style.h"
#include "file_types.h"
#include <sys/stat.h>
#include <glib/gstdio.h>

static void update_audio_metadata_label(AppData* app, GtkMediaStream* stream) {
    if (app == NULL || app->audio_current_path == NULL || app->audio_meta == NULL) {
        return;
    }

    struct stat st;
    gint64 fsize = (g_stat(app->audio_current_path, &st) == 0) ? (gint64)st.st_size : 0;
    gchar* size_str = g_format_size((guint64)fsize);

    const gchar* ext = file_extension(app->audio_current_path);
    gchar* ext_upper = g_ascii_strup(ext, -1);

    GString* info = g_string_new(ext_upper);
    g_string_append_printf(info, "  •  %s", size_str);

    if (stream != NULL) {
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

    g_string_append(info, "  •  [press m for details]");

    gtk_label_set_text(GTK_LABEL(app->audio_meta), info->str);

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
    if (app == NULL || stream == NULL) {
        return;
    }
    const GError* err = gtk_media_stream_get_error(stream);
    if (err != NULL && app->audio_status != NULL) {
        const gchar* err_msg = (err->message != NULL && err->message[0] != '\0')
                                   ? err->message
                                   : "Audio playback error";
        gchar* msg = g_strdup_printf("⚠ Media playback warning: %s (press 'm' to inspect raw file details)",
                                     err_msg);
        gtk_label_set_text(GTK_LABEL(app->audio_status), msg);
        gtk_widget_set_visible(app->audio_status, TRUE);
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

    /* Big musical note icon */
    GtkWidget* icon_label = gtk_label_new("🎵");
    gtk_widget_set_name(icon_label, "audio-icon");
    gtk_widget_set_halign(icon_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(audio_card), icon_label);

    /* Title label */
    GtkWidget* audio_title = gtk_label_new("🎵  Audio Preview");
    gtk_widget_set_name(audio_title, "audio-title");
    gtk_label_set_selectable(GTK_LABEL(audio_title), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(audio_title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(audio_title), 0.5f);
    gtk_box_append(GTK_BOX(audio_card), audio_title);

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

    app->audio_scroll = audio_scroll;
    app->audio_box = audio_box;
    app->audio_card = audio_card;
    app->audio_controls = audio_controls;
    app->audio_title = audio_title;
    app->audio_meta = audio_meta;
    app->audio_status = audio_status;
    app->audio_stream = NULL;
    app->audio_duration_sig = 0;
    app->audio_prepared_sig = 0;
    app->audio_error_sig = 0;
    app->audio_current_path = NULL;
}

void audio_preview_stop(AppData* app) {
    if (app == NULL || (app->audio_stream == NULL && app->audio_current_path == NULL)) {
        return;
    }

    if (app->audio_stream != NULL) {
        GtkMediaStream* stream = app->audio_stream;
        app->audio_stream = NULL;

        if (app->audio_duration_sig != 0) {
            g_signal_handler_disconnect(stream, app->audio_duration_sig);
            app->audio_duration_sig = 0;
        }
        if (app->audio_prepared_sig != 0) {
            g_signal_handler_disconnect(stream, app->audio_prepared_sig);
            app->audio_prepared_sig = 0;
        }
        if (app->audio_error_sig != 0) {
            g_signal_handler_disconnect(stream, app->audio_error_sig);
            app->audio_error_sig = 0;
        }

        if (gtk_media_stream_get_playing(stream)) {
            gtk_media_stream_pause(stream);
        }

        if (app->audio_controls != NULL) {
            gtk_media_controls_set_media_stream(GTK_MEDIA_CONTROLS(app->audio_controls), NULL);
        }

        g_object_unref(stream);
    }

    g_free(app->audio_current_path);
    app->audio_current_path = NULL;
}

void audio_preview_pause(AppData* app) {
    if (app == NULL || app->audio_stream == NULL) {
        return;
    }
    if (gtk_media_stream_get_playing(app->audio_stream)) {
        gtk_media_stream_pause(app->audio_stream);
    }
}

void audio_preview_toggle_play(AppData* app) {
    if (app == NULL || app->audio_stream == NULL) {
        return;
    }
    if (gtk_media_stream_get_playing(app->audio_stream)) {
        gtk_media_stream_pause(app->audio_stream);
    } else {
        gtk_media_stream_play(app->audio_stream);
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

    struct stat st;
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
    g_string_append_printf(out, "🎵  %s\n", name);
    g_string_append_printf(out, "═══════════════════════════════════════════════════════════════\n");
    g_string_append_printf(out, "File:         %s\n", name);
    g_string_append_printf(out, "Path:         %s\n", full);
    g_string_append_printf(out, "Format:       %s\n", ext_upper);
    g_string_append_printf(out, "Size:         %s (%" G_GINT64_FORMAT " bytes)\n", size_str, fsize);
    g_string_append_printf(out, "Permissions:  0%o\n", (unsigned int)(st.st_mode & 0777));
    g_string_append_printf(out, "Modified:     %s\n", time_str ? time_str : "-");
    g_string_append_printf(out, "Mode:         Text / Raw mode active\n");
    g_string_append_printf(out, "              Press 'm' to switch to interactive audio player\n");
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
            g_string_append_printf(out, "Detected Header: %s\n\n", tag_detected);
        }

        g_string_append_printf(out, "=== File Header Dump (first %zu bytes) ===\n", n);
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
        g_string_append(out, "[ Cannot open file for reading ]\n");
    }

    gtk_text_buffer_set_text(app->buffer, out->str, (gint)out->len);
    app->viewer_match = -1;

    g_string_free(out, TRUE);
    g_free(ext_upper);
    g_free(size_str);
    g_free(time_str);
}

void render_audio_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app == NULL || name == NULL || full == NULL) {
        return;
    }

    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }

    if (app->render_raw) {
        render_audio_raw(app, name, full);
        return;
    }

    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "audio");
    }

    if (app->audio_current_path != NULL && g_strcmp0(app->audio_current_path, full) == 0) {
        return;
    }

    audio_preview_stop(app);
    app->audio_current_path = g_strdup(full);

    if (app->audio_title != NULL) {
        gchar* title_text = g_strdup_printf("🎵  %s", name);
        gtk_label_set_text(GTK_LABEL(app->audio_title), title_text);
        g_free(title_text);
    }

    if (app->audio_status != NULL) {
        gtk_label_set_text(GTK_LABEL(app->audio_status), "");
        gtk_widget_set_visible(app->audio_status, FALSE);
    }

    update_audio_metadata_label(app, NULL);

    GFile* file = g_file_new_for_path(full);
    if (file == NULL) {
        return;
    }
    GtkMediaStream* stream = gtk_media_file_new_for_file(file);
    g_object_unref(file);

    if (stream == NULL) {
        if (app->audio_status != NULL) {
            gtk_label_set_text(GTK_LABEL(app->audio_status),
                               "⚠ Could not create media stream for file (press 'm' for details)");
            gtk_widget_set_visible(app->audio_status, TRUE);
        }
        return;
    }

    app->audio_stream = stream;
    if (app->audio_controls != NULL) {
        gtk_media_controls_set_media_stream(GTK_MEDIA_CONTROLS(app->audio_controls), stream);
    }

    app->audio_duration_sig = g_signal_connect(stream, "notify::duration",
                                              G_CALLBACK(on_audio_duration_notify), app);
    app->audio_prepared_sig = g_signal_connect(stream, "notify::prepared",
                                              G_CALLBACK(on_audio_prepared_notify), app);
    app->audio_error_sig = g_signal_connect(stream, "notify::error",
                                           G_CALLBACK(on_audio_error_notify), app);

    if (gtk_media_stream_is_prepared(stream)) {
        update_audio_metadata_label(app, stream);
    }
}
