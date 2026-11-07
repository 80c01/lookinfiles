#ifndef MEDIA_PLAYER_H
#define MEDIA_PLAYER_H

#include <gtk/gtk.h>

/* The media stream of the video players (preview, Video tabs): a
 * GtkMediaFile (gtk_media_file_set_file(), gtk_media_file_clear()) that
 * plays with GStreamer (playbin) and lets the user choose the audio track
 * (language) and the subtitles: those of the file, and the subtitle files
 * beside it ("<name>.srt", "<name>.fr.srt", .ass, .ssa, .vtt; the first one
 * is shown when the video opens). Built without GStreamer, it is a plain
 * GtkMediaFile with no choice of tracks. */

typedef enum {
    MEDIA_TRACK_AUDIO,
    MEDIA_TRACK_SUBTITLE,
} MediaTrackKind;

/* A new stream with no file */
GtkMediaStream* media_player_new(void);

/* Labels of the tracks of kind ("English", "French - Commentary",
 * "Movie.fr.srt"), NULL-terminated; empty while no file is loaded, NULL
 * when the stream has no choice of tracks (no GStreamer). Free with
 * g_strfreev(). */
gchar** media_player_track_labels(GtkMediaStream* stream, MediaTrackKind kind);

/* Index of the current track of kind in the labels; -1 for none (subtitles
 * off) */
gint media_player_get_track(GtkMediaStream* stream, MediaTrackKind kind);

/* Play track index of kind (-1: subtitles off) */
void media_player_set_track(GtkMediaStream* stream, MediaTrackKind kind, gint index);

/* The next track of kind, after the last the first; subtitles go through
 * "off" after the last. Returns FALSE when there is nothing to change. */
gboolean media_player_cycle_track(GtkMediaStream* stream, MediaTrackKind kind);

/* Call callback(stream, object) when the tracks or the current ones change,
 * while object lives (g_signal_connect_object()). Does nothing on a stream
 * with no choice of tracks. */
void media_player_connect_tracks_changed(GtkMediaStream* stream, GCallback callback, gpointer object);

#endif /* MEDIA_PLAYER_H */
