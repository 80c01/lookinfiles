#ifndef MEDIA_TRACKS_H
#define MEDIA_TRACKS_H

#include <glib.h>

/* Helpers of the audio and subtitle track menus of the video players
 * (media_player.c): track labels, the subtitle files that go with a video.
 * Pure GLib, unit-tested. */

/* Label of a track: "English", "English - Commentary", "Commentary",
 * "Track 2" (index counts from 0). language is a language name or code;
 * NULL, "" and "und" (undetermined) mean unknown; generic titles
 * ("Audio", "Subtitle") are left out. */
gchar* media_track_label(const gchar* language, const gchar* title, guint index);

/* The subtitle files (.srt, .ass, .ssa, .vtt) of the video video_name
 * among names (the entries of its directory): "<base>.<ext>" and
 * "<base>.<anything>.<ext>", where <base> is video_name without its
 * extension. The file "<base>.<ext>" comes first, the others sorted.
 * Returns a NULL-terminated array, empty when there are none. */
gchar** media_subtitle_files(const gchar* video_name, const gchar* const* names);

#endif /* MEDIA_TRACKS_H */
