#ifndef MEDIA_TAGS_H
#define MEDIA_TAGS_H

#include <glib.h>

/* Tags and technical details of audio and video files, read without a media
 * framework: ID3v2 (2.2 to 2.4) and ID3v1 (MP3), FLAC metadata blocks and
 * Vorbis comments (FLAC, Ogg Vorbis, Opus), MP4 / M4A atoms (iTunes tags,
 * duration, video size), RIFF WAVE. Only the headers are read (the MP4 'moov'
 * box may be at the end of the file: it is found by skipping the others).
 * Pure GLib, unit-tested. */

typedef struct {
    gchar* title;
    gchar* artist;
    gchar* album;
    gchar* album_artist;
    gchar* year;
    gchar* track;            /* "3" or "3/12" */
    gchar* genre;
    gchar* comment;
    GBytes* cover;           /* embedded picture (front cover first), or NULL */
    gchar* codec;            /* "MP3", "FLAC", "Vorbis", "Opus", "AAC", "ALAC", "PCM"... */
    guint sample_rate;       /* Hz, 0 if unknown */
    guint channels;
    guint bits_per_sample;
    guint bitrate;           /* kbit/s, 0 if unknown */
    gdouble duration;        /* seconds, 0 if unknown */
    guint width;             /* video frame size, 0 if none */
    guint height;
} MediaTags;

/* Details of a media file; NULL if it cannot be read or nothing was found */
MediaTags* media_tags_read_file(const gchar* path);
void media_tags_free(MediaTags* tags);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MediaTags, media_tags_free)

/* "3:45", "1:02:03" */
gchar* media_tags_format_duration(gdouble seconds);

/* Technical summary: "MP3 · 320 kbit/s · 44.1 kHz · stereo · 3:45" (NULL
 * parts skipped). Caller frees. */
gchar* media_tags_technical_line(const MediaTags* tags);

/* Largest embedded picture kept */
#define MEDIA_TAGS_MAX_COVER (8 * 1024 * 1024)

#endif /* MEDIA_TAGS_H */
