#include "video.h"
#include "text_style.h"
#include "file_types.h"
#include <sys/stat.h>
#include <glib/gstdio.h>

static void update_video_metadata_label(AppData* app, GtkMediaStream* stream) {
    if (app == NULL || app->video_current_path == NULL || app->video_meta == NULL) {
        return;
    }

    struct stat st;
    gint64 fsize = (g_stat(app->video_current_path, &st) == 0) ? (gint64)st.st_size : 0;
    gchar* size_str = g_format_size((guint64)fsize);

    const gchar* ext = file_extension(app->video_current_path);
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

        gboolean has_v = gtk_media_stream_has_video(stream);
        gboolean has_a = gtk_media_stream_has_audio(stream);
        if (has_v && has_a) {
            g_string_append(info, "  •  Video / Audio");
        } else if (has_v) {
            g_string_append(info, "  •  Video only");
        } else if (has_a) {
            g_string_append(info, "  •  Audio only");
        }
    }

    g_string_append(info, "  •  [press m for details]");

    gtk_label_set_text(GTK_LABEL(app->video_meta), info->str);

    g_string_free(info, TRUE);
    g_free(ext_upper);
    g_free(size_str);
}

static void on_video_duration_notify(GtkMediaStream* stream, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    update_video_metadata_label(app, stream);
}

static void on_video_prepared_notify(GtkMediaStream* stream, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    update_video_metadata_label(app, stream);
}

static void on_video_error_notify(GtkMediaStream* stream, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    if (app == NULL || stream == NULL) {
        return;
    }
    const GError* err = gtk_media_stream_get_error(stream);
    if (err != NULL && app->video_status != NULL) {
        const gchar* err_msg = (err->message != NULL && err->message[0] != '\0')
                                   ? err->message
                                   : "Video playback error";
        gchar* msg = g_strdup_printf("⚠ Media playback warning: %s (press 'm' to inspect raw file details)",
                                     err_msg);
        gtk_label_set_text(GTK_LABEL(app->video_status), msg);
        gtk_widget_set_visible(app->video_status, TRUE);
        g_free(msg);
    }
}

void video_preview_init(AppData* app) {
    if (app == NULL || app->preview_stack == NULL) {
        return;
    }

    GtkWidget* video_scroll = gtk_scrolled_window_new();
    gtk_widget_set_name(video_scroll, "video-scroll");
    gtk_widget_set_vexpand(video_scroll, TRUE);
    gtk_widget_set_hexpand(video_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(video_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget* video_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_name(video_box, "video-box");
    gtk_widget_set_hexpand(video_box, TRUE);
    gtk_widget_set_vexpand(video_box, TRUE);
    gtk_widget_set_margin_start(video_box, 16);
    gtk_widget_set_margin_end(video_box, 16);
    gtk_widget_set_margin_top(video_box, 16);
    gtk_widget_set_margin_bottom(video_box, 16);

    /* Header banner */
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(header_box, TRUE);

    GtkWidget* video_title = gtk_label_new("🎬  Video Preview");
    gtk_widget_set_name(video_title, "video-title");
    gtk_label_set_selectable(GTK_LABEL(video_title), TRUE);
    gtk_label_set_xalign(GTK_LABEL(video_title), 0.5f);
    gtk_box_append(GTK_BOX(header_box), video_title);

    GtkWidget* video_meta = gtk_label_new("");
    gtk_widget_set_name(video_meta, "video-meta");
    gtk_label_set_selectable(GTK_LABEL(video_meta), TRUE);
    gtk_label_set_xalign(GTK_LABEL(video_meta), 0.5f);
    gtk_box_append(GTK_BOX(header_box), video_meta);

    GtkWidget* video_status = gtk_label_new("");
    gtk_widget_set_name(video_status, "video-status");
    gtk_label_set_xalign(GTK_LABEL(video_status), 0.5f);
    gtk_widget_set_visible(video_status, FALSE);
    gtk_box_append(GTK_BOX(header_box), video_status);

    gtk_box_append(GTK_BOX(video_box), header_box);

    /* Video player widget */
    GtkWidget* video_widget = gtk_video_new();
    gtk_widget_set_name(video_widget, "video-player");
    gtk_widget_set_hexpand(video_widget, TRUE);
    gtk_widget_set_vexpand(video_widget, TRUE);
    gtk_widget_set_size_request(video_widget, 200, 160);
    gtk_video_set_autoplay(GTK_VIDEO(video_widget), FALSE);
    gtk_video_set_loop(GTK_VIDEO(video_widget), FALSE);
    gtk_box_append(GTK_BOX(video_box), video_widget);

    /* One media stream for the whole session: previews swap its file. A new
     * stream per file (gtk_video_set_file()) is never freed by the GStreamer
     * backend, so each previewed video leaked its pipeline's threads and file
     * descriptors until the process ran out of them. Swapping the file shuts
     * the previous pipeline down. */
    GtkMediaStream* stream = gtk_media_file_new();
    g_signal_connect(stream, "notify::duration", G_CALLBACK(on_video_duration_notify), app);
    g_signal_connect(stream, "notify::prepared", G_CALLBACK(on_video_prepared_notify), app);
    g_signal_connect(stream, "notify::error", G_CALLBACK(on_video_error_notify), app);
    gtk_video_set_media_stream(GTK_VIDEO(video_widget), stream);
    g_object_unref(stream);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(video_scroll), video_box);
    gtk_stack_add_named(GTK_STACK(app->preview_stack), video_scroll, "video");

    app->video_scroll = video_scroll;
    app->video_box = video_box;
    app->video_widget = video_widget;
    app->video_title = video_title;
    app->video_meta = video_meta;
    app->video_status = video_status;
    app->video_current_path = NULL;
}

void video_preview_stop(AppData* app) {
    if (app == NULL || app->video_widget == NULL || app->video_current_path == NULL) {
        return;
    }
    /* Cleared first: the notifications emitted while closing see no file */
    g_free(app->video_current_path);
    app->video_current_path = NULL;

    GtkMediaStream* stream = gtk_video_get_media_stream(GTK_VIDEO(app->video_widget));
    if (stream != NULL) {
        if (gtk_media_stream_get_playing(stream)) {
            gtk_media_stream_pause(stream);
        }
        gtk_media_file_clear(GTK_MEDIA_FILE(stream));
    }
}

void video_preview_pause(AppData* app) {
    if (app == NULL || app->video_widget == NULL) {
        return;
    }
    GtkMediaStream* stream = gtk_video_get_media_stream(GTK_VIDEO(app->video_widget));
    if (stream != NULL && gtk_media_stream_get_playing(stream)) {
        gtk_media_stream_pause(stream);
    }
}

void video_preview_toggle_play(AppData* app) {
    if (app == NULL || app->video_widget == NULL) {
        return;
    }
    GtkMediaStream* stream = gtk_video_get_media_stream(GTK_VIDEO(app->video_widget));
    if (stream != NULL) {
        if (gtk_media_stream_get_playing(stream)) {
            gtk_media_stream_pause(stream);
        } else {
            gtk_media_stream_play(stream);
        }
    }
}

static void render_video_raw(AppData* app, const gchar* name, const gchar* full) {
    video_preview_stop(app);

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
    g_string_append_printf(out, "🎬  %s\n", name);
    g_string_append_printf(out, "═══════════════════════════════════════════════════════════════\n");
    g_string_append_printf(out, "File:         %s\n", name);
    g_string_append_printf(out, "Path:         %s\n", full);
    g_string_append_printf(out, "Container:    %s\n", ext_upper);
    g_string_append_printf(out, "Size:         %s (%" G_GINT64_FORMAT " bytes)\n", size_str, fsize);
    g_string_append_printf(out, "Permissions:  0%o\n", (unsigned int)(st.st_mode & 0777));
    g_string_append_printf(out, "Modified:     %s\n", time_str ? time_str : "-");
    g_string_append_printf(out, "Mode:         Text / Raw mode active\n");
    g_string_append_printf(out, "              Press 'm' to switch to interactive video player\n");
    g_string_append_printf(out, "═══════════════════════════════════════════════════════════════\n\n");

    /* Read first 512 bytes for hex dump */
    FILE* fp = g_fopen(full, "rb");
    if (fp != NULL) {
        unsigned char buf[512];
        size_t n = fread(buf, 1, sizeof(buf), fp);
        fclose(fp);

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

void render_video_preview(AppData* app, const gchar* name, const gchar* full) {
    if (app == NULL || name == NULL || full == NULL) {
        return;
    }

    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }

    if (app->render_raw) {
        render_video_raw(app, name, full);
        return;
    }

    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "video");
    }

    if (app->video_current_path != NULL && g_strcmp0(app->video_current_path, full) == 0) {
        return;
    }

    video_preview_stop(app);
    app->video_current_path = g_strdup(full);

    if (app->video_title != NULL) {
        gchar* title_text = g_strdup_printf("🎬  %s", name);
        gtk_label_set_text(GTK_LABEL(app->video_title), title_text);
        g_free(title_text);
    }

    if (app->video_status != NULL) {
        gtk_label_set_text(GTK_LABEL(app->video_status), "");
        gtk_widget_set_visible(app->video_status, FALSE);
    }

    update_video_metadata_label(app, NULL);

    GtkMediaStream* stream = (app->video_widget != NULL)
                                 ? gtk_video_get_media_stream(GTK_VIDEO(app->video_widget))
                                 : NULL;
    if (stream != NULL) {
        GFile* file = g_file_new_for_path(full);
        gtk_media_file_set_file(GTK_MEDIA_FILE(stream), file);
        g_object_unref(file);
        if (gtk_media_stream_is_prepared(stream)) {
            update_video_metadata_label(app, stream);
        }
    }
}
