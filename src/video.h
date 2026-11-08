#ifndef VIDEO_H
#define VIDEO_H

#include "core/app_types.h"

/* Initialize video preview widgets inside app->preview_stack */
void video_preview_init(AppData* app);

/* Render video preview (or raw info / hex dump in the raw view, preview_state_raw()) */
void render_video_preview(AppData* app, const gchar* name, const gchar* full);

/* Stop playback and clear the active video stream */
void video_preview_stop(AppData* app);

/* Stop playback and free the player state (at exit) */
void video_preview_free(AppData* app);

/* Pause video playback if currently playing */
void video_preview_pause(AppData* app);

/* Toggle play/pause state of the active video stream */
void video_preview_toggle_play(AppData* app);

/* Video preview shown (not raw) and keyval Left / Right without modifiers:
 * seek 5 s back / forward. Returns TRUE when the key was used. */
gboolean video_preview_seek_key(AppData* app, guint keyval, GdkModifierType state);

#endif /* VIDEO_H */
